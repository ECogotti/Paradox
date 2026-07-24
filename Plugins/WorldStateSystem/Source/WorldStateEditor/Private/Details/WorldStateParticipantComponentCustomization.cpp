#include "Details/WorldStateParticipantComponentCustomization.h"
#include "Details/WorldStatePropertyPickerModel.h"

#include "Components/SceneComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "Serialization/WorldStatePropertySerializer.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WorldStateParticipantComponentCustomization"

TSharedRef<IDetailCustomization> FWorldStateParticipantComponentCustomization::MakeInstance()
{
	return MakeShared<FWorldStateParticipantComponentCustomization>();
}

void FWorldStateParticipantComponentCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	PropertyUtilities = DetailBuilder.GetPropertyUtilities();
	if (!FWorldStatePropertyPickerModel::CanEditUniqueSources(Objects))
	{
		// Multi-edit cannot safely map one source name to potentially different Actor/Component class graphs.
		DetailBuilder.EditCategory(TEXT("World State"))
			.AddCustomRow(LOCTEXT("MultiSelectionSearch", "Multiple World State participants"))
			.WholeRowContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MultiSelection", "World State capture selections are disabled for multi-object editing because capture sources may differ."))
				.AutoWrapText(true)
			];
		return;
	}
	Participant = Cast<UWorldStateParticipantComponent>(Objects[0].Get());
	if (!Participant.IsValid())
	{
		return;
	}

	// Raw arrays are hidden because all mutations must preserve signatures, uniqueness and Undo/Redo.
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UWorldStateParticipantComponent, ParticipantId));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UWorldStateParticipantComponent, CapturedProperties));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UWorldStateParticipantComponent, SceneComponentCaptureSelections));

	// Identity authoring keeps duplicate detection next to the transaction-aware repair command.
	IDetailCategoryBuilder& IdentityCategory = DetailBuilder.EditCategory(TEXT("World State|Identity"));
	IdentityCategory.AddCustomRow(LOCTEXT("ParticipantIdSearch", "Participant ID"))
		.NameContent()[SNew(STextBlock).Text(LOCTEXT("ParticipantId", "Participant ID"))]
		.ValueContent().MinDesiredWidth(360.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return FText::FromString(Participant.IsValid() ? Participant->ParticipantId.ToString() : FString()); })
				.ColorAndOpacity_Lambda([this]() { return HasDuplicateId() ? FLinearColor(1.0f, 0.2f, 0.1f) : FSlateColor::UseForeground(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("RegenerateId", "Regenerate"))
				.ToolTipText(LOCTEXT("RegenerateIdTooltip", "Assign a new transaction-aware per-instance Participant ID."))
				.OnClicked(this, &FWorldStateParticipantComponentCustomization::RegenerateId)
			]
		];
	IdentityCategory.AddCustomRow(LOCTEXT("DuplicateIdSearch", "Duplicate Participant ID"))
		.Visibility(TAttribute<EVisibility>::CreateLambda([this]() { return HasDuplicateId() ? EVisibility::Visible : EVisibility::Collapsed; }))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DuplicateId", "Duplicate Participant ID detected in this world. Regenerate it before capture."))
			.ColorAndOpacity(FLinearColor(1.0f, 0.2f, 0.1f))
			.AutoWrapText(true)
		];

	// Structural authoring exposes stable authored Components, never runtime-created instance Components.
	IDetailCategoryBuilder& StructuralCategory = DetailBuilder.EditCategory(TEXT("World State|Structural State"));
	StructuralCategory.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWorldStateParticipantComponent, bCaptureExistence)));
	StructuralCategory.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWorldStateParticipantComponent, bCaptureActorTransform)));
	StructuralCategory.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWorldStateParticipantComponent, bCaptureAttachment)));
	for (const FWorldStatePropertyPickerSource& Source : BuildCaptureSources())
	{
		USceneComponent* SceneComponent = Cast<USceneComponent>(Source.Object.Get());
		if (!SceneComponent)
		{
			continue;
		}
		// Actor transform is authoritative for the root; restoring both would create contradictory writes.
		const bool bRootConflict = Participant->bCaptureActorTransform && Participant->GetOwner() && SceneComponent == Participant->GetOwner()->GetRootComponent();
		StructuralCategory.AddCustomRow(Source.Label)
			.NameContent()[SNew(STextBlock).Text(Source.Label)]
			.ValueContent()
			[
				SNew(SCheckBox)
				.IsEnabled(!bRootConflict)
				.IsChecked(this, &FWorldStateParticipantComponentCustomization::IsSceneComponentSelected, Source.Id)
				.OnCheckStateChanged(this, &FWorldStateParticipantComponentCustomization::SetSceneComponentSelected, Source.Id)
				.ToolTipText(bRootConflict
					? LOCTEXT("RootConflict", "Actor transform capture is authoritative for the root component.")
					: LOCTEXT("SceneCapture", "Capture this component's complete relative transform."))
			];
	}
	StructuralCategory.AddCustomRow(LOCTEXT("AttachmentCaptureWarningSearch", "Attachment capture warning"))
		.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
		{
			return Participant.IsValid() && !Participant->bCaptureAttachment && !Participant->SceneComponentCaptureSelections.IsEmpty()
				? EVisibility::Visible
				: EVisibility::Collapsed;
		}))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AttachmentCaptureWarning", "Attachment capture is disabled. If a selected Scene Component's parent changes, restore warns and applies its relative transform against the current parent."))
			.ColorAndOpacity(FLinearColor(1.0f, 0.65f, 0.1f))
			.AutoWrapText(true)
		];

	// Invalid persisted selections stay visible so users can diagnose or remove them transactionally.
	IDetailCategoryBuilder& PropertiesCategory = DetailBuilder.EditCategory(TEXT("World State|Captured Properties"));
	for (int32 Index = 0; Index < Participant->CapturedProperties.Num(); ++Index)
	{
		PropertiesCategory.AddCustomRow(LOCTEXT("CapturedPropertySearch", "Captured Property"))
			.WholeRowContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(this, &FWorldStateParticipantComponentCustomization::DescribePropertySelection, Index)
					.ColorAndOpacity(this, &FWorldStateParticipantComponentCustomization::GetPropertySelectionColor, Index)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("RemoveProperty", "Remove"))
					.OnClicked(this, &FWorldStateParticipantComponentCustomization::RemovePropertySelection, Index)
				]
			];
	}
	PropertiesCategory.AddCustomRow(LOCTEXT("AddPropertySearch", "Add Property"))
		.WholeRowContent()
		[
			SNew(SComboButton)
			.ButtonContent()[SNew(STextBlock).Text(LOCTEXT("AddProperty", "Add Property"))]
			.OnGetMenuContent(this, &FWorldStateParticipantComponentCustomization::BuildPropertyMenu)
		];
}

