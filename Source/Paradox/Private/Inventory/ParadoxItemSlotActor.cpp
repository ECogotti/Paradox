#include "Inventory/ParadoxItemSlotActor.h"

#include "Actions/GameplayActionDefinition.h"
#include "Characters/ParadoxCharacter.h"
#include "Components/ArrowComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Components/SceneComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Interaction/ParadoxInteractionComponent.h"
#include "Interaction/ParadoxSelectableComponent.h"
#include "Inventory/ParadoxInsertablePickupableActor.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Paradox.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "Subsystems/WorldStateSubsystem.h"
#include "Types/PerceptionKnowledgeTypes.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ParadoxItemSlotActor"

namespace UE::Paradox::ItemSlot::Private
{
	struct FOperationGuard
	{
		explicit FOperationGuard(bool& InFlag) : Flag(InFlag) { Flag = true; }
		~FOperationGuard() { Flag = false; }
		bool& Flag;
	};
}

AParadoxItemSlotActor::AParadoxItemSlotActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	InsertAnchor = CreateDefaultSubobject<UArrowComponent>(TEXT("InsertAnchor"));
	InsertAnchor->SetupAttachment(SceneRoot);
	InsertAnchor->SetHiddenInGame(true);

	SelectableComponent = CreateDefaultSubobject<UParadoxSelectableComponent>(TEXT("SelectableComponent"));
	SelectableComponent->bShowInteractionCellsWhenSelected = true;
	SmartObjectComponent = CreateDefaultSubobject<USmartObjectComponent>(TEXT("SmartObjectComponent"));
	SmartObjectComponent->SetupAttachment(SceneRoot);
	InteractionComponent = CreateDefaultSubobject<UParadoxInteractionComponent>(TEXT("InteractionComponent"));
	WorldStateParticipant = CreateDefaultSubobject<UWorldStateParticipantComponent>(TEXT("WorldStateParticipant"));
	WorldStateParticipant->bCaptureExistence = true;
	WorldStateParticipant->bCaptureActorTransform = true;
	WorldStateParticipant->bCaptureAttachment = false;
	WorldStateParticipant->ExistencePolicy = EWorldStateExistencePolicy::RespawnAndDestroy;
	WorldStateParticipant->RestorePhase = EWorldStateRestorePhase::Late;
	FWorldStatePropertySelection& ItemSelection =
		WorldStateParticipant->CapturedProperties.AddDefaulted_GetRef();
	ItemSelection.CaptureSourceId = FWorldStateCaptureSourceId::OwnerActor();
	ItemSelection.PropertyName = GET_MEMBER_NAME_CHECKED(
		AParadoxItemSlotActor,
		WorldStateInsertedItem);
	ItemSelection.ReferenceRequirement = EWorldStateReferenceRequirement::Optional;

	PerceptionSource = CreateDefaultSubobject<UPerceptionKnowledgeSourceComponent>(TEXT("PerceptionSource"));

	static ConstructorHelpers::FObjectFinder<USmartObjectDefinition> SmartObjectDefinitionFinder(
		TEXT("/Game/Data/Inventory/DA_ParadoxItemSlotSmartObject.DA_ParadoxItemSlotSmartObject"));
	if (SmartObjectDefinitionFinder.Succeeded())
	{
		SmartObjectComponent->SetDefinition(SmartObjectDefinitionFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UGameplayActionDefinition> InsertDefinitionFinder(
		TEXT("/Game/Data/GameplayActions/DA_ParadoxInsertItem.DA_ParadoxInsertItem"));
	if (InsertDefinitionFinder.Succeeded())
	{
		FParadoxInteractionDefinition& Definition =
			InteractionComponent->InteractionDefinitions.AddDefaulted_GetRef();
		Definition.InteractionTag = ParadoxGameplayTags::Interaction_ItemSlot_Insert;
		Definition.GameplayActionDefinition = InsertDefinitionFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UGameplayActionDefinition> PickupDefinitionFinder(
		TEXT("/Game/Data/GameplayActions/DA_ParadoxPickupFromItemSlot.DA_ParadoxPickupFromItemSlot"));
	if (PickupDefinitionFinder.Succeeded())
	{
		FParadoxInteractionDefinition& Definition =
			InteractionComponent->InteractionDefinitions.AddDefaulted_GetRef();
		Definition.InteractionTag = ParadoxGameplayTags::Interaction_ItemSlot_Pickup;
		Definition.GameplayActionDefinition = PickupDefinitionFinder.Object;
	}
}

void AParadoxItemSlotActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	PrimaryActorTick.SetTickFunctionEnable(false);
}

#if WITH_EDITOR
void AParadoxItemSlotActor::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName ChangedProperty = PropertyChangedEvent.GetPropertyName();
	if ((ChangedProperty == GET_MEMBER_NAME_CHECKED(ThisClass, InsertedItem)
			|| ChangedProperty == GET_MEMBER_NAME_CHECKED(ThisClass, AcceptedItemQuery))
		&& IsValid(InsertedItem.Get())
		&& !MatchesAllowedItemQuery(InsertedItem.Get()))
	{
		PARADOX_LOG_WARNING(
			TEXT("Item Slot '%s' initially inserted item '%s' does not match Allowed Item Query and will be rejected at runtime."),
			*GetNameSafe(this),
			*GetNameSafe(InsertedItem.Get()));
	}
}

