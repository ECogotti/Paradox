#include "Inventory/ParadoxPuzzleItemSlotActor.h"

#include "Emitters/PuzzleEmitterComponent.h"
#include "Inventory/ParadoxInsertablePickupableActor.h"
#include "Paradox.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "TimerManager.h"

AParadoxPuzzleItemSlotActor::AParadoxPuzzleItemSlotActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PuzzleEmitter = CreateDefaultSubobject<UPuzzleEmitterComponent>(TEXT("PuzzleEmitter"));
	PuzzleReceiver = CreateDefaultSubobject<UPuzzleReceiverComponent>(TEXT("PuzzleReceiver"));
	OutputSignalTag = ParadoxGameplayTags::Puzzle_Signal_ItemSlotSatisfied;
}

void AParadoxPuzzleItemSlotActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ConfigurePuzzleRole();
}

void AParadoxPuzzleItemSlotActor::BeginPlay()
{
	ConfigurePuzzleRole();
	Super::BeginPlay();
	if (PuzzleReceiver)
	{
		PuzzleReceiver->OnReceiverStateChangedNative.AddUObject(
			this, &ThisClass::HandleReceiverStateChanged);
		PuzzleReceiver->OnReceiverActivationPrerequisitesChangedNative.AddUObject(
			this, &ThisClass::HandleReceiverActivationPrerequisitesChanged);
	}
	NotifySlotActiveStateMayHaveChanged();
	RefreshPuzzleOutput();
	RefreshReceiverActivationPermission();
}

void AParadoxPuzzleItemSlotActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bReceiverPermissionRefreshQueued = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReceiverPermissionRefreshTimer);
	}
	if (PuzzleReceiver)
	{
		PuzzleReceiver->OnReceiverStateChangedNative.RemoveAll(this);
		PuzzleReceiver->OnReceiverActivationPrerequisitesChangedNative.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

bool AParadoxPuzzleItemSlotActor::UsesPuzzleEmitter() const
{
	return PuzzleRole == EParadoxPuzzleItemSlotRole::Emitter
		|| PuzzleRole == EParadoxPuzzleItemSlotRole::EmitterAndReceiver;
}

bool AParadoxPuzzleItemSlotActor::UsesPuzzleReceiver() const
{
	return PuzzleRole == EParadoxPuzzleItemSlotRole::Receiver
		|| PuzzleRole == EParadoxPuzzleItemSlotRole::EmitterAndReceiver;
}

bool AParadoxPuzzleItemSlotActor::IsRightItemInserted() const
{
	const AParadoxInsertablePickupableActor* Item = GetInsertedItem();
	if (!IsValid(Item))
	{
		return false;
	}
	return RightItemTags.IsEmpty()
		|| Item->GetInsertableTraits().HasAnyExact(RightItemTags);
}

bool AParadoxPuzzleItemSlotActor::IsReceiverActivationPermittedByInsertedItem() const
{
	return UsesPuzzleReceiver() && IsRightItemInserted();
}

bool AParadoxPuzzleItemSlotActor::EvaluateRequiredSlotActive() const
{
	return Super::EvaluateRequiredSlotActive()
		&& (!bRequirePuzzleReceiverForActivation
			|| (PuzzleReceiver && PuzzleReceiver->IsReceiverActive()));
}

bool AParadoxPuzzleItemSlotActor::EvaluatePuzzleOutput_Implementation() const
{
	return UsesPuzzleEmitter() && IsSlotActive() && IsRightItemInserted();
}

void AParadoxPuzzleItemSlotActor::NotifyPuzzleRelevantItemStateChanged()
{
	RefreshPuzzleOutput();
	RefreshReceiverActivationPermission();
}

void AParadoxPuzzleItemSlotActor::RefreshPuzzleOutput()
{
	if (!UsesPuzzleEmitter())
	{
		return;
	}
	if (!PuzzleEmitter)
	{
		PARADOX_LOG_WARNING(
			TEXT("Puzzle Item Slot '%s' cannot publish because it has no Puzzle Emitter."),
			*GetNameSafe(this));
		return;
	}
	if (!OutputSignalTag.IsValid())
	{
		PARADOX_LOG_WARNING(
			TEXT("Puzzle Item Slot '%s' cannot publish because its output signal tag is invalid."),
			*GetNameSafe(this));
		return;
	}
	const bool bDesiredOutput = EvaluatePuzzleOutput();
	if (!PuzzleEmitter->SetSignalState(OutputSignalTag, bDesiredOutput, nullptr))
	{
		FPuzzleSignalState ExistingState;
		if (!PuzzleEmitter->TryGetSignalState(OutputSignalTag, ExistingState)
			|| !ExistingState.bIsValid
			|| ExistingState.bIsActive != bDesiredOutput)
		{
			PARADOX_LOG_ERROR(
				TEXT("Puzzle Item Slot '%s' failed to publish signal '%s'."),
				*GetNameSafe(this),
				*OutputSignalTag.ToString());
		}
	}
}

void AParadoxPuzzleItemSlotActor::HandleAuthoritativeSlotStateChanged()
{
	Super::HandleAuthoritativeSlotStateChanged();
	RefreshPuzzleOutput();
	RefreshReceiverActivationPermission();
}

void AParadoxPuzzleItemSlotActor::ConfigurePuzzleRole()
{
	if (!PuzzleReceiver)
	{
		return;
	}

	PuzzleReceiver->ActivationMode = UsesPuzzleReceiver()
		? EPuzzleReceiverActivationMode::Manual
		: EPuzzleReceiverActivationMode::Automatic;
}

void AParadoxPuzzleItemSlotActor::RefreshReceiverActivationPermission()
{
	if (!PuzzleReceiver || !UsesPuzzleReceiver())
	{
		return;
	}
	if (PuzzleReceiver->GetActivationMode() != EPuzzleReceiverActivationMode::Manual)
	{
		PARADOX_LOG_ERROR(
			TEXT("Puzzle Item Slot '%s' cannot apply its Receiver item permission because Receiver '%s' is not in Manual activation mode."),
			*GetNameSafe(this),
			*GetNameSafe(PuzzleReceiver.Get()));
		return;
	}

	const bool bPermissionGranted = IsReceiverActivationPermittedByInsertedItem();
	if (bPermissionGranted)
	{
		if (!PuzzleReceiver->AreActivationPrerequisitesSatisfied()
			|| PuzzleReceiver->IsManualActivationRequested())
		{
			return;
		}

		const FPuzzleReceiverActivationCommandResult Result =
			PuzzleReceiver->RequestManualActivation();
		if (!Result.WasAccepted())
		{
			if (Result.Status == EPuzzleReceiverActivationCommandStatus::SupersededDuringNotification)
			{
				QueueReceiverActivationPermissionRefresh();
				return;
			}
			PARADOX_LOG_WARNING(
				TEXT("Puzzle Item Slot '%s' could not grant Receiver activation permission: %s"),
				*GetNameSafe(this),
				*Result.DiagnosticMessage);
		}
		return;
	}

	if (!PuzzleReceiver->IsManualActivationRequested()
		&& !PuzzleReceiver->IsReceiverActive())
	{
		return;
	}

	const FPuzzleReceiverActivationCommandResult Result =
		PuzzleReceiver->RequestManualDeactivation();
	if (!Result.WasAccepted())
	{
		if (Result.Status == EPuzzleReceiverActivationCommandStatus::SupersededDuringNotification)
		{
			QueueReceiverActivationPermissionRefresh();
			return;
		}
		PARADOX_LOG_WARNING(
			TEXT("Puzzle Item Slot '%s' could not revoke Receiver activation permission: %s"),
			*GetNameSafe(this),
			*Result.DiagnosticMessage);
	}
}

void AParadoxPuzzleItemSlotActor::QueueReceiverActivationPermissionRefresh()
{
	if (bReceiverPermissionRefreshQueued || !GetWorld())
	{
		return;
	}
	bReceiverPermissionRefreshQueued = true;
	ReceiverPermissionRefreshTimer = GetWorldTimerManager().SetTimerForNextTick(
		this,
		&ThisClass::HandleQueuedReceiverActivationPermissionRefresh);
}

void AParadoxPuzzleItemSlotActor::HandleQueuedReceiverActivationPermissionRefresh()
{
	bReceiverPermissionRefreshQueued = false;
	ReceiverPermissionRefreshTimer.Invalidate();
	RefreshReceiverActivationPermission();
}

void AParadoxPuzzleItemSlotActor::HandleReceiverStateChanged(
	UPuzzleReceiverComponent* Receiver,
	const bool bReceiverActive)
{
	(void)bReceiverActive;
	if (Receiver == PuzzleReceiver)
	{
		NotifySlotActiveStateMayHaveChanged();
	}
}

void AParadoxPuzzleItemSlotActor::HandleReceiverActivationPrerequisitesChanged(
	UPuzzleReceiverComponent* Receiver,
	const bool bPrerequisitesSatisfied)
{
	(void)bPrerequisitesSatisfied;
	if (Receiver == PuzzleReceiver && UsesPuzzleReceiver())
	{
		// PuzzleSystem deliberately rejects Manual commands during its synchronous reconciliation.
		QueueReceiverActivationPermissionRefresh();
	}
}
