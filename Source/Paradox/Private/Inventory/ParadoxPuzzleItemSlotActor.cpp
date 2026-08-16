#include "Inventory/ParadoxPuzzleItemSlotActor.h"

#include "Emitters/PuzzleEmitterComponent.h"
#include "Paradox.h"
#include "Receivers/PuzzleReceiverComponent.h"

AParadoxPuzzleItemSlotActor::AParadoxPuzzleItemSlotActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PuzzleEmitter = CreateDefaultSubobject<UPuzzleEmitterComponent>(TEXT("PuzzleEmitter"));
	PuzzleReceiver = CreateDefaultSubobject<UPuzzleReceiverComponent>(TEXT("PuzzleReceiver"));
	OutputSignalTag = ParadoxGameplayTags::Puzzle_Signal_ItemSlotSatisfied;
}

void AParadoxPuzzleItemSlotActor::BeginPlay()
{
	Super::BeginPlay();
	if (PuzzleReceiver)
	{
		PuzzleReceiver->OnReceiverStateChangedNative.AddUObject(
			this, &ThisClass::HandleReceiverStateChanged);
	}
	NotifySlotActiveStateMayHaveChanged();
	RefreshPuzzleOutput();
}

void AParadoxPuzzleItemSlotActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PuzzleReceiver)
	{
		PuzzleReceiver->OnReceiverStateChangedNative.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

bool AParadoxPuzzleItemSlotActor::EvaluateRequiredSlotActive() const
{
	return Super::EvaluateRequiredSlotActive()
		&& (!bRequirePuzzleReceiverForActivation
			|| (PuzzleReceiver && PuzzleReceiver->IsReceiverActive()));
}

bool AParadoxPuzzleItemSlotActor::EvaluatePuzzleOutput_Implementation() const
{
	return IsSlotActive() && IsOccupied();
}

void AParadoxPuzzleItemSlotActor::NotifyPuzzleRelevantItemStateChanged()
{
	RefreshPuzzleOutput();
}

void AParadoxPuzzleItemSlotActor::RefreshPuzzleOutput()
{
	if (!PuzzleEmitter || !OutputSignalTag.IsValid())
	{
		PARADOX_LOG_WARNING(
			TEXT("Puzzle Item Slot '%s' cannot publish: emitter=%s signal=%s."),
			*GetNameSafe(this),
			*GetNameSafe(PuzzleEmitter.Get()),
			*OutputSignalTag.ToString());
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