EDataValidationResult AParadoxItemSlotActor::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}

	const AParadoxInsertablePickupableActor* Item = InsertedItem.Get();
	if (!IsValid(Item))
	{
		return Result;
	}
	if (Item->GetWorld() != GetWorld())
	{
		Context.AddError(LOCTEXT(
			"InitialItemDifferentWorld",
			"Initially Inserted Item must be a placed insertable pickupable from the same World."));
		Result = EDataValidationResult::Invalid;
	}
	if (!MatchesAllowedItemQuery(Item))
	{
		Context.AddError(FText::Format(
			LOCTEXT(
				"InitialItemRejectedByQuery",
				"Initially Inserted Item '{0}' does not match Allowed Item Query."),
			FText::FromString(GetNameSafe(Item))));
		Result = EDataValidationResult::Invalid;
	}
	if (!InsertAnchor)
	{
		Context.AddError(LOCTEXT(
			"InitialItemMissingAnchor",
			"An authored Initially Inserted Item requires the native Insert Anchor."));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

void AParadoxItemSlotActor::BeginPlay()
{
	Super::BeginPlay();
	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreCapture.AddUniqueDynamic(
			this, &ThisClass::HandleWorldStatePreCapture);
		WorldStateParticipant->OnWorldStatePreRestore.AddUniqueDynamic(
			this, &ThisClass::HandleWorldStatePreRestore);
		WorldStateParticipant->OnWorldStatePropertiesRestored.AddUniqueDynamic(
			this, &ThisClass::HandleWorldStatePropertiesRestored);
		WorldStateParticipant->OnWorldStateRestored.AddUniqueDynamic(
			this, &ThisClass::HandleWorldStateParticipantRestored);
		WorldStateParticipant->OnWorldStateRestoreFailed.AddUniqueDynamic(
			this, &ThisClass::HandleWorldStateParticipantFailed);
	}
	if (UWorldStateSubsystem* WorldState = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>()
		: nullptr)
	{
		WorldState->OnRestoreStartedNative().AddUObject(
			this, &ThisClass::HandleWorldStateRestoreStarted);
		WorldState->OnRestoreCompletedNative().AddUObject(
			this, &ThisClass::HandleWorldStateRestoreFinished);
		WorldState->OnRestoreFailedNative().AddUObject(
			this, &ThisClass::HandleWorldStateRestoreFinished);
	}

	InitializeAuthoredInsertedItem();
	bCachedSlotActive = IsSlotActive();
	bInitialized = true;
	RefreshPerceptionState();
	RefreshInteractionAffordances();
	HandleAuthoritativeSlotStateChanged();
	LogDebugState(TEXT("BeginPlay"));
}

void AParadoxItemSlotActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bInitialized = false;
	if (WorldStateParticipant)
	{
		WorldStateParticipant->OnWorldStatePreCapture.RemoveDynamic(
			this, &ThisClass::HandleWorldStatePreCapture);
		WorldStateParticipant->OnWorldStatePreRestore.RemoveDynamic(
			this, &ThisClass::HandleWorldStatePreRestore);
		WorldStateParticipant->OnWorldStatePropertiesRestored.RemoveDynamic(
			this, &ThisClass::HandleWorldStatePropertiesRestored);
		WorldStateParticipant->OnWorldStateRestored.RemoveDynamic(
			this, &ThisClass::HandleWorldStateParticipantRestored);
		WorldStateParticipant->OnWorldStateRestoreFailed.RemoveDynamic(
			this, &ThisClass::HandleWorldStateParticipantFailed);
	}
	if (UWorldStateSubsystem* WorldState = GetWorld()
		? GetWorld()->GetSubsystem<UWorldStateSubsystem>()
		: nullptr)
	{
		WorldState->OnRestoreStartedNative().RemoveAll(this);
		WorldState->OnRestoreCompletedNative().RemoveAll(this);
		WorldState->OnRestoreFailedNative().RemoveAll(this);
	}

	if (AParadoxInsertablePickupableActor* Item = InsertedItem.Get())
	{
		UnbindInsertedItem(Item);
		InsertedItem = nullptr;
		Item->ClearInsertedStateNative(true);
		if (EndPlayReason != EEndPlayReason::EndPlayInEditor
			&& EndPlayReason != EEndPlayReason::Quit
			&& IsValid(Item))
		{
			Item->SetWorldStateNative(Item->GetActorTransform(), nullptr, false);
		}
	}
	Super::EndPlay(EndPlayReason);
}

bool AParadoxItemSlotActor::IsOccupied() const
{
	return IsValid(InsertedItem.Get());
}

bool AParadoxItemSlotActor::IsSlotActive() const
{
	return EvaluateRequiredSlotActive() && EvaluateAdditionalSlotActive();
}

bool AParadoxItemSlotActor::EvaluateRequiredSlotActive() const
{
	return true;
}

bool AParadoxItemSlotActor::EvaluateAdditionalSlotActive_Implementation() const
{
	return true;
}

bool AParadoxItemSlotActor::CanAcceptItemAdditional_Implementation(
	AParadoxInsertablePickupableActor* Item,
	AParadoxCharacter* Requester,
	FString& OutDiagnostic) const
{
	(void)Item;
	(void)Requester;
	OutDiagnostic.Reset();
	return true;
}

bool AParadoxItemSlotActor::CanAcceptItem(
	AParadoxInsertablePickupableActor* Item,
	AParadoxCharacter* Requester) const
{
	return EvaluateAcceptItem(Item, Requester).IsSuccess();
}

FParadoxItemSlotOperationResult AParadoxItemSlotActor::EvaluateAcceptItem(
	AParadoxInsertablePickupableActor* Item,
	AParadoxCharacter* Requester) const
{
	if (!IsValid(Requester))
	{
		return MakeResult(EParadoxItemSlotOperationStatus::InvalidRequester, TEXT("A valid Paradox Character requester is required."));
	}
	UParadoxInventoryComponent* Inventory = Requester->GetInventoryComponent();
	if (!IsValid(Inventory))
	{
		return MakeResult(EParadoxItemSlotOperationStatus::MissingInventory, TEXT("The requester does not own a valid inventory component."));
	}
	if (bResetInProgress || Inventory->bResetInProgress)
	{
		return MakeResult(EParadoxItemSlotOperationStatus::ResetInProgress, TEXT("Item Slot transitions are disabled during World State restore."));
	}
	if (bOperationInProgress || Inventory->bOperationInProgress)
	{
		return MakeResult(EParadoxItemSlotOperationStatus::OperationInProgress, TEXT("A reentrant Item Slot transition was rejected."));
	}
	if (!IsSlotActive())
	{
		return MakeResult(EParadoxItemSlotOperationStatus::SlotInactive, TEXT("The Item Slot is not operational."));
	}
	if (IsOccupied())
	{
		return MakeResult(EParadoxItemSlotOperationStatus::SlotOccupied, TEXT("The Item Slot is already occupied."));
	}
	if (!IsValid(Item))
	{
		return MakeResult(EParadoxItemSlotOperationStatus::InvalidItem, TEXT("A valid insertable pickupable is required."));
	}
	if (Inventory->GetEquippedItem() != Item || Item->GetCurrentHolder() != Requester)
	{
		return MakeResult(EParadoxItemSlotOperationStatus::RequesterDoesNotOwnItem, TEXT("The requester inventory does not authoritatively own this item."));
	}
	if (Item->GetCurrentItemSlot() || Item->GetPickupableState() != EParadoxPickupableState::Held)
	{
		return MakeResult(EParadoxItemSlotOperationStatus::OwnershipConflict, TEXT("The item already has incompatible ownership state."));
	}
	if (!InsertAnchor || !InsertAnchor->IsRegistered())
	{
		return MakeResult(EParadoxItemSlotOperationStatus::InvalidPlacement, TEXT("The Item Slot has no registered Insert Anchor."));
	}
	if (!MatchesAllowedItemQuery(Item))
	{
		return MakeResult(EParadoxItemSlotOperationStatus::IncompatibleTraits, TEXT("The item's Insertable Traits do not match the Slot query."));
	}
	FString AdditionalDiagnostic;
	if (!CanAcceptItemAdditional(Item, Requester, AdditionalDiagnostic))
	{
		return MakeResult(
			EParadoxItemSlotOperationStatus::AdditionalValidationFailed,
			AdditionalDiagnostic.IsEmpty()
				? TEXT("A derived Item Slot compatibility condition rejected the item.")
				: MoveTemp(AdditionalDiagnostic));
	}
	return MakeResult(EParadoxItemSlotOperationStatus::Succeeded, TEXT("The item can be inserted."));
}