TArray<FWorldStatePropertyPickerSource> FWorldStateParticipantComponentCustomization::BuildCaptureSources() const
{
	return Participant.IsValid()
		? FWorldStatePropertyPickerModel::BuildSources(*Participant)
		: TArray<FWorldStatePropertyPickerSource>();
}

TSharedRef<SWidget> FWorldStateParticipantComponentCustomization::BuildPropertyMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);
	if (!Participant.IsValid())
	{
		return MenuBuilder.MakeWidget();
	}
	for (const FWorldStatePropertyPickerSource& Source : BuildCaptureSources())
	{
		UObject* SourceObject = Source.Object.Get();
		if (!SourceObject)
		{
			continue;
		}
		// Source-grouped sections make the persisted identity explicit when property names repeat.
		MenuBuilder.BeginSection(FName(*Source.Id.ToString()), Source.Label);
		for (const FWorldStatePropertyPickerCandidate& Candidate : FWorldStatePropertyPickerModel::BuildCandidates(*Participant, Source, false))
		{
			if (Candidate.bAlreadySelected)
			{
				continue;
			}
			const FText Label = FText::Format(LOCTEXT("PropertyLabel", "{0} ({1})"), FText::FromName(Candidate.PropertyName), FText::FromString(Candidate.DisplayType));
			MenuBuilder.AddMenuEntry(
				Label,
				FText::FromString(Candidate.Validation.TypeSignature),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(
					this,
					&FWorldStateParticipantComponentCustomization::AddPropertySelection,
					Source.Id,
					Candidate.PropertyName,
					FSoftClassPath(SourceObject->GetClass()),
					Candidate.Validation.TypeSignature)));
		}
		MenuBuilder.EndSection();
	}
	return MenuBuilder.MakeWidget();
}

FText FWorldStateParticipantComponentCustomization::DescribePropertySelection(int32 SelectionIndex) const
{
	if (!Participant.IsValid() || !Participant->CapturedProperties.IsValidIndex(SelectionIndex))
	{
		return FText::GetEmpty();
	}
	const FWorldStatePropertySelection& Selection = Participant->CapturedProperties[SelectionIndex];
	const FWorldStatePropertyPickerCandidate Candidate = FWorldStatePropertyPickerModel::DescribeSelection(*Participant, Selection);
	if (!Candidate.Validation.IsValid())
	{
		return FText::FromString(FString::Printf(TEXT("Missing or incompatible: %s.%s"), *Selection.CaptureSourceId.ToString(), *Selection.PropertyName.ToString()));
	}
	return FText::FromString(FString::Printf(TEXT("%s.%s  (%s)"), *Selection.CaptureSourceId.ToString(), *Selection.PropertyName.ToString(), *Candidate.DisplayType));
}

