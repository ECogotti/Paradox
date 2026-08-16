#include "Inventory/ParadoxPickupableAction.h"

#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Characters/ParadoxCharacter.h"
#include "Components/GameplayActionComponent.h"
#include "GameplayActionTags.h"
#include "Inventory/ParadoxInventoryComponent.h"
#include "Inventory/ParadoxPickupableActor.h"
#include "Paradox.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace ParadoxPickupableActionParameters
{
	const FName Pickupable = GET_MEMBER_NAME_CHECKED(
		FParadoxPickupableGameplayActionParameters,
		Pickupable);
}

namespace UE::Paradox::PickupableAction::Private
{
	FGameplayActionSubmissionResult MakeFailure(
		const FGameplayTag Reason,
		const FString& Diagnostic)
	{
		FGameplayActionSubmissionResult Result;
		Result.Status = EGameplayActionSubmissionStatus::RejectedInvalidRequest;
		Result.ReasonTag = Reason;
		Result.DiagnosticMessage = Diagnostic;
		return Result;
	}

	AParadoxPickupableActor* ReadPickupable(const FInstancedPropertyBag& Bag)
	{
		FParadoxPickupableGameplayActionParameters Values;
		const FProperty* Property = FParadoxPickupableGameplayActionParameters::StaticStruct()
			->FindPropertyByName(ParadoxPickupableActionParameters::Pickupable);
		return UGameplayActionBlueprintLibrary::GetBagValueToProperty(
			Bag,
			ParadoxPickupableActionParameters::Pickupable,
			Property,
			Property ? Property->ContainerPtrToValuePtr<void>(&Values) : nullptr)
			== EGameplayActionParameterAccessResult::Success
			? Values.Pickupable.Get()
			: nullptr;
	}
}

AParadoxCharacter* UParadoxPickupableGameplayActionBase::GetPickupableActionCharacter() const
{
	if (UGameplayActionComponent* Component = GetOwningComponent())
	{
		return Cast<AParadoxCharacter>(Component->GetOwner());
	}
	return nullptr;
}

AParadoxPickupableActor* UParadoxPickupableGameplayActionBase::GetPickupableActionItem() const
{
	return UE::Paradox::PickupableAction::Private::ReadPickupable(GetParameters());
}

bool UParadoxPickupableGameplayActionBase::CanStartAction_Implementation(
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	if (!Super::CanStartAction_Implementation(OutFailureReason, OutDiagnostic))
	{
		return false;
	}
	AParadoxCharacter* Character = GetPickupableActionCharacter();
	AParadoxPickupableActor* Pickupable = GetPickupableActionItem();
	if (!Character || !Pickupable || !Character->GetInventoryComponent())
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest;
		OutDiagnostic = TEXT("Pickupable Action requires a Paradox Character, inventory and resolvable item parameter.");
		return false;
	}
	if (Character->GetInventoryComponent()->GetEquippedItem() != Pickupable
		|| Pickupable->GetCurrentHolder() != Character)
	{
		OutFailureReason = ParadoxGameplayTags::Result_Failure_Inventory_OwnershipConflict;
		OutDiagnostic = TEXT("The semantic Pickupable Action target is not currently equipped by this Character.");
		return false;
	}
	return CanExecutePickupableAction(
		Character,
		Pickupable,
		OutFailureReason,
		OutDiagnostic);
}

void UParadoxPickupableGameplayActionBase::OnActionStarted_Implementation()
{
	Super::OnActionStarted_Implementation();
	ExecutePickupableAction(GetPickupableActionCharacter(), GetPickupableActionItem());
}

bool UParadoxPickupableGameplayActionBase::CanExecutePickupableAction_Implementation(
	AParadoxCharacter* Character,
	AParadoxPickupableActor* Pickupable,
	FGameplayTag& OutFailureReason,
	FString& OutDiagnostic) const
{
	(void)Character;
	(void)Pickupable;
	(void)OutFailureReason;
	(void)OutDiagnostic;
	return true;
}

void UParadoxPickupableGameplayActionBase::ExecutePickupableAction_Implementation(
	AParadoxCharacter* Character,
	AParadoxPickupableActor* Pickupable)
{
	(void)Character;
	(void)Pickupable;
	FailAction(
		ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest,
		TEXT("Pickupable Action has no native or Blueprint execution implementation."));
}

void UParadoxPickupableGameplayActionBase::CompletePickupableActionSuccess(
	const FString& DiagnosticMessage)
{
	SucceedAction(GameplayActionTags::Result_Success, DiagnosticMessage);
}

void UParadoxPickupableGameplayActionBase::CompletePickupableActionFailure(
	const FGameplayTag ReasonTag,
	const FString& DiagnosticMessage)
{
	FailAction(ReasonTag, DiagnosticMessage);
}