FParadoxItemSlotOperationResult AParadoxItemSlotActor::EvaluatePickupInsertedItem(
	AParadoxCharacter* Requester) const
{
	if (!IsValid(Requester))
	{
		return MakeResult(EParadoxItemSlotOperationStatus::InvalidRequester, TEXT("A valid Paradox Character requester is required."));
	}
	UParadoxInventoryComponent* Inventory = Requester->GetInventoryComponent();
	if (!IsValid(Inventory))
	{
		return MakeResult(EParadoxItemSlotOperationStatus::MissingInventory, TEXT("The requester does not own a valid inventory component."));
	}
	if (bResetInProgress || Inventory->bResetInProgress)
	{
		return MakeResult(EParadoxItemSlotOperationStatus::ResetInProgress, TEXT("Item Slot transitions are disabled during World State restore."));
	}
	if (bOperationInProgress || Inventory->bOperationInProgress)
	{
		return MakeResult(EParadoxItemSlotOperationStatus::OperationInProgress, TEXT("A reentrant Item Slot transition was rejected."));
	}
	if (!IsSlotActive())
	{
		return MakeResult(EParadoxItemSlotOperationStatus::SlotInactive, TEXT("The Item Slot is not operational."));
	}
	AParadoxInsertablePickupableActor* Item = InsertedItem.Get();
	if (!IsValid(Item))
	{
		return MakeResult(EParadoxItemSlotOperationStatus::SlotEmpty, TEXT("The Item Slot is empty."));
	}
	if (bLockInsertedItem)
	{
		return MakeResult(EParadoxItemSlotOperationStatus::ItemLocked, TEXT("The inserted item is locked against ordinary Pickup."));
	}
	if (Inventory->HasItem())
	{
		return MakeResult(EParadoxItemSlotOperationStatus::InventoryOccupied, TEXT("Pickup requires an empty requester inventory."));
	}
	if (!Item->IsInserted() || Item->GetCurrentItemSlot() != this || Item->GetCurrentHolder())
	{
		return MakeResult(EParadoxItemSlotOperationStatus::OwnershipConflict, TEXT("The Item and Slot ownership references disagree."));
	}
	return MakeResult(EParadoxItemSlotOperationStatus::Succeeded, TEXT("The inserted item can be picked up."));
}

FParadoxItemSlotOperationResult AParadoxItemSlotActor::TryInsertItem(
	AParadoxCharacter* Requester)
{
	UParadoxInventoryComponent* Inventory = IsValid(Requester)
		? Requester->GetInventoryComponent()
		: nullptr;
	AParadoxInsertablePickupableActor* Item = Inventory
		? Cast<AParadoxInsertablePickupableActor>(Inventory->GetEquippedItem())
		: nullptr;
	if (Inventory && Inventory->HasItem() && !Item)
	{
		return MakeResult(EParadoxItemSlotOperationStatus::NotInsertable, TEXT("The equipped pickupable does not support Item Slot insertion."));
	}
	const FParadoxItemSlotOperationResult Validation = EvaluateAcceptItem(Item, Requester);
	if (!Validation.IsSuccess())
	{
		LogDebugState(TEXT("InsertRejected"), Validation.DiagnosticMessage);
		return Validation;
	}

	UE::Paradox::ItemSlot::Private::FOperationGuard Guard(bOperationInProgress);
	return Inventory->TransferEquippedItemToSlot(*this, *Item);
}