FSlateColor FWorldStateParticipantComponentCustomization::GetPropertySelectionColor(int32 SelectionIndex) const
{
	return DescribePropertySelection(SelectionIndex).ToString().StartsWith(TEXT("Missing"))
		? FSlateColor(FLinearColor(1.0f, 0.2f, 0.1f))
		: FSlateColor::UseForeground();
}

FReply FWorldStateParticipantComponentCustomization::RemovePropertySelection(int32 SelectionIndex)
{
	if (Participant.IsValid() && Participant->CapturedProperties.IsValidIndex(SelectionIndex))
	{
		// Modify plus PostEditChange provides both transaction capture and Blueprint/default propagation.
		const FScopedTransaction Transaction(LOCTEXT("RemovePropertyTransaction", "Remove World State Property"));
		Participant->Modify();
		Participant->CapturedProperties.RemoveAt(SelectionIndex);
		Participant->PostEditChange();
		Refresh();
	}
	return FReply::Handled();
}

void FWorldStateParticipantComponentCustomization::AddPropertySelection(
	FWorldStateCaptureSourceId SourceId,
	FName PropertyName,
	FSoftClassPath SourceClass,
	FString TypeSignature)
{
	if (!Participant.IsValid())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("AddPropertyTransaction", "Add World State Property"));
	Participant->Modify();
	FWorldStatePropertySelection& Selection = Participant->CapturedProperties.AddDefaulted_GetRef();
	Selection.CaptureSourceId = SourceId;
	Selection.PropertyName = PropertyName;
	// Persist compatibility metadata at authoring time so runtime preflight never guesses from payload bytes.
	Selection.ExpectedSourceClass = MoveTemp(SourceClass);
	Selection.ExpectedTypeSignature = MoveTemp(TypeSignature);
	Participant->PostEditChange();
	Refresh();
}

ECheckBoxState FWorldStateParticipantComponentCustomization::IsSceneComponentSelected(FWorldStateCaptureSourceId SourceId) const
{
	if (Participant.IsValid() && Participant->SceneComponentCaptureSelections.ContainsByPredicate([&SourceId](const FWorldStateSceneComponentCaptureSelection& Selection)
	{
		return Selection.CaptureSourceId == SourceId && Selection.bEnabled && Selection.bCaptureRelativeTransform;
	}))
	{
		return ECheckBoxState::Checked;
	}
	return ECheckBoxState::Unchecked;
}

void FWorldStateParticipantComponentCustomization::SetSceneComponentSelected(ECheckBoxState NewState, FWorldStateCaptureSourceId SourceId)
{
	if (!Participant.IsValid())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("SceneSelectionTransaction", "Change World State Scene Component Capture"));
	Participant->Modify();
	const int32 ExistingIndex = Participant->SceneComponentCaptureSelections.IndexOfByPredicate([&SourceId](const FWorldStateSceneComponentCaptureSelection& Selection)
	{
		return Selection.CaptureSourceId == SourceId;
	});
	if (NewState == ECheckBoxState::Checked)
	{
		// Re-enable an existing authored row when possible to retain its strict-parent policy.
		if (ExistingIndex == INDEX_NONE)
		{
			FWorldStateSceneComponentCaptureSelection& Selection = Participant->SceneComponentCaptureSelections.AddDefaulted_GetRef();
			Selection.CaptureSourceId = SourceId;
		}
		else
		{
			Participant->SceneComponentCaptureSelections[ExistingIndex].bEnabled = true;
			Participant->SceneComponentCaptureSelections[ExistingIndex].bCaptureRelativeTransform = true;
		}
	}
	else if (ExistingIndex != INDEX_NONE)
	{
		Participant->SceneComponentCaptureSelections.RemoveAt(ExistingIndex);
	}
	Participant->PostEditChange();
	Refresh();
}

FReply FWorldStateParticipantComponentCustomization::RegenerateId()
{
	if (Participant.IsValid())
	{
		const FScopedTransaction Transaction(LOCTEXT("RegenerateIdTransaction", "Regenerate World State Participant ID"));
		Participant->Modify();
		Participant->RegenerateParticipantId();
		Participant->PostEditChange();
		Refresh();
	}
	return FReply::Handled();
}

bool FWorldStateParticipantComponentCustomization::HasDuplicateId() const
{
	if (!Participant.IsValid() || !Participant->ParticipantId.IsValid() || !Participant->GetWorld())
	{
		return false;
	}
	// Editor worlds do not necessarily run the runtime registry, so duplicate detection scans live Actors directly.
	for (TActorIterator<AActor> It(Participant->GetWorld()); It; ++It)
	{
		if (UWorldStateParticipantComponent* Other = It->FindComponentByClass<UWorldStateParticipantComponent>(); Other && Other != Participant.Get() && Other->ParticipantId == Participant->ParticipantId)
		{
			return true;
		}
	}
	return false;
}

void FWorldStateParticipantComponentCustomization::Refresh() const
{
	if (PropertyUtilities)
	{
		PropertyUtilities->ForceRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