UParadoxPickupableGameplayActionDefinition::UParadoxPickupableGameplayActionDefinition()
{
	InstanceClass = UParadoxPickupableGameplayActionBase::StaticClass();
	BlockedPolicy = EGameplayActionBlockedPolicy::Reject;
	JournalRequirement = EGameplayActionJournalRequirement::Optional;
	ExecutionLocks.AddTag(ParadoxGameplayTags::Lock_Inventory);
	TArray<FPropertyBagPropertyDesc> Descriptors;
	Descriptors.Add({
		ParadoxPickupableActionParameters::Pickupable,
		EPropertyBagPropertyType::SoftObject,
		AParadoxPickupableActor::StaticClass()});
	DefaultParameters.InitializeFromBagStruct(UPropertyBag::GetOrCreateFromDescs(Descriptors));
}

void UParadoxPickupableGameplayActionDefinition::PostLoad()
{
	Super::PostLoad();
	const UGameplayActionDefinition* NativeDefaults =
		GetClass()->GetDefaultObject<UGameplayActionDefinition>();
	if (this != NativeDefaults && NativeDefaults
		&& !DefaultParameters.HasSameLayout(NativeDefaults->DefaultParameters))
	{
		DefaultParameters.MigrateToNewBagInstance(NativeDefaults->DefaultParameters);
	}
}

#if WITH_EDITOR
EDataValidationResult UParadoxPickupableGameplayActionDefinition::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}
	const FPropertyBagPropertyDesc* Desc =
		DefaultParameters.FindPropertyDescByName(ParadoxPickupableActionParameters::Pickupable);
	if (!Desc || Desc->ValueType != EPropertyBagPropertyType::SoftObject
		|| Desc->ValueTypeObject != AParadoxPickupableActor::StaticClass())
	{
		Context.AddError(FText::FromString(TEXT("Pickupable Action Definitions require a soft AParadoxPickupableActor parameter named Pickupable.")));
		Result = EDataValidationResult::Invalid;
	}
	if (!ExecutionLocks.HasTagExact(ParadoxGameplayTags::Lock_Inventory)
		|| BlockedPolicy != EGameplayActionBlockedPolicy::Reject
		|| JournalRequirement == EGameplayActionJournalRequirement::Disabled)
	{
		Context.AddError(FText::FromString(TEXT("Pickupable Action Definitions require the inventory lock, Reject policy and journaling.")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

FGameplayActionSubmissionResult UParadoxPickupableAction::EvaluateExecution(
	AParadoxCharacter* Character) const
{
	FGameplayActionRequest Request;
	FGameplayActionSubmissionResult Failure;
	if (!BuildRequest(Character, Request, Failure))
	{
		return Failure;
	}
	return Character->GetGameplayActionComponent()->PreflightAction(Request);
}

FGameplayActionSubmissionResult UParadoxPickupableAction::RequestExecute(
	AParadoxCharacter* Character) const
{
	FGameplayActionRequest Request;
	FGameplayActionSubmissionResult Failure;
	if (!BuildRequest(Character, Request, Failure))
	{
		return Failure;
	}
	return Character->GetGameplayActionComponent()->SubmitAction(Request);
}

bool UParadoxPickupableAction::BuildRequest(
	AParadoxCharacter* Character,
	FGameplayActionRequest& OutRequest,
	FGameplayActionSubmissionResult& OutFailure) const
{
	using namespace UE::Paradox::PickupableAction::Private;
	if (!IsValid(Character) || !Character->GetInventoryComponent()
		|| !Character->GetGameplayActionComponent())
	{
		OutFailure = MakeFailure(
			ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest,
			TEXT("Pickupable Action requires a valid Paradox Character with inventory and Gameplay Actions."));
		return false;
	}
	AParadoxPickupableActor* Pickupable = Character->GetInventoryComponent()->GetEquippedItem();
	if (!Pickupable || Pickupable->GetCurrentHolder() != Character)
	{
		OutFailure = MakeFailure(
			ParadoxGameplayTags::Result_Failure_Inventory_SlotEmpty,
			TEXT("Pickupable Action requires an item currently equipped by this Character."));
		return false;
	}
	UGameplayActionDefinition* Definition = GameplayActionDefinition.LoadSynchronous();
	FGameplayActionRequestCreationResult Creation =
		UGameplayActionBlueprintLibrary::CreateActionRequest(Definition);
	if (!Creation.WasCreated())
	{
		OutFailure = MakeFailure(
			ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest,
			Creation.DiagnosticMessage);
		return false;
	}
	FParadoxPickupableGameplayActionParameters Values;
	Values.Pickupable = Pickupable;
	const FProperty* Property = Values.StaticStruct()->FindPropertyByName(
		ParadoxPickupableActionParameters::Pickupable);
	if (UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
			Creation.Request,
			ParadoxPickupableActionParameters::Pickupable,
			Property,
			Property ? Property->ContainerPtrToValuePtr<void>(&Values) : nullptr)
		!= EGameplayActionParameterAccessResult::Success)
	{
		OutFailure = MakeFailure(
			ParadoxGameplayTags::Result_Failure_Inventory_InvalidRequest,
			TEXT("Pickupable Action Definition has an incompatible Pickupable parameter."));
		return false;
	}
	UGameplayActionBlueprintLibrary::SetRequestContext(
		Creation.Request,
		FGameplayTag(),
		Character,
		FGameplayActionCorrelationData());
	OutRequest = MoveTemp(Creation.Request);
	return true;
}