FParadoxItemSlotOperationResult AParadoxItemSlotActor::TryPickupInsertedItem(
	AParadoxCharacter* Requester)
{
	const FParadoxItemSlotOperationResult Validation = EvaluatePickupInsertedItem(Requester);
	if (!Validation.IsSuccess())
	{
		LogDebugState(TEXT("PickupRejected"), Validation.DiagnosticMessage);
		return Validation;
	}
	UParadoxInventoryComponent* Inventory = Requester->GetInventoryComponent();
	AParadoxInsertablePickupableActor* Item = InsertedItem.Get();
	UE::Paradox::ItemSlot::Private::FOperationGuard Guard(bOperationInProgress);
	return Inventory->TransferInsertedItemFromSlot(*this, *Item);
}

FParadoxItemSlotOperationResult AParadoxItemSlotActor::ReleaseInsertedItemToWorld(
	const FTransform& WorldTransform)
{
	if (bOperationInProgress)
	{
		return MakeResult(EParadoxItemSlotOperationStatus::OperationInProgress, TEXT("A reentrant Item Slot release was rejected."));
	}
	if (bResetInProgress)
	{
		return MakeResult(EParadoxItemSlotOperationStatus::ResetInProgress, TEXT("Use World State cleanup while restore is active."));
	}
	AParadoxInsertablePickupableActor* Item = InsertedItem.Get();
	if (!IsValid(Item))
	{
		return MakeResult(EParadoxItemSlotOperationStatus::SlotEmpty, TEXT("The Item Slot is already empty."));
	}
	if (!WorldTransform.IsValid())
	{
		return MakeResult(EParadoxItemSlotOperationStatus::InvalidPlacement, TEXT("The release transform is invalid."));
	}
	if (Item->GetCurrentItemSlot() != this)
	{
		return MakeResult(EParadoxItemSlotOperationStatus::OwnershipConflict, TEXT("The Item and Slot ownership references disagree."));
	}

	UE::Paradox::ItemSlot::Private::FOperationGuard Guard(bOperationInProgress);
	ClearInsertedItemCommitted(Item);
	Item->ClearInsertedStateNative(true);
	Item->SetWorldStateNative(WorldTransform, nullptr, false);
	Item->ReceiveRemovedFromSlot(this);
	FinalizeOccupancyTransition(Item, nullptr);
	return MakeResult(EParadoxItemSlotOperationStatus::Succeeded, TEXT("The inserted item was released into the world."));
}

void AParadoxItemSlotActor::NotifySlotActiveStateMayHaveChanged()
{
	const bool bNewActive = IsSlotActive();
	const bool bPreviousActive = bCachedSlotActive;
	bCachedSlotActive = bNewActive;
	RefreshInteractionAffordances();
	RefreshPerceptionState();
	HandleAuthoritativeSlotStateChanged();
	if (bInitialized && bPreviousActive != bNewActive)
	{
		OnSlotActiveStateChanged.Broadcast(this, bPreviousActive, bNewActive);
		ReceiveSlotActiveStateChanged(bPreviousActive, bNewActive);
		LogDebugState(TEXT("ActiveStateChanged"));
	}
}

void AParadoxItemSlotActor::NotifyInsertedItemRelevantStateChanged(
	AParadoxInsertablePickupableActor* ChangedItem)
{
	if (ChangedItem != InsertedItem.Get())
	{
		PARADOX_LOG_WARNING(
			TEXT("Item Slot '%s' ignored a relevant-state notification from non-owned item '%s'."),
			*GetNameSafe(this),
			*GetNameSafe(ChangedItem));
		return;
	}
	HandleAuthoritativeSlotStateChanged();
}

FParadoxItemSlotOperationResult AParadoxItemSlotActor::MakeResult(
	const EParadoxItemSlotOperationStatus Status,
	FString Diagnostic) const
{
	FParadoxItemSlotOperationResult Result;
	Result.Status = Status;
	Result.DiagnosticMessage = MoveTemp(Diagnostic);
	return Result;
}

bool AParadoxItemSlotActor::MatchesAllowedItemQuery(
	const AParadoxInsertablePickupableActor* Item) const
{
	return IsValid(Item)
		&& (AcceptedItemQuery.IsEmpty()
			|| AcceptedItemQuery.Matches(Item->GetInsertableTraits()));
}

