#include "Components/WorldStateParticipantComponent.h"

#include "GameFramework/Actor.h"
#include "Serialization/WorldStatePropertySerializer.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "UObject/UnrealType.h"

namespace
{
	/** Builds a fully contextual participant diagnostic without exposing subsystem internals. */
	FWorldStateIssue MakeValidationIssue(
		EWorldStateIssueSeverity Severity,
		FName Code,
		FString Message,
		const UWorldStateParticipantComponent* Participant,
		const FWorldStateCaptureSourceId& Source = FWorldStateCaptureSourceId(),
		FName PropertyName = NAME_None)
	{
		FWorldStateIssue Issue;
		Issue.Severity = Severity;
		Issue.Code = Code;
		Issue.Message = MoveTemp(Message);
		Issue.ParticipantId = Participant ? Participant->ParticipantId : FWorldStateParticipantId();
		Issue.CaptureSourceId = Source;
		Issue.PropertyName = PropertyName;
		return Issue;
	}
}

UWorldStateParticipantComponent::UWorldStateParticipantComponent()
{
	// Registration and debugging are event-driven; the component never needs per-frame work.
	PrimaryComponentTick.bCanEverTick = false;
}

void UWorldStateParticipantComponent::EnsureStableId(bool bForceNewId)
{
	if (IsTemplate())
	{
		// CDO/template identity would be copied to every instance and therefore must remain invalid.
		ParticipantId.Reset();
		return;
	}

	if (bForceNewId || !ParticipantId.IsValid())
	{
		ParticipantId = FWorldStateParticipantId::NewId();
	}
}

void UWorldStateParticipantComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UWorld* World = GetWorld())
	{
		if (UWorldStateSubsystem* Subsystem = World->GetSubsystem<UWorldStateSubsystem>())
		{
			// Respawn identity is staged before deferred construction so registration observes the captured ID.
			if (!Subsystem->ClaimPendingRespawnIdentity(this))
			{
				EnsureStableId();
			}
			Subsystem->RegisterParticipant(this);
		}
	}
}

void UWorldStateParticipantComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UWorldStateSubsystem* Subsystem = World->GetSubsystem<UWorldStateSubsystem>())
		{
			Subsystem->UnregisterParticipant(this);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UWorldStateParticipantComponent::PostLoad()
{
	Super::PostLoad();
	EnsureStableId();
}

void UWorldStateParticipantComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
	EnsureStableId();
}

void UWorldStateParticipantComponent::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	// PIE duplicates model the same authored instance; ordinary duplication creates a new participant.
	EnsureStableId(DuplicateMode != EDuplicateMode::PIE);
}

#if WITH_EDITOR
void UWorldStateParticipantComponent::PostEditImport()
{
	Super::PostEditImport();
	EnsureStableId(true);
}
#endif

bool UWorldStateParticipantComponent::RegenerateParticipantId()
{
	// Re-keying a live registry entry would invalidate snapshots and dependency edges.
	if (UWorld* World = GetWorld())
	{
		if (UWorldStateSubsystem* Subsystem = World->GetSubsystem<UWorldStateSubsystem>(); Subsystem && Subsystem->IsParticipantRegistered(this))
		{
			return false;
		}
	}
	EnsureStableId(true);
	return ParticipantId.IsValid();
}

void UWorldStateParticipantComponent::MarkParticipantDirty()
{
	if (UWorld* World = GetWorld())
	{
		if (UWorldStateSubsystem* Subsystem = World->GetSubsystem<UWorldStateSubsystem>())
		{
			Subsystem->MarkParticipantDirty(ParticipantId);
		}
	}
}

UObject* UWorldStateParticipantComponent::ResolveCaptureSource(const FWorldStateCaptureSourceId& SourceId) const
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !SourceId.IsValid())
	{
		return nullptr;
	}
	if (SourceId.Kind == EWorldStateCaptureSourceKind::OwnerActor)
	{
		return OwnerActor;
	}

	// Stable UObject name, not Component array position or class, is the persisted source identity.
	TArray<UActorComponent*> Components;
	OwnerActor->GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (Component && Component != this && Component->GetFName() == SourceId.ComponentName)
		{
			return Component;
		}
	}
	return nullptr;
}

