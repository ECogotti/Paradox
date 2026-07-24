#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Details/WorldStatePropertyPickerModel.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Serialization/WorldStatePropertySerializer.h"
#include "ScopedTransaction.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

/** Covers Blueprint/SCS source discovery, filters, missing selections, duplicates and Undo/Redo. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldStatePropertyPickerModelTest,
	"WorldState.Editor.PropertyPicker.BlueprintSourcesFilteringMissingDuplicatesAndTransactions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldStatePropertyPickerModelTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(),
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("WorldStatePickerBlueprint")),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None);
	TestNotNull(TEXT("Transient Blueprint exists"), Blueprint);
	if (!Blueprint || !Blueprint->SimpleConstructionScript)
	{
		return false;
	}

	USCS_Node* ParticipantNode = Blueprint->SimpleConstructionScript->CreateNode(UWorldStateParticipantComponent::StaticClass(), TEXT("WorldStateParticipant"));
	USCS_Node* SceneNode = Blueprint->SimpleConstructionScript->CreateNode(USceneComponent::StaticClass(), TEXT("AuthoredScene"));
	Blueprint->SimpleConstructionScript->AddNode(ParticipantNode);
	ParticipantNode->AddChildNode(SceneNode);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	UWorldStateParticipantComponent* Participant = Cast<UWorldStateParticipantComponent>(ParticipantNode->ComponentTemplate);
	TestNotNull(TEXT("Blueprint participant template exists"), Participant);
	if (!Participant)
	{
		return false;
	}
	Participant->SetFlags(RF_Transactional);
	const TArray<FWorldStatePropertyPickerSource> Sources = FWorldStatePropertyPickerModel::BuildSources(*Participant);
	TestTrue(TEXT("Blueprint CDO provides Owner Actor source"), Sources.ContainsByPredicate([](const FWorldStatePropertyPickerSource& Source)
	{
		return Source.Id.Kind == EWorldStateCaptureSourceKind::OwnerActor;
	}));
	const FWorldStatePropertyPickerSource* SceneSource = Sources.FindByPredicate([](const FWorldStatePropertyPickerSource& Source)
	{
		return Source.Id.Kind == EWorldStateCaptureSourceKind::ActorComponent && Source.Id.ComponentName == TEXT("AuthoredScene");
	});
	TestNotNull(TEXT("Simple Construction Script component source is enumerated"), SceneSource);
	if (!SceneSource)
	{
		return false;
	}

	const TArray<FWorldStatePropertyPickerCandidate> AllCandidates = FWorldStatePropertyPickerModel::BuildCandidates(*Participant, *SceneSource, true);
	TestTrue(TEXT("picker exposes validation state for supported candidates"), AllCandidates.ContainsByPredicate([](const FWorldStatePropertyPickerCandidate& Candidate)
	{
		return Candidate.Validation.IsValid();
	}));
	TestTrue(TEXT("picker exposes validation state for unsupported hard-reference candidates"), AllCandidates.ContainsByPredicate([](const FWorldStatePropertyPickerCandidate& Candidate)
	{
		return Candidate.Validation.Status == EWorldStatePropertyValidationStatus::HardObjectReferenceRejected ||
			Candidate.Validation.Status == EWorldStatePropertyValidationStatus::WeakObjectReferenceRejected;
	}));
	const TArray<FWorldStatePropertyPickerCandidate> SupportedCandidates = FWorldStatePropertyPickerModel::BuildCandidates(*Participant, *SceneSource, false);
	TestTrue(TEXT("supported-only candidates are filtered"), SupportedCandidates.Num() > 0 && !SupportedCandidates.ContainsByPredicate([](const FWorldStatePropertyPickerCandidate& Candidate)
	{
		return !Candidate.Validation.IsValid();
	}));

	const FWorldStatePropertyPickerCandidate* SelectedCandidate = SupportedCandidates.FindByPredicate([](const FWorldStatePropertyPickerCandidate& Candidate)
	{
		return Candidate.PropertyName == GET_MEMBER_NAME_CHECKED(USceneComponent, ComponentTags);
	});
	if (!SelectedCandidate && !SupportedCandidates.IsEmpty())
	{
		SelectedCandidate = &SupportedCandidates[0];
	}
	TestNotNull(TEXT("a supported candidate can be selected"), SelectedCandidate);
	if (!SelectedCandidate)
	{
		return false;
	}

	FWorldStatePropertySelection Selection;
	Selection.CaptureSourceId = SceneSource->Id;
	Selection.PropertyName = SelectedCandidate->PropertyName;
	Selection.ExpectedSourceClass = FSoftClassPath(SceneSource->Object->GetClass());
	Selection.ExpectedTypeSignature = SelectedCandidate->Validation.TypeSignature;
	const int32 BeforeTransaction = Participant->CapturedProperties.Num();
	{
		const FScopedTransaction Transaction(NSLOCTEXT("WorldStateEditorTests", "AddSelection", "Add World State Test Selection"));
		Participant->Modify();
		Participant->CapturedProperties.Add(Selection);
	}
	TestEqual(TEXT("transaction adds selection"), Participant->CapturedProperties.Num(), BeforeTransaction + 1);
	const TArray<FWorldStatePropertyPickerCandidate> DuplicateCandidates = FWorldStatePropertyPickerModel::BuildCandidates(*Participant, *SceneSource, false);
	TestTrue(TEXT("duplicate candidate is marked rather than offered twice"), DuplicateCandidates.ContainsByPredicate([&Selection](const FWorldStatePropertyPickerCandidate& Candidate)
	{
		return Candidate.PropertyName == Selection.PropertyName && Candidate.bAlreadySelected;
	}));

	if (GEditor)
	{
		GEditor->UndoTransaction();
		TestEqual(TEXT("Undo removes picker mutation"), Participant->CapturedProperties.Num(), BeforeTransaction);
		GEditor->RedoTransaction();
		TestEqual(TEXT("Redo restores picker mutation"), Participant->CapturedProperties.Num(), BeforeTransaction + 1);
	}

	FWorldStatePropertySelection MissingSelection;
	MissingSelection.CaptureSourceId = FWorldStateCaptureSourceId::Component(TEXT("RemovedAuthoredComponent"));
	MissingSelection.PropertyName = TEXT("RemovedProperty");
	const FWorldStatePropertyPickerCandidate Missing = FWorldStatePropertyPickerModel::DescribeSelection(*Participant, MissingSelection);
	TestEqual(TEXT("missing authored source remains diagnosable"), Missing.Validation.Status, EWorldStatePropertyValidationStatus::MissingSource);
	FWorldStatePropertySelection RemovedPropertySelection;
	RemovedPropertySelection.CaptureSourceId = SceneSource->Id;
	RemovedPropertySelection.PropertyName = TEXT("RemovedReflectedProperty");
	RemovedPropertySelection.ExpectedSourceClass = FSoftClassPath(SceneSource->Object->GetClass());
	const FWorldStatePropertyPickerCandidate RemovedProperty = FWorldStatePropertyPickerModel::DescribeSelection(*Participant, RemovedPropertySelection);
	TestEqual(TEXT("removed property on an existing source remains diagnosable"), RemovedProperty.Validation.Status, EWorldStatePropertyValidationStatus::MissingProperty);

	TArray<TWeakObjectPtr<UObject>> SingleSelection = { Participant };
	TArray<TWeakObjectPtr<UObject>> MultipleSelection = { Participant, SceneNode->ComponentTemplate };
	TestTrue(TEXT("unique source set is editable"), FWorldStatePropertyPickerModel::CanEditUniqueSources(SingleSelection));
	TestFalse(TEXT("ambiguous multi-edit is disabled"), FWorldStatePropertyPickerModel::CanEditUniqueSources(MultipleSelection));
	return true;
}

/** Covers transient Blueprint User Defined Struct validation and round-trip serialization without assets. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldStateUserDefinedStructTest,
	"WorldState.Editor.Serialization.TransientBlueprintUserDefinedStruct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldStateUserDefinedStructTest::RunTest(const FString& Parameters)
{
	UUserDefinedStruct* UserStruct = FStructureEditorUtils::CreateUserDefinedStruct(
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UUserDefinedStruct::StaticClass(), TEXT("WorldStateTransientStruct")),
		RF_Public | RF_Standalone | RF_Transactional);
	TestNotNull(TEXT("Transient User Defined Struct exists"), UserStruct);
	if (!UserStruct)
	{
		return false;
	}

	FEdGraphPinType IntegerType;
	IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
	TestTrue(TEXT("integer member added"), FStructureEditorUtils::AddVariable(UserStruct, IntegerType));
	TArray<FStructVariableDescription>& Descriptions = FStructureEditorUtils::GetVarDesc(UserStruct);
	TestTrue(TEXT("integer member renamed"), !Descriptions.IsEmpty() && FStructureEditorUtils::RenameVariable(UserStruct, Descriptions.Last().VarGuid, TEXT("Count")));

	FEdGraphPinType StringArrayType;
	StringArrayType.PinCategory = UEdGraphSchema_K2::PC_String;
	StringArrayType.ContainerType = EPinContainerType::Array;
	TestTrue(TEXT("array member added"), FStructureEditorUtils::AddVariable(UserStruct, StringArrayType));
	FStructureEditorUtils::CompileStructure(UserStruct);

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(),
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("WorldStateStructHolder")),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None);
	TestNotNull(TEXT("Transient holder Blueprint exists"), Blueprint);
	if (!Blueprint)
	{
		return false;
	}
	FEdGraphPinType StructType;
	StructType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	StructType.PinSubCategoryObject = UserStruct;
	TestTrue(TEXT("User Defined Struct property added to Blueprint"), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("DesignerState"), StructType));
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	UObject* DefaultObject = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
	FStructProperty* StructProperty = Blueprint->GeneratedClass
		? FindFProperty<FStructProperty>(Blueprint->GeneratedClass, TEXT("DesignerState"))
		: nullptr;
	TestNotNull(TEXT("compiled User Defined Struct property exists"), StructProperty);
	if (!DefaultObject || !StructProperty)
	{
		return false;
	}
	const FWorldStatePropertyValidationResult Validation = FWorldStatePropertySerializer::Validate(StructProperty);
	TestTrue(TEXT("complete User Defined Struct validates through generic property pipeline"), Validation.IsValid());

	FProperty* CountProperty = FStructureEditorUtils::GetPropertyByFriendlyName(UserStruct, TEXT("Count"));
	FNumericProperty* NumericCount = CastField<FNumericProperty>(CountProperty);
	void* StructValue = StructProperty->ContainerPtrToValuePtr<void>(DefaultObject);
	TestNotNull(TEXT("compiled integer member exists"), NumericCount);
	if (!NumericCount)
	{
		return false;
	}
	NumericCount->SetIntPropertyValue(CountProperty->ContainerPtrToValuePtr<void>(StructValue), static_cast<int64>(1234));
	TArray<uint8> Payload;
	FString Error;
	TestTrue(TEXT("User Defined Struct serializes as one root property"), FWorldStatePropertySerializer::Serialize(StructProperty, DefaultObject, Payload, Error));
	NumericCount->SetIntPropertyValue(CountProperty->ContainerPtrToValuePtr<void>(StructValue), static_cast<int64>(0));
	TestTrue(TEXT("User Defined Struct deserializes as one root property"), FWorldStatePropertySerializer::Deserialize(StructProperty, DefaultObject, Payload, Error));
	TestEqual(TEXT("User Defined Struct member restored"), static_cast<int32>(NumericCount->GetSignedIntPropertyValue(CountProperty->ContainerPtrToValuePtr<void>(StructValue))), 1234);

	UUserDefinedStruct* ForbiddenStruct = FStructureEditorUtils::CreateUserDefinedStruct(
		GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UUserDefinedStruct::StaticClass(), TEXT("WorldStateForbiddenStruct")),
		RF_Public | RF_Standalone | RF_Transactional);
	FEdGraphPinType ObjectType;
	ObjectType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ObjectType.PinSubCategoryObject = UObject::StaticClass();
	TestTrue(TEXT("hard-object member added to transient struct"), ForbiddenStruct && FStructureEditorUtils::AddVariable(ForbiddenStruct, ObjectType));
	FStructureEditorUtils::CompileStructure(ForbiddenStruct);
	FEdGraphPinType ForbiddenStructType;
	ForbiddenStructType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	ForbiddenStructType.PinSubCategoryObject = ForbiddenStruct;
	TestTrue(TEXT("forbidden struct property added"), FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("ForbiddenDesignerState"), ForbiddenStructType));
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	FStructProperty* ForbiddenProperty = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, TEXT("ForbiddenDesignerState"));
	const FWorldStatePropertyValidationResult ForbiddenValidation = FWorldStatePropertySerializer::Validate(ForbiddenProperty);
	TestFalse(TEXT("hard reference nested inside User Defined Struct is rejected"), ForbiddenValidation.IsValid());
	TestTrue(TEXT("nested User Defined Struct failure includes member path"), ForbiddenValidation.NestedFailurePath.Contains(TEXT("MemberVar")) || ForbiddenValidation.NestedFailurePath.Contains(TEXT("Object")));
	return true;
}

#endif