void AParadoxItemSlotActor::InitializeAuthoredInsertedItem()
{
	AParadoxInsertablePickupableActor* Item = InsertedItem.Get();
	if (!IsValid(Item))
	{
		InsertedItem = nullptr;
		WorldStateInsertedItem.Reset();
		return;
	}
	if (Item->GetCurrentHolder() || Item->GetCurrentItemSlot()
		|| !MatchesAllowedItemQuery(Item)
		|| !InsertAnchor)
	{
		PARADOX_LOG_ERROR(
			TEXT("Item Slot '%s' rejected invalid authored baseline item '%s'."),
			*GetNameSafe(this),
			*GetNameSafe(Item));
		InsertedItem = nullptr;
		WorldStateInsertedItem.Reset();
		return;
	}
	BindInsertedItem(*Item);
	Item->SetInsertedStateNative(*this, *InsertAnchor);
	Item->ReceiveInsertedIntoSlot(this);
	WorldStateInsertedItem = Item;
}

void AParadoxItemSlotActor::SetInsertedItemCommitted(
	AParadoxInsertablePickupableActor* NewItem)
{
	InsertedItem = NewItem;
	if (NewItem)
	{
		BindInsertedItem(*NewItem);
	}
}

void AParadoxItemSlotActor::ClearInsertedItemCommitted(
	AParadoxInsertablePickupableActor* ExpectedItem)
{
	if (InsertedItem.Get() != ExpectedItem)
	{
		return;
	}
	UnbindInsertedItem(ExpectedItem);
	InsertedItem = nullptr;
}

void AParadoxItemSlotActor::FinalizeOccupancyTransition(
	AParadoxInsertablePickupableActor* PreviousItem,
	AParadoxInsertablePickupableActor* NewItem)
{
	RefreshInteractionAffordances();
	RefreshPerceptionState();
	HandleAuthoritativeSlotStateChanged();
	OnInsertedItemChanged.Broadcast(this, PreviousItem, NewItem);
	ReceiveInsertedItemChanged(PreviousItem, NewItem);
	LogDebugState(TEXT("OccupancyChanged"));
}

void AParadoxItemSlotActor::BindInsertedItem(AParadoxInsertablePickupableActor& Item)
{
	Item.OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleInsertedItemDestroyed);
}

void AParadoxItemSlotActor::UnbindInsertedItem(AParadoxInsertablePickupableActor* Item)
{
	if (Item)
	{
		Item->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleInsertedItemDestroyed);
	}
}

void AParadoxItemSlotActor::PrepareForWorldStateRestore()
{
	if (bResetInProgress)
	{
		return;
	}
	bResetInProgress = true;
	if (AParadoxInsertablePickupableActor* Item = InsertedItem.Get())
	{
		UnbindInsertedItem(Item);
		InsertedItem = nullptr;
		Item->ClearInsertedStateNative(true);
		Item->PrepareForWorldStateRestore();
	}
	LogDebugState(TEXT("RestoreStarted"));
}

void AParadoxItemSlotActor::RestoreCapturedRelationship()
{
	AParadoxInsertablePickupableActor* Item = WorldStateInsertedItem.Get();
	if (!IsValid(Item))
	{
		InsertedItem = nullptr;
		return;
	}
	if (Item->GetCurrentHolder()
		|| (Item->GetCurrentItemSlot() && Item->GetCurrentItemSlot() != this)
		|| !MatchesAllowedItemQuery(Item)
		|| !InsertAnchor)
	{
		PARADOX_LOG_ERROR(
			TEXT("Item Slot '%s' could not restore captured item '%s' because another owner is authoritative."),
			*GetNameSafe(this),
			*GetNameSafe(Item));
		InsertedItem = nullptr;
		return;
	}
	SetInsertedItemCommitted(Item);
	Item->SetInsertedStateNative(*this, *InsertAnchor);
}

void AParadoxItemSlotActor::FinishWorldStateRestore(const bool bSucceeded)
{
	bResetInProgress = false;
	bCachedSlotActive = IsSlotActive();
	RefreshInteractionAffordances();
	RefreshPerceptionState();
	HandleAuthoritativeSlotStateChanged();
	LogDebugState(bSucceeded ? TEXT("RestoreCompleted") : TEXT("RestoreFailed"));
}

