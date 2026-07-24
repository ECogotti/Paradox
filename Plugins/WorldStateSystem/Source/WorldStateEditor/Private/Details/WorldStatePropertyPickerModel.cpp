#include "Details/WorldStatePropertyPickerModel.h"

#include "Components/WorldStateParticipantComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"

namespace
{
	/** Resolves the owning Blueprint from templates, generated classes or live Blueprint instances. */
	UBlueprint* FindOwningBlueprint(const UWorldStateParticipantComponent& Participant)
	{
		if (const UBlueprintGeneratedClass* GeneratedClass = Participant.GetTypedOuter<UBlueprintGeneratedClass>())
		{
			return Cast<UBlueprint>(GeneratedClass->ClassGeneratedBy);
		}
		if (const UBlueprint* Blueprint = Participant.GetTypedOuter<UBlueprint>())
		{
			return const_cast<UBlueprint*>(Blueprint);
		}
		return Participant.GetOwner() ? UBlueprint::GetBlueprintFromClass(Participant.GetOwner()->GetClass()) : nullptr;
	}

	/** Adds only Components whose authored name can be reconstructed after respawn or Blueprint reinstancing. */
	void AddComponentSource(
		const UWorldStateParticipantComponent& Participant,
		UActorComponent* Component,
		TArray<FWorldStatePropertyPickerSource>& OutSources,
		FName StableComponentName = NAME_None)
	{
		// Instance Components have no authored reconstruction contract, so persisting their names would be unsafe.
		if (!Component || Component == &Participant || Component->CreationMethod == EComponentCreationMethod::Instance)
		{
			return;
		}
		const FWorldStateCaptureSourceId SourceId = FWorldStateCaptureSourceId::Component(
			StableComponentName.IsNone() ? Component->GetFName() : StableComponentName);
		if (OutSources.ContainsByPredicate([&SourceId](const FWorldStatePropertyPickerSource& Existing) { return Existing.Id == SourceId; }))
		{
			return;
		}
		FWorldStatePropertyPickerSource& Source = OutSources.AddDefaulted_GetRef();
		Source.Id = SourceId;
		Source.Object = Component;
		Source.Label = FText::FromName(Component->GetFName());
	}
}

TArray<FWorldStatePropertyPickerSource> FWorldStatePropertyPickerModel::BuildSources(const UWorldStateParticipantComponent& Participant)
{
	TArray<FWorldStatePropertyPickerSource> Sources;
	AActor* OwnerActor = Participant.GetOwner();
	UBlueprint* Blueprint = FindOwningBlueprint(Participant);
	// Blueprint templates may not have a live owner; their generated-class CDO exposes inherited native Components.
	if (!OwnerActor && Blueprint && Blueprint->GeneratedClass)
	{
		OwnerActor = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
	}
	if (OwnerActor)
	{
		FWorldStatePropertyPickerSource& OwnerSource = Sources.AddDefaulted_GetRef();
		OwnerSource.Id = FWorldStateCaptureSourceId::OwnerActor();
		OwnerSource.Object = OwnerActor;
		OwnerSource.Label = NSLOCTEXT("WorldStatePropertyPicker", "OwnerActor", "Owner Actor");

		TArray<UActorComponent*> Components;
		OwnerActor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			AddComponentSource(Participant, Component, Sources);
		}
	}

	if (Blueprint && Blueprint->SimpleConstructionScript)
	{
		// SCS variable names are the stable identity; template UObject names can change during Blueprint compilation.
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			AddComponentSource(Participant, Node ? Node->ComponentTemplate : nullptr, Sources, Node ? Node->GetVariableName() : NAME_None);
		}
	}

	Sources.Sort([](const FWorldStatePropertyPickerSource& Left, const FWorldStatePropertyPickerSource& Right)
	{
		return Left.Id.Kind != Right.Id.Kind
			? Left.Id.Kind == EWorldStateCaptureSourceKind::OwnerActor
			: Left.Id.ToString() < Right.Id.ToString();
	});
	return Sources;
}

TArray<FWorldStatePropertyPickerCandidate> FWorldStatePropertyPickerModel::BuildCandidates(
	const UWorldStateParticipantComponent& Participant,
	const FWorldStatePropertyPickerSource& Source,
	bool bIncludeUnsupported)
{
	TArray<FWorldStatePropertyPickerCandidate> Candidates;
	UObject* SourceObject = Source.Object.Get();
	if (!SourceObject)
	{
		return Candidates;
	}
	for (TFieldIterator<FProperty> It(SourceObject->GetClass()); It; ++It)
	{
		// TFieldIterator intentionally includes inherited properties so Blueprint and native sources behave alike.
		FProperty* Property = *It;
		FWorldStatePropertyPickerCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.SourceId = Source.Id;
		Candidate.Source = SourceObject;
		Candidate.PropertyName = Property->GetFName();
		Candidate.DisplayType = Property->GetCPPType();
		Candidate.Validation = FWorldStatePropertySerializer::Validate(Property);
		Candidate.bAlreadySelected = Participant.CapturedProperties.ContainsByPredicate([&Source, Property](const FWorldStatePropertySelection& Selection)
		{
			return Selection.CaptureSourceId == Source.Id && Selection.PropertyName == Property->GetFName();
		});
		if (!bIncludeUnsupported && !Candidate.Validation.IsValid())
		{
			Candidates.Pop(EAllowShrinking::No);
		}
	}
	Candidates.Sort([](const FWorldStatePropertyPickerCandidate& Left, const FWorldStatePropertyPickerCandidate& Right)
	{
		return Left.PropertyName.LexicalLess(Right.PropertyName);
	});
	return Candidates;
}

FWorldStatePropertyPickerCandidate FWorldStatePropertyPickerModel::DescribeSelection(
	const UWorldStateParticipantComponent& Participant,
	const FWorldStatePropertySelection& Selection)
{
	FWorldStatePropertyPickerCandidate Candidate;
	Candidate.SourceId = Selection.CaptureSourceId;
	Candidate.PropertyName = Selection.PropertyName;
	Candidate.bAlreadySelected = true;
	for (const FWorldStatePropertyPickerSource& Source : BuildSources(Participant))
	{
		if (Source.Id == Selection.CaptureSourceId)
		{
			Candidate.Source = Source.Object;
			break;
		}
	}
	// Describe from the persisted identity first: missing selections remain actionable instead of disappearing.
	UObject* SourceObject = Candidate.Source.Get();
	FProperty* Property = SourceObject ? FindFProperty<FProperty>(SourceObject->GetClass(), Selection.PropertyName) : nullptr;
	Candidate.DisplayType = Property ? Property->GetCPPType() : FString();
	Candidate.Validation = FWorldStatePropertySerializer::Validate(Property);
	if (!SourceObject)
	{
		Candidate.Validation.Status = EWorldStatePropertyValidationStatus::MissingSource;
		Candidate.Validation.Message = TEXT("The authored capture source is missing.");
	}
	else if (Candidate.Validation.IsValid() && !Selection.ExpectedTypeSignature.IsEmpty() && Candidate.Validation.TypeSignature != Selection.ExpectedTypeSignature)
	{
		Candidate.Validation.Status = EWorldStatePropertyValidationStatus::TypeSignatureMismatch;
		Candidate.Validation.Message = TEXT("The reflected type signature changed.");
	}
	return Candidate;
}

bool FWorldStatePropertyPickerModel::CanEditUniqueSources(TConstArrayView<TWeakObjectPtr<UObject>> CustomizedObjects)
{
	return CustomizedObjects.Num() == 1 && CustomizedObjects[0].IsValid();
}