FWorldStateOperationResult UWorldStateParticipantComponent::ValidateCapturedProperties() const
{
	FWorldStateOperationResult Result;
	Result.Status = EWorldStateOperationStatus::Success;
	TSet<FString> SeenProperties;

	if (!ParticipantId.IsValid() && !IsTemplate())
	{
		Result.Issues.Add(MakeValidationIssue(EWorldStateIssueSeverity::Error, TEXT("InvalidParticipantId"), TEXT("Participant ID is invalid."), this));
	}

	// Validate authored identity before recursive reflected type support so diagnostics identify the earliest cause.
	for (const FWorldStatePropertySelection& Selection : CapturedProperties)
	{
		if (!Selection.bEnabled)
		{
			continue;
		}
		const FString SelectionKey = Selection.CaptureSourceId.ToString() + TEXT(".") + Selection.PropertyName.ToString();
		if (SeenProperties.Contains(SelectionKey))
		{
			Result.Issues.Add(MakeValidationIssue(EWorldStateIssueSeverity::Error, TEXT("DuplicateProperty"), TEXT("The same source property is selected more than once."), this, Selection.CaptureSourceId, Selection.PropertyName));
			continue;
		}
		SeenProperties.Add(SelectionKey);

		UObject* Source = ResolveCaptureSource(Selection.CaptureSourceId);
		if (!Source)
		{
			Result.Issues.Add(MakeValidationIssue(EWorldStateIssueSeverity::Error, TEXT("MissingCaptureSource"), TEXT("The selected capture source no longer exists."), this, Selection.CaptureSourceId, Selection.PropertyName));
			continue;
		}
		if (const UActorComponent* SourceComponent = Cast<UActorComponent>(Source); SourceComponent && SourceComponent->CreationMethod == EComponentCreationMethod::Instance)
		{
			// Runtime instance Components cannot be found again after Actor reconstruction without an external contract.
			Result.Issues.Add(MakeValidationIssue(EWorldStateIssueSeverity::Error, TEXT("UnstableRuntimeCaptureSource"), TEXT("Runtime instance Components require a stable reconstruction contract before they can be selected."), this, Selection.CaptureSourceId, Selection.PropertyName));
			continue;
		}
		if (!Selection.ExpectedSourceClass.IsNull() && Selection.ExpectedSourceClass.ToString() != Source->GetClass()->GetPathName())
		{
			Result.Issues.Add(MakeValidationIssue(EWorldStateIssueSeverity::Error, TEXT("SourceClassMismatch"), TEXT("The capture source class no longer matches the authored class."), this, Selection.CaptureSourceId, Selection.PropertyName));
			continue;
		}

		const FProperty* Property = FindFProperty<FProperty>(Source->GetClass(), Selection.PropertyName);
		FWorldStatePropertyValidationResult PropertyResult = FWorldStatePropertySerializer::Validate(Property);
		if (!PropertyResult.IsValid())
		{
			Result.Issues.Add(MakeValidationIssue(EWorldStateIssueSeverity::Error, TEXT("InvalidProperty"), PropertyResult.Message + TEXT(" Nested path: ") + PropertyResult.NestedFailurePath, this, Selection.CaptureSourceId, Selection.PropertyName));
			continue;
		}
		if (!Selection.ExpectedTypeSignature.IsEmpty() && Selection.ExpectedTypeSignature != PropertyResult.TypeSignature)
		{
			Result.Issues.Add(MakeValidationIssue(EWorldStateIssueSeverity::Error, TEXT("TypeSignatureMismatch"), TEXT("The selected property type changed after authoring."), this, Selection.CaptureSourceId, Selection.PropertyName));
		}
	}

	// Structural selections have their own uniqueness and transform-authority invariants.
	TSet<FWorldStateCaptureSourceId> SeenSceneComponents;
	for (const FWorldStateSceneComponentCaptureSelection& Selection : SceneComponentCaptureSelections)
	{
		if (!Selection.bEnabled || !Selection.bCaptureRelativeTransform)
		{
			continue;
		}
		if (SeenSceneComponents.Contains(Selection.CaptureSourceId))
		{
			Result.Issues.Add(MakeValidationIssue(EWorldStateIssueSeverity::Error, TEXT("DuplicateSceneComponent"), TEXT("The same Scene Component transform is selected more than once."), this, Selection.CaptureSourceId));
			continue;
		}
		SeenSceneComponents.Add(Selection.CaptureSourceId);
		const USceneComponent* SceneComponent = Cast<USceneComponent>(ResolveCaptureSource(Selection.CaptureSourceId));
		if (!SceneComponent)
		{
			Result.Issues.Add(MakeValidationIssue(EWorldStateIssueSeverity::Error, TEXT("MissingSceneComponent"), TEXT("The structural selection does not resolve to an owned Scene Component."), this, Selection.CaptureSourceId));
		}
		else if (SceneComponent->CreationMethod == EComponentCreationMethod::Instance)
		{
			Result.Issues.Add(MakeValidationIssue(EWorldStateIssueSeverity::Error, TEXT("UnstableRuntimeSceneComponent"), TEXT("Runtime instance Scene Components cannot be structurally selected without a stable reconstruction contract."), this, Selection.CaptureSourceId));
		}
		else if (bCaptureActorTransform && SceneComponent == GetOwner()->GetRootComponent())
		{
			Result.Issues.Add(MakeValidationIssue(EWorldStateIssueSeverity::Error, TEXT("CompetingTransformAuthority"), TEXT("The root Scene Component cannot capture a relative transform while Actor transform capture is enabled."), this, Selection.CaptureSourceId));
		}
	}

	if (Result.Issues.ContainsByPredicate([](const FWorldStateIssue& Issue) { return Issue.Severity == EWorldStateIssueSeverity::Error; }))
	{
		Result.Status = EWorldStateOperationStatus::RejectedInvalidRequest;
	}
	else if (!Result.Issues.IsEmpty())
	{
		Result.Status = EWorldStateOperationStatus::SuccessWithWarnings;
	}
	return Result;
}