void AParadoxItemSlotActor::RefreshPerceptionState()
{
	if (!PerceptionSource)
	{
		return;
	}
	PerceptionSource->SetObservableState(
		ParadoxGameplayTags::State_ItemSlot_Active,
		FPerceptionKnowledgeValue::MakeBool(IsSlotActive()));
	PerceptionSource->SetObservableState(
		ParadoxGameplayTags::State_ItemSlot_Occupied,
		FPerceptionKnowledgeValue::MakeBool(IsOccupied()));
	PerceptionSource->SetObservableState(
		ParadoxGameplayTags::State_ItemSlot_Locked,
		FPerceptionKnowledgeValue::MakeBool(IsOccupied() && bLockInsertedItem));
	PerceptionSource->SetObservableState(
		ParadoxGameplayTags::State_ItemSlot_Removable,
		FPerceptionKnowledgeValue::MakeBool(
			IsSlotActive() && IsOccupied() && !bLockInsertedItem));
}

void AParadoxItemSlotActor::RefreshInteractionAffordances()
{
	if (InteractionComponent)
	{
		InteractionComponent->NotifyInteractionAffordanceChanged();
	}
}

void AParadoxItemSlotActor::HandleAuthoritativeSlotStateChanged()
{
}

void AParadoxItemSlotActor::LogDebugState(
	const TCHAR* EventName,
	const FString& Diagnostic) const
{
	if (!bEnableDebug || !IsParadoxInventoryDebugEnabled())
	{
		return;
	}
	PARADOX_LOG_INFO(
		TEXT("ItemSlot event=%s slot=%s active=%d item=%s locked=%d query_empty=%d anchor=%s reset=%d diagnostic='%s'"),
		EventName,
		*GetNameSafe(this),
		IsSlotActive() ? 1 : 0,
		*GetNameSafe(InsertedItem.Get()),
		bLockInsertedItem ? 1 : 0,
		AcceptedItemQuery.IsEmpty() ? 1 : 0,
		*GetNameSafe(InsertAnchor.Get()),
		bResetInProgress ? 1 : 0,
		*Diagnostic);
	if (GetWorld() && InsertAnchor)
	{
		DrawDebugCoordinateSystem(
			GetWorld(),
			InsertAnchor->GetComponentLocation(),
			InsertAnchor->GetComponentRotation(),
			35.0f,
			false,
			1.5f,
			0,
			1.5f);
	}
}

void AParadoxItemSlotActor::HandleWorldStateRestoreStarted(
	const FWorldStateRestoreLifecycleContext& Context)
{
	(void)Context;
	PrepareForWorldStateRestore();
}

void AParadoxItemSlotActor::HandleWorldStateRestoreFinished(
	const FWorldStateRestoreResult& Result)
{
	FinishWorldStateRestore(Result.IsSuccess());
}

void AParadoxItemSlotActor::HandleWorldStatePreCapture(
	const FWorldStateParticipantId ParticipantId)
{
	(void)ParticipantId;
	WorldStateInsertedItem = InsertedItem.Get();
}

void AParadoxItemSlotActor::HandleWorldStatePreRestore(
	const FWorldStateParticipantId ParticipantId)
{
	(void)ParticipantId;
	PrepareForWorldStateRestore();
}

void AParadoxItemSlotActor::HandleWorldStatePropertiesRestored(
	const FWorldStateParticipantId ParticipantId)
{
	(void)ParticipantId;
	RestoreCapturedRelationship();
}

void AParadoxItemSlotActor::HandleWorldStateParticipantRestored(
	const FWorldStateParticipantId ParticipantId)
{
	(void)ParticipantId;
	RefreshPerceptionState();
	RefreshInteractionAffordances();
	HandleAuthoritativeSlotStateChanged();
}

void AParadoxItemSlotActor::HandleWorldStateParticipantFailed(
	const FWorldStateParticipantResult& Result)
{
	PARADOX_LOG_ERROR(
		TEXT("Item Slot '%s' World State participant restore failed (issues=%d)."),
		*GetNameSafe(this),
		Result.Issues.Num());
}

void AParadoxItemSlotActor::HandleInsertedItemDestroyed(AActor* DestroyedActor)
{
	HandleInsertedItemInvalidated(Cast<AParadoxInsertablePickupableActor>(DestroyedActor));
}

void AParadoxItemSlotActor::HandleInsertedItemInvalidated(
	AParadoxInsertablePickupableActor* Item)
{
	if (!Item || InsertedItem.Get() != Item)
	{
		return;
	}
	UnbindInsertedItem(Item);
	InsertedItem = nullptr;
	if (bInitialized && !bResetInProgress)
	{
		FinalizeOccupancyTransition(Item, nullptr);
	}
}

#undef LOCTEXT_NAMESPACE
