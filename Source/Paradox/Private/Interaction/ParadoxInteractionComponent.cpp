#include "Interaction/ParadoxInteractionComponent.h"

#include "Actions/GameplayActionDefinition.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GridNavigationOccupancyComponent.h"
#include "Components/GameplayActionComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Execution/GridMoveToCellExecution.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/GridNavigationQueryContext.h"
#include "Interaction/ParadoxInteractionActionBase.h"
#include "Interaction/ParadoxInteractionActionDefinition.h"
#include "Graph/PuzzleGraphSubsystem.h"
#include "Navigation/GridNavigationData.h"
#include "Navigation/GridTrafficReservation.h"
#include "Navigation/GridWorldSnapshot.h"
#include "Paradox.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "SmartObjectComponent.h"
#include "SmartObjectRuntime.h"
#include "SmartObjectSubsystem.h"
#include "StructUtils/StructView.h"
#include "Subsystems/GridWorldSubsystem.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "ParadoxInteractionComponent"

namespace UE::Paradox::Interaction::Private
{
	const FName TargetParameterName =
		GET_MEMBER_NAME_CHECKED(FParadoxInteractionActionParameters, Target);
	const FName InteractionTagParameterName =
		GET_MEMBER_NAME_CHECKED(FParadoxInteractionActionParameters, InteractionTag);

	const FProperty* FindParameterProperty(const FName PropertyName)
	{
		return FParadoxInteractionActionParameters::StaticStruct()->FindPropertyByName(
			PropertyName);
	}

	bool HasRequiredParameterSchema(
		const UGameplayActionDefinition& Definition,
		FString& OutDiagnostic)
	{
		const FInstancedPropertyBag& Bag = Definition.GetDefaultParameters();
		const FProperty* TargetProperty = FindParameterProperty(TargetParameterName);
		const FProperty* InteractionTagProperty =
			FindParameterProperty(InteractionTagParameterName);
		const FPropertyBagPropertyDesc* TargetDesc =
			Bag.FindPropertyDescByName(TargetParameterName);
		const FPropertyBagPropertyDesc* InteractionTagDesc =
			Bag.FindPropertyDescByName(InteractionTagParameterName);
		if (!TargetProperty || !TargetDesc || !TargetDesc->CachedProperty
			|| !TargetDesc->CachedProperty->SameType(TargetProperty))
		{
			OutDiagnostic = TEXT(
				"The Gameplay Action Definition requires a Target Soft Object parameter typed as AActor.");
			return false;
		}
		if (!InteractionTagProperty || !InteractionTagDesc
			|| !InteractionTagDesc->CachedProperty
			|| !InteractionTagDesc->CachedProperty->SameType(InteractionTagProperty))
		{
			OutDiagnostic = TEXT(
				"The Gameplay Action Definition requires an InteractionTag Gameplay Tag parameter.");
			return false;
		}
		return true;
	}

	EParadoxInteractionRequestStatus ResolveQueryFailureStatus(
		const EParadoxInteractionQueryStatus QueryStatus)
	{
		switch (QueryStatus)
		{
		case EParadoxInteractionQueryStatus::InvalidRequester:
			return EParadoxInteractionRequestStatus::InvalidRequester;
		case EParadoxInteractionQueryStatus::InvalidInteractionTag:
			return EParadoxInteractionRequestStatus::InvalidInteractionTag;
		default:
			return EParadoxInteractionRequestStatus::QueryFailed;
		}
	}

	FGuid ResolveRequesterOccupancyId(const AActor& Requester)
	{
		const UGridNavigationOccupancyComponent* Occupancy =
			UGridNavigationOccupancyComponent::FindActiveAgentOccupancy(Requester);
		return Occupancy != nullptr ? Occupancy->OccupantId : FGuid();
	}

	AController* ResolveMovementController(AActor* Requester)
	{
		if (AController* Controller = Cast<AController>(Requester))
		{
			return Controller;
		}
		if (APawn* Pawn = Cast<APawn>(Requester))
		{
			return Pawn->GetController();
		}
		return nullptr;
	}

	FGuid ResolveRequesterReservationId(const AActor& Requester, const FGuid& OccupancyId)
	{
		auto ResolveFromContext = [](const UObject* Context) -> FGuid
		{
			if (Context != nullptr
				&& Context->GetClass()->ImplementsInterface(UGridNavigationQueryContext::StaticClass()))
			{
				return IGridNavigationQueryContext::Execute_GetGridReservationId(
					const_cast<UObject*>(Context));
			}
			return FGuid();
		};

		FGuid ReservationId = ResolveFromContext(&Requester);
		if (!ReservationId.IsValid())
		{
			if (const APawn* Pawn = Cast<APawn>(&Requester))
			{
				ReservationId = ResolveFromContext(Pawn->GetController());
			}
			else if (const AController* Controller = Cast<AController>(&Requester))
			{
				ReservationId = ResolveFromContext(Controller->GetPawn());
			}
		}
		if (ReservationId.IsValid())
		{
			return ReservationId;
		}
		return OccupancyId;
	}

	void ResolveRequesterAgentShape(const AActor& Requester, float& OutRadius, float& OutHeight)
	{
		OutRadius = 42.0f;
		OutHeight = 192.0f;
		const AController* Controller = Cast<AController>(&Requester);
		if (const APawn* Pawn = Cast<APawn>(&Requester))
		{
			Controller = Pawn->GetController();
		}
		if (Controller != nullptr)
		{
			const FNavAgentProperties& Agent = Controller->GetNavAgentPropertiesRef();
			OutRadius = Agent.AgentRadius > 0.0f ? Agent.AgentRadius : OutRadius;
			OutHeight = Agent.AgentHeight > 0.0f ? Agent.AgentHeight : OutHeight;
		}
	}

	bool HasOtherOwner(
		const TArray<FGuid, TInlineAllocator<2>>& Owners,
		const FGuid& FirstIgnoredId,
		const FGuid& SecondIgnoredId)
	{
		return Owners.ContainsByPredicate(
			[&FirstIgnoredId, &SecondIgnoredId](const FGuid& OwnerId)
			{
				return OwnerId.IsValid()
					&& OwnerId != FirstIgnoredId
					&& OwnerId != SecondIgnoredId;
			});
	}

	FParadoxInteractionMovementParameters ReadAuthoredMovementParameters(
		const UGameplayActionDefinition& Definition)
	{
		FParadoxInteractionMovementParameters Parameters;
		const FInstancedPropertyBag& Bag = Definition.GetDefaultParameters();
		if (const TValueOrError<UClass*, EPropertyBagResult> Value =
			Bag.GetValueClass(ParadoxInteractionActionParameters::NavigationFilter);
			Value.HasValue())
		{
			Parameters.NavigationFilter = Value.GetValue();
		}
		if (const TValueOrError<float, EPropertyBagResult> Value =
			Bag.GetValueFloat(ParadoxInteractionActionParameters::AcceptanceRadius);
			Value.HasValue())
		{
			Parameters.AcceptanceRadius = Value.GetValue();
		}
		if (const TValueOrError<bool, EPropertyBagResult> Value =
			Bag.GetValueBool(ParadoxInteractionActionParameters::AllowStrafe);
			Value.HasValue())
		{
			Parameters.bAllowStrafe = Value.GetValue();
		}
		return Parameters;
	}
}

UParadoxInteractionComponent::UParadoxInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

FParadoxInteractionQueryResult UParadoxInteractionComponent::QueryInteractionOptions(
	AActor* Requester) const
{
	return QueryInteractionOptionsInternal(Requester, nullptr);
}

FParadoxInteractionQueryResult UParadoxInteractionComponent::QueryInteractionOptionsByTag(
	AActor* Requester,
	const FGameplayTag InteractionTag) const
{
	return QueryInteractionOptionsInternal(Requester, &InteractionTag);
}

bool UParadoxInteractionComponent::HasFreeInteractionOption(
	AActor* Requester,
	const FGameplayTag InteractionTag) const
{
	const FParadoxInteractionQueryResult Result = InteractionTag.IsValid()
		? QueryInteractionOptionsByTag(Requester, InteractionTag)
		: QueryInteractionOptions(Requester);
	return Result.Options.ContainsByPredicate(
		[](const FParadoxInteractionOption& Option)
		{
			return Option.State == EParadoxInteractionOptionState::Free;
		});
}

FParadoxInteractionAvailabilityResult
UParadoxInteractionComponent::EvaluateInteractionAvailability(
	AActor* Requester,
	const FGameplayTag InteractionTag) const
{
	FParadoxInteractionAvailabilityResult Availability;
	Availability.InteractionTag = InteractionTag;
	if (!IsValid(Requester))
	{
		Availability.Status = EParadoxInteractionAvailabilityStatus::InvalidRequester;
		Availability.DiagnosticMessage = TEXT("A valid requester Actor is required.");
		return Availability;
	}

	FParadoxInteractionOption CatalogOption;
	EParadoxInteractionRequestStatus CatalogStatus = EParadoxInteractionRequestStatus::InvalidRequester;
	if (!ResolveExactFreeCatalogOption(
		Requester,
		InteractionTag,
		CatalogOption,
		CatalogStatus,
		Availability.QueryStatus,
		Availability.DiagnosticMessage))
	{
		switch (CatalogStatus)
		{
		case EParadoxInteractionRequestStatus::InvalidRequester:
			Availability.Status = EParadoxInteractionAvailabilityStatus::InvalidRequester;
			break;
		case EParadoxInteractionRequestStatus::NoMatchingInteraction:
			Availability.Status = EParadoxInteractionAvailabilityStatus::NoMatchingInteraction;
			break;
		case EParadoxInteractionRequestStatus::SlotUnavailable:
			Availability.Status = EParadoxInteractionAvailabilityStatus::NoFreeSlot;
			break;
		default:
			Availability.Status = EParadoxInteractionAvailabilityStatus::InvalidDefinition;
			break;
		}
		return Availability;
	}
	UGameplayActionDefinition* AuthoredDefinition = CatalogOption.GameplayActionDefinition.LoadSynchronous();
	if (!IsValid(AuthoredDefinition))
	{
		Availability.Status = EParadoxInteractionAvailabilityStatus::DefinitionUnavailable;
		Availability.DiagnosticMessage = TEXT("The exact interaction has no loadable Gameplay Action Definition.");
		return Availability;
	}
	const FParadoxInteractionMovementParameters MovementParameters =
		UE::Paradox::Interaction::Private::ReadAuthoredMovementParameters(*AuthoredDefinition);

	FParadoxInteractionOption BestOption;
	double PathCost = 0.0;
	bool bAlreadyInPlace = false;
	EParadoxInteractionRequestStatus CandidateStatus =
		EParadoxInteractionRequestStatus::InvalidRequester;
	if (!ResolveBestReachableExecutionOption(
		Requester,
		InteractionTag,
		BestOption,
		PathCost,
		bAlreadyInPlace,
		CandidateStatus,
		Availability.QueryStatus,
		Availability.DiagnosticMessage,
		&MovementParameters))
	{
		switch (CandidateStatus)
		{
		case EParadoxInteractionRequestStatus::InvalidRequester:
			Availability.Status = EParadoxInteractionAvailabilityStatus::InvalidRequester;
			break;
		case EParadoxInteractionRequestStatus::InvalidTarget:
			Availability.Status = EParadoxInteractionAvailabilityStatus::InvalidTarget;
			break;
		case EParadoxInteractionRequestStatus::NoMatchingInteraction:
			Availability.Status = EParadoxInteractionAvailabilityStatus::NoMatchingInteraction;
			break;
		case EParadoxInteractionRequestStatus::SlotUnavailable:
			Availability.Status = EParadoxInteractionAvailabilityStatus::NoFreeSlot;
			break;
		default:
			Availability.Status = EParadoxInteractionAvailabilityStatus::NoReachableSlot;
			break;
		}
		return Availability;
	}

	Availability.DestinationCell = BestOption.GridCellId;
	Availability.PathCost = PathCost;
	FGameplayActionRequest Request;
	FParadoxInteractionRequestResult RequestResult;
	if (!BuildGameplayActionRequest(
		Requester,
		InteractionTag,
		FGameplayTag(),
		Requester,
		Request,
		RequestResult))
	{
		Availability.QueryStatus = RequestResult.QueryStatus;
		Availability.DiagnosticMessage = RequestResult.DiagnosticMessage;
		switch (RequestResult.Status)
		{
		case EParadoxInteractionRequestStatus::InvalidRequester:
		case EParadoxInteractionRequestStatus::MissingGameplayActionComponent:
			Availability.Status = EParadoxInteractionAvailabilityStatus::InvalidRequester;
			break;
		case EParadoxInteractionRequestStatus::InvalidTarget:
		case EParadoxInteractionRequestStatus::UnrecordableTarget:
			Availability.Status = EParadoxInteractionAvailabilityStatus::InvalidTarget;
			break;
		case EParadoxInteractionRequestStatus::NoMatchingInteraction:
			Availability.Status = EParadoxInteractionAvailabilityStatus::NoMatchingInteraction;
			break;
		case EParadoxInteractionRequestStatus::SlotUnavailable:
			Availability.Status = EParadoxInteractionAvailabilityStatus::NoFreeSlot;
			break;
		case EParadoxInteractionRequestStatus::DefinitionUnavailable:
			Availability.Status = EParadoxInteractionAvailabilityStatus::DefinitionUnavailable;
			break;
		default:
			Availability.Status = EParadoxInteractionAvailabilityStatus::InvalidDefinition;
			break;
		}
		return Availability;
	}

	UGameplayActionComponent* ActionComponent =
		Requester->FindComponentByClass<UGameplayActionComponent>();
	if (!IsValid(ActionComponent))
	{
		Availability.Status = EParadoxInteractionAvailabilityStatus::InvalidRequester;
		Availability.DiagnosticMessage = TEXT("The requester has no Gameplay Action Component.");
		return Availability;
	}
	Availability.SubmissionResult = ActionComponent->PreflightAction(Request);
	if (!Availability.SubmissionResult.IsAccepted())
	{
		Availability.Status =
			Availability.SubmissionResult.Status == EGameplayActionSubmissionStatus::RejectedBlocked
				? EParadoxInteractionAvailabilityStatus::SchedulerRejected
				: EParadoxInteractionAvailabilityStatus::EffectUnavailable;
		Availability.DiagnosticMessage = Availability.SubmissionResult.DiagnosticMessage;
		return Availability;
	}

	Availability.Status = bAlreadyInPlace
		? EParadoxInteractionAvailabilityStatus::AvailableInPlace
		: EParadoxInteractionAvailabilityStatus::AvailableAfterMovement;
	Availability.DiagnosticMessage = bAlreadyInPlace
		? TEXT("The interaction is executable from the requester's current cell.")
		: TEXT("The interaction is executable after reaching the selected GridWorld cell.");
	return Availability;
}

bool UParadoxInteractionComponent::CanRequestInteraction(
	AActor* Requester,
	const FGameplayTag InteractionTag) const
{
	return EvaluateInteractionAvailability(Requester, InteractionTag).IsAvailable();
}

FParadoxInteractionRequestResult UParadoxInteractionComponent::RequestInteraction(
	AActor* Requester,
	const FGameplayTag InteractionTag,
	const FGameplayTag OriginTag,
	UObject* RequestSource) const
{
	FGameplayActionRequest Request;
	FParadoxInteractionRequestResult Result;
	if (!BuildGameplayActionRequest(
		Requester,
		InteractionTag,
		OriginTag,
		RequestSource,
		Request,
		Result))
	{
		return Result;
	}

	UGameplayActionComponent* ActionComponent =
		Requester ? Requester->FindComponentByClass<UGameplayActionComponent>() : nullptr;
	if (!IsValid(ActionComponent))
	{
		Result.Status = EParadoxInteractionRequestStatus::MissingGameplayActionComponent;
		Result.DiagnosticMessage = TEXT(
			"The requester no longer owns a valid Gameplay Action Component.");
		return Result;
	}
	Result.SubmissionResult = ActionComponent->PreflightAction(Request);
	if (!Result.SubmissionResult.IsAccepted())
	{
		Result.Status = Result.SubmissionResult.ReasonTag
			== ParadoxGameplayTags::Result_Failure_Interaction_InvalidPosition
			? EParadoxInteractionRequestStatus::InvalidCurrentPosition
			: EParadoxInteractionRequestStatus::SubmissionRejected;
		Result.DiagnosticMessage = Result.SubmissionResult.DiagnosticMessage;
		return Result;
	}

	Result.SubmissionResult = ActionComponent->SubmitAction(Request);
	Result.Status = Result.SubmissionResult.IsAccepted()
		? EParadoxInteractionRequestStatus::Accepted
		: EParadoxInteractionRequestStatus::SubmissionRejected;
	Result.DiagnosticMessage = Result.SubmissionResult.DiagnosticMessage;
	if (bEnableDebug && IsParadoxInteractionDebugEnabled())
	{
		PARADOX_LOG_INFO(
			TEXT("Interaction request target='%s' requester='%s' tag='%s' status=%d handle=%lld diagnostic='%s'."),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(Requester),
			*InteractionTag.ToString(),
			static_cast<int32>(Result.Status),
			Result.SubmissionResult.Handle.GetValue(),
			*Result.DiagnosticMessage);
	}
	return Result;
}

void UParadoxInteractionComponent::RefreshInteractionSources()
{
	AActor* Owner = GetOwner();
	TInlineComponentArray<USmartObjectComponent*> CurrentComponents;
	if (IsValid(Owner))
	{
		Owner->GetComponents(CurrentComponents, false);
	}

	bool bChanged = CurrentComponents.Num() != InteractionSources.Num();
	if (!bChanged)
	{
		for (USmartObjectComponent* Component : CurrentComponents)
		{
			if (!InteractionSources.Contains(TWeakObjectPtr<USmartObjectComponent>(Component)))
			{
				bChanged = true;
				break;
			}
		}
	}
	if (!bChanged)
	{
		BindPuzzleAffordanceSources();
		return;
	}

	UnbindInteractionSources();
	for (USmartObjectComponent* Component : CurrentComponents)
	{
		if (!IsValid(Component) || Component->GetOwner() != Owner)
		{
			continue;
		}
		InteractionSources.Add(Component);
		Component->GetOnSmartObjectEventNative().AddUObject(
			this,
			&ThisClass::HandleSmartObjectEvent);
		Component->TransformUpdated.AddUObject(
			this,
			&ThisClass::HandleSmartObjectTransformUpdated);
	}
	BindPuzzleAffordanceSources();
	BroadcastInteractionAffordanceChanged();
}

void UParadoxInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshInteractionSources();
}

void UParadoxInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindPuzzleAffordanceSources();
	UnbindInteractionSources();
	Super::EndPlay(EndPlayReason);
}

void UParadoxInteractionComponent::BindPuzzleAffordanceSources()
{
	UnbindPuzzleAffordanceSources();
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
	{
		return;
	}
	TInlineComponentArray<UPuzzleReceiverComponent*> Receivers;
	Owner->GetComponents(Receivers, false);
	for (UPuzzleReceiverComponent* Receiver : Receivers)
	{
		if (IsValid(Receiver) && Receiver->GetOwner() == Owner)
		{
			ReceiverAffordanceSources.Add(Receiver);
			Receiver->OnReceiverStateChangedNative.AddUObject(this, &ThisClass::HandleReceiverStateChanged);
			Receiver->OnReceiverActivationPrerequisitesChangedNative.AddUObject(this, &ThisClass::HandleReceiverPrerequisitesChanged);
		}
	}
	TInlineComponentArray<UPuzzleEmitterComponent*> Emitters;
	Owner->GetComponents(Emitters, false);
	for (UPuzzleEmitterComponent* Emitter : Emitters)
	{
		if (IsValid(Emitter) && Emitter->GetOwner() == Owner)
		{
			EmitterAffordanceSources.Add(Emitter);
			Emitter->OnSignalChangedNative.AddUObject(this, &ThisClass::HandleEmitterSignalChanged);
		}
	}
	if (UPuzzleGraphSubsystem* Graph = GetWorld() ? GetWorld()->GetSubsystem<UPuzzleGraphSubsystem>() : nullptr)
	{
		Graph->OnPuzzleGraphTopologyChangedNative.AddUObject(this, &ThisClass::HandlePuzzleGraphTopologyChanged);
		Graph->OnPuzzleGraphLinkStateChangedNative.AddUObject(this, &ThisClass::HandlePuzzleGraphLinkStateChanged);
	}
}

void UParadoxInteractionComponent::UnbindPuzzleAffordanceSources()
{
	for (const TWeakObjectPtr<UPuzzleReceiverComponent>& WeakReceiver : ReceiverAffordanceSources)
	{
		if (UPuzzleReceiverComponent* Receiver = WeakReceiver.Get())
		{
			Receiver->OnReceiverStateChangedNative.RemoveAll(this);
			Receiver->OnReceiverActivationPrerequisitesChangedNative.RemoveAll(this);
		}
	}
	ReceiverAffordanceSources.Reset();
	for (const TWeakObjectPtr<UPuzzleEmitterComponent>& WeakEmitter : EmitterAffordanceSources)
	{
		if (UPuzzleEmitterComponent* Emitter = WeakEmitter.Get())
		{
			Emitter->OnSignalChangedNative.RemoveAll(this);
		}
	}
	EmitterAffordanceSources.Reset();
	if (UPuzzleGraphSubsystem* Graph = GetWorld() ? GetWorld()->GetSubsystem<UPuzzleGraphSubsystem>() : nullptr)
	{
		Graph->OnPuzzleGraphTopologyChangedNative.RemoveAll(this);
		Graph->OnPuzzleGraphLinkStateChangedNative.RemoveAll(this);
	}
}

void UParadoxInteractionComponent::HandleReceiverStateChanged(UPuzzleReceiverComponent* Receiver, const bool bReceiverActive)
{
	(void)Receiver;
	(void)bReceiverActive;
	BroadcastInteractionAffordanceChanged();
}

void UParadoxInteractionComponent::HandleReceiverPrerequisitesChanged(UPuzzleReceiverComponent* Receiver, const bool bPrerequisitesSatisfied)
{
	(void)Receiver;
	(void)bPrerequisitesSatisfied;
	BroadcastInteractionAffordanceChanged();
}

void UParadoxInteractionComponent::HandleEmitterSignalChanged(UPuzzleEmitterComponent* Emitter, const FGameplayTag SignalTag, const FPuzzleSignalState SignalState)
{
	(void)Emitter;
	(void)SignalTag;
	(void)SignalState;
	BroadcastInteractionAffordanceChanged();
}

void UParadoxInteractionComponent::HandlePuzzleGraphTopologyChanged(const int64 Revision, APuzzleController* Controller, const EPuzzleGraphTopologyChangeKind ChangeKind)
{
	(void)Revision;
	(void)Controller;
	(void)ChangeKind;
	BroadcastInteractionAffordanceChanged();
}

void UParadoxInteractionComponent::HandlePuzzleGraphLinkStateChanged(const FPuzzleGraphLinkHandle& LinkHandle, const FPuzzleGraphLinkState& PreviousState, const FPuzzleGraphLinkState& NewState)
{
	(void)LinkHandle;
	(void)PreviousState;
	(void)NewState;
	BroadcastInteractionAffordanceChanged();
}

#if WITH_EDITOR
EDataValidationResult UParadoxInteractionComponent::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}

	TSet<FGameplayTag> SeenTags;
	if (!InteractionDefinitions.IsEmpty())
	{
		TInlineComponentArray<USmartObjectComponent*> SmartObjectComponents;
		if (const AActor* Owner = GetOwner())
		{
			Owner->GetComponents(SmartObjectComponents, false);
		}
		if (SmartObjectComponents.IsEmpty())
		{
			Context.AddError(LOCTEXT(
				"MissingSmartObjectComponent",
				"A non-empty interaction catalog requires a direct Smart Object Component."));
			Result = EDataValidationResult::Invalid;
		}
		else if (!SmartObjectComponents.ContainsByPredicate(
			[](const USmartObjectComponent* Component)
			{
				return IsValid(Component) && Component->GetDefinition() != nullptr;
			}))
		{
			Context.AddError(LOCTEXT(
				"MissingSmartObjectDefinition",
				"A non-empty interaction catalog requires a direct Smart Object Component with a Definition."));
			Result = EDataValidationResult::Invalid;
		}
	}
	for (int32 Index = 0; Index < InteractionDefinitions.Num(); ++Index)
	{
		const FParadoxInteractionDefinition& Definition = InteractionDefinitions[Index];
		if (!Definition.InteractionTag.IsValid())
		{
			Context.AddError(FText::Format(
				LOCTEXT("MissingInteractionTag", "Interaction definition {0} has no Interaction Tag."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		else if (SeenTags.Contains(Definition.InteractionTag))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateInteractionTag", "Interaction Tag '{0}' appears more than once in this catalog."),
				FText::FromName(Definition.InteractionTag.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
		SeenTags.Add(Definition.InteractionTag);

		if (Definition.GameplayActionDefinition.IsNull())
		{
			Context.AddError(FText::Format(
				LOCTEXT("MissingActionDefinition", "Interaction definition {0} has no Gameplay Action Definition."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		else if (UGameplayActionDefinition* ActionDefinition =
			Definition.GameplayActionDefinition.LoadSynchronous())
		{
			if (!ActionDefinition->InstanceClass
				|| !ActionDefinition->InstanceClass->IsChildOf(
					UParadoxInteractionActionBase::StaticClass()))
			{
				Context.AddError(FText::Format(
					LOCTEXT(
						"InvalidInteractionActionClass",
						"Interaction definition {0} must use an action class derived from UParadoxInteractionActionBase."),
					FText::AsNumber(Index)));
				Result = EDataValidationResult::Invalid;
			}
			if (ActionDefinition->JournalRequirement
				== EGameplayActionJournalRequirement::Disabled)
			{
				Context.AddError(FText::Format(
					LOCTEXT(
						"DisabledInteractionJournaling",
						"Interaction definition {0} must use Optional or Required Gameplay Action journaling."),
					FText::AsNumber(Index)));
				Result = EDataValidationResult::Invalid;
			}
			FString SchemaDiagnostic;
			if (!UE::Paradox::Interaction::Private::HasRequiredParameterSchema(
				*ActionDefinition,
				SchemaDiagnostic))
			{
				Context.AddError(FText::Format(
					LOCTEXT(
						"InvalidInteractionParameterSchema",
						"Interaction definition {0} has an invalid parameter schema: {1}"),
					FText::AsNumber(Index),
					FText::FromString(SchemaDiagnostic)));
				Result = EDataValidationResult::Invalid;
			}
		}
		else
		{
			Context.AddError(FText::Format(
				LOCTEXT(
					"UnavailableActionDefinition",
					"Interaction definition {0} references a Gameplay Action Definition that cannot be loaded."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
	}
	if (GridProjectionExtent.GetMin() < 0.0f || TrafficAdditionalSeparation < 0.0f)
	{
		Context.AddError(LOCTEXT(
			"InvalidGridConfiguration",
			"Grid Projection Extent and Traffic Additional Separation must be non-negative."));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

FParadoxInteractionQueryResult UParadoxInteractionComponent::QueryInteractionOptionsInternal(
	AActor* Requester,
	const FGameplayTag* InteractionTag) const
{
	using namespace UE::Paradox::Interaction::Private;

	FParadoxInteractionQueryResult Result;
	if (InteractionTag != nullptr && !InteractionTag->IsValid())
	{
		Result.Status = EParadoxInteractionQueryStatus::InvalidInteractionTag;
		Result.DiagnosticMessage = TEXT("A valid Interaction Tag is required for a tagged query.");
		return Result;
	}
	if (!IsValid(Requester))
	{
		Result.Status = EParadoxInteractionQueryStatus::InvalidRequester;
		Result.DiagnosticMessage = TEXT("A valid requester Actor is required.");
		return Result;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		Result.Status = EParadoxInteractionQueryStatus::MissingWorld;
		Result.DiagnosticMessage = TEXT("The interaction component has no World.");
		return Result;
	}
	USmartObjectSubsystem* SmartObjects = USmartObjectSubsystem::GetCurrent(World);
	if (SmartObjects == nullptr)
	{
		Result.Status = EParadoxInteractionQueryStatus::MissingSmartObjectSubsystem;
		Result.DiagnosticMessage = TEXT("The World has no Smart Object Subsystem.");
		return Result;
	}
	if (InteractionSources.IsEmpty())
	{
		Result.Status = EParadoxInteractionQueryStatus::MissingSmartObjectComponent;
		Result.DiagnosticMessage = TEXT(
			"The interaction Actor has no registered direct Smart Object Component.");
		return Result;
	}
	UGridWorldSubsystem* GridWorld = World->GetSubsystem<UGridWorldSubsystem>();
	if (GridWorld == nullptr)
	{
		Result.Status = EParadoxInteractionQueryStatus::MissingGridWorld;
		Result.DiagnosticMessage = TEXT("The World has no GridWorld Subsystem.");
		return Result;
	}
	AGridNavigationData* GridNavigationData = GridWorld->GetNavigationData();
	const FGridWorldSnapshotPtr GridSnapshot =
		GridNavigationData != nullptr ? GridNavigationData->GetSnapshot() : nullptr;
	const FGridTrafficReservationSnapshotPtr TrafficSnapshot =
		GridNavigationData != nullptr
			? GridNavigationData->GetTrafficReservationSnapshot()
			: nullptr;

	const FGuid RequesterOccupancyId = ResolveRequesterOccupancyId(*Requester);
	const FGuid RequesterReservationId =
		ResolveRequesterReservationId(*Requester, RequesterOccupancyId);
	float RequesterRadius = 42.0f;
	float RequesterHeight = 192.0f;
	ResolveRequesterAgentShape(*Requester, RequesterRadius, RequesterHeight);
	const FSmartObjectActorUserData RequesterUserData(Requester);
	const FConstStructView RequesterView = FConstStructView::Make(RequesterUserData);

	for (const TWeakObjectPtr<USmartObjectComponent>& WeakSource : InteractionSources)
	{
		USmartObjectComponent* Source = WeakSource.Get();
		if (!IsValid(Source) || Source->GetOwner() != GetOwner())
		{
			continue;
		}
		const FSmartObjectHandle SmartObjectHandle = Source->GetRegisteredHandle();
		if (!SmartObjectHandle.IsValid())
		{
			continue;
		}

		TArray<FSmartObjectSlotHandle> SlotHandles;
		SmartObjects->GetAllSlots(SmartObjectHandle, SlotHandles);
		SlotHandles.Sort();
		for (const FSmartObjectSlotHandle& SlotHandle : SlotHandles)
		{
			FGameplayTagContainer SlotActivityTags;
			ESmartObjectSlotState SlotState = ESmartObjectSlotState::Invalid;
			bool bSlotEnabled = false;
			bool bSlotOwnedByRequester = false;
			const bool bReadSlot = SmartObjects->ReadSlotData(
				SlotHandle,
				[&](const FConstSmartObjectSlotView SlotView)
				{
					SlotView.GetActivityTags(SlotActivityTags);
					SlotState = SlotView.GetState();
					bSlotEnabled = SlotView.IsEnabled();
					const FConstStructView UserData = SlotView.GetUserData();
					if (const FSmartObjectActorUserData* ActorUserData =
						UserData.GetPtr<FSmartObjectActorUserData>())
					{
						bSlotOwnedByRequester = ActorUserData->UserActor.Get() == Requester;
					}
				});
			if (!bReadSlot || !bSlotEnabled
				|| !SmartObjects->EvaluateSelectionConditions(SlotHandle, RequesterView))
			{
				continue;
			}

			const TOptional<FTransform> SlotTransform =
				SmartObjects->GetSlotTransform(SlotHandle);
			if (!SlotTransform.IsSet())
			{
				continue;
			}
			const FGridCellQueryResult Projected = GridWorld->ProjectPoint(
				SlotTransform->GetLocation(),
				GridProjectionExtent);

			for (const FParadoxInteractionDefinition& Definition : InteractionDefinitions)
			{
				if (!Definition.InteractionTag.IsValid()
					|| (InteractionTag != nullptr
						&& InteractionTag->IsValid()
						&& !Definition.InteractionTag.MatchesTag(*InteractionTag))
					|| (!Definition.SlotActivityRequirements.IsEmpty()
						&& !Definition.SlotActivityRequirements.Matches(SlotActivityTags)))
				{
					continue;
				}

				FParadoxInteractionOption& Option = Result.Options.AddDefaulted_GetRef();
				Option.InteractionTag = Definition.InteractionTag;
				Option.GameplayActionDefinition = Definition.GameplayActionDefinition;
				Option.TargetActor = GetOwner();
				Option.SmartObjectHandle = SmartObjectHandle;
				Option.SlotHandle = SlotHandle;
				Option.SlotWorldTransform = SlotTransform.GetValue();
				if (Projected.Status != EGridQueryStatus::Success || !Projected.CellId.IsValid())
				{
					Option.State = EParadoxInteractionOptionState::GridUnresolved;
					continue;
				}

				Option.GridCellId = Projected.CellId;
				const FGridCellData* Cell =
					GridSnapshot.IsValid() ? GridSnapshot->FindCell(Projected.CellId) : nullptr;
				const bool bSmartObjectOccupied =
					!bSlotOwnedByRequester
					&& (SlotState == ESmartObjectSlotState::Claimed
						|| SlotState == ESmartObjectSlotState::Occupied
						|| !SmartObjects->CanBeClaimed(SlotHandle));
				const bool bGridOccupied = Cell == nullptr
					|| !Cell->bWalkable
					|| (Cell->bOccupied
						&& (Cell->OccupancyOwners.IsEmpty()
							|| HasOtherOwner(
								Cell->OccupancyOwners,
								RequesterOccupancyId,
								RequesterReservationId)))
					|| HasOtherOwner(
						Cell->ReservationOwners,
						RequesterReservationId,
						RequesterOccupancyId);
				const bool bTrafficOccupied = Cell != nullptr
					&& TrafficSnapshot.IsValid()
					&& TrafficSnapshot->ConflictsWithCell(
						Cell->WorldCenter,
						RequesterRadius,
						RequesterHeight,
						FMath::Max(0.0f, TrafficAdditionalSeparation),
						RequesterOccupancyId);
				Option.State = bSmartObjectOccupied || bGridOccupied || bTrafficOccupied
					? EParadoxInteractionOptionState::Occupied
					: EParadoxInteractionOptionState::Free;
			}
		}
	}

	Result.Options.Sort(
		[](const FParadoxInteractionOption& Left, const FParadoxInteractionOption& Right)
		{
			if (Left.SlotHandle != Right.SlotHandle)
			{
				return Left.SlotHandle < Right.SlotHandle;
			}
			const int32 TagOrder = Left.InteractionTag.GetTagName().Compare(
				Right.InteractionTag.GetTagName());
			if (TagOrder != 0)
			{
				return TagOrder < 0;
			}
			return Left.GameplayActionDefinition.ToSoftObjectPath().ToString()
				< Right.GameplayActionDefinition.ToSoftObjectPath().ToString();
		});
	Result.Status = Result.Options.IsEmpty()
		? EParadoxInteractionQueryStatus::NoOptions
		: EParadoxInteractionQueryStatus::Success;
	Result.DiagnosticMessage = Result.Options.IsEmpty()
		? TEXT("No configured interaction matched an enabled current Smart Object slot.")
		: FString::Printf(TEXT("Resolved %d interaction option(s)."), Result.Options.Num());

#if ENABLE_DRAW_DEBUG
	if (bEnableDebug && IsParadoxInteractionDebugEnabled())
	{
		for (const FParadoxInteractionOption& Option : Result.Options)
		{
			const FColor Color = Option.State == EParadoxInteractionOptionState::Free
				? FColor::Green
				: (Option.State == EParadoxInteractionOptionState::Occupied
					? FColor::Orange
					: FColor::Red);
			DrawDebugSphere(
				World,
				Option.SlotWorldTransform.GetLocation(),
				18.0f,
				12,
				Color,
				false,
				0.15f,
				0,
				2.0f);
		}
		PARADOX_LOG_INFO(
			TEXT("Interaction component '%s' resolved %d option(s) for requester '%s'."),
			*GetNameSafe(this),
			Result.Options.Num(),
			*GetNameSafe(Requester));
	}
#endif
	return Result;
}

bool UParadoxInteractionComponent::ResolveCurrentExecutionOption(
	AActor* Requester,
	const FGameplayTag InteractionTag,
	FParadoxInteractionOption& OutOption,
	EParadoxInteractionRequestStatus& OutStatus,
	EParadoxInteractionQueryStatus& OutQueryStatus,
	FString& OutDiagnostic) const
{
	OutOption = FParadoxInteractionOption();
	OutQueryStatus = EParadoxInteractionQueryStatus::NoOptions;
	if (!IsValid(Requester))
	{
		OutStatus = EParadoxInteractionRequestStatus::InvalidRequester;
		OutDiagnostic = TEXT("A valid requester Actor is required.");
		return false;
	}
	if (!InteractionTag.IsValid())
	{
		OutStatus = EParadoxInteractionRequestStatus::InvalidInteractionTag;
		OutQueryStatus = EParadoxInteractionQueryStatus::InvalidInteractionTag;
		OutDiagnostic = TEXT("A valid exact Interaction Tag is required.");
		return false;
	}

	const FParadoxInteractionQueryResult Query =
		QueryInteractionOptionsByTag(Requester, InteractionTag);
	OutQueryStatus = Query.Status;
	if (!Query.IsSuccess())
	{
		OutStatus = UE::Paradox::Interaction::Private::ResolveQueryFailureStatus(Query.Status);
		OutDiagnostic = Query.DiagnosticMessage;
		return false;
	}

	TArray<const FParadoxInteractionOption*, TInlineAllocator<8>> ExactOptions;
	for (const FParadoxInteractionOption& Option : Query.Options)
	{
		if (Option.InteractionTag == InteractionTag)
		{
			ExactOptions.Add(&Option);
		}
	}
	if (ExactOptions.IsEmpty())
	{
		OutStatus = EParadoxInteractionRequestStatus::NoMatchingInteraction;
		OutDiagnostic = TEXT("No current interaction option matches the exact requested tag.");
		return false;
	}

	UGridWorldSubsystem* GridWorld = GetWorld()
		? GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	if (!GridWorld)
	{
		OutStatus = EParadoxInteractionRequestStatus::QueryFailed;
		OutQueryStatus = EParadoxInteractionQueryStatus::MissingGridWorld;
		OutDiagnostic = TEXT("The World has no GridWorld Subsystem.");
		return false;
	}
	const FGridCellQueryResult RequesterCell = GridWorld->ProjectPoint(
		Requester->GetActorLocation(),
		GridProjectionExtent);
	if (RequesterCell.Status != EGridQueryStatus::Success
		|| !RequesterCell.CellId.IsValid())
	{
		OutStatus = EParadoxInteractionRequestStatus::InvalidCurrentPosition;
		OutDiagnostic = TEXT("The requester's current position does not resolve to GridWorld.");
		return false;
	}

	bool bCurrentCellOccupied = false;
	for (const FParadoxInteractionOption* Option : ExactOptions)
	{
		if (!Option || Option->GridCellId != RequesterCell.CellId)
		{
			continue;
		}
		if (Option->State == EParadoxInteractionOptionState::Free)
		{
			OutOption = *Option;
			OutStatus = EParadoxInteractionRequestStatus::Accepted;
			OutDiagnostic = TEXT("Resolved a free interaction slot at the requester's current cell.");
			return true;
		}
		bCurrentCellOccupied |=
			Option->State == EParadoxInteractionOptionState::Occupied;
	}

	OutStatus = bCurrentCellOccupied
		? EParadoxInteractionRequestStatus::SlotUnavailable
		: EParadoxInteractionRequestStatus::InvalidCurrentPosition;
	OutDiagnostic = bCurrentCellOccupied
		? TEXT("Every matching interaction slot on the requester's current cell is unavailable.")
		: TEXT("The requester is not currently positioned on a resolved interaction cell.");
	return false;
}

bool UParadoxInteractionComponent::ResolveBestReachableExecutionOption(
	AActor* Requester,
	const FGameplayTag InteractionTag,
	FParadoxInteractionOption& OutOption,
	double& OutPathCost,
	bool& bOutAlreadyInPlace,
	EParadoxInteractionRequestStatus& OutStatus,
	EParadoxInteractionQueryStatus& OutQueryStatus,
	FString& OutDiagnostic,
	const FParadoxInteractionMovementParameters* MovementParameters) const
{
	OutOption = FParadoxInteractionOption();
	OutPathCost = 0.0;
	bOutAlreadyInPlace = false;
	if (!IsValid(Requester))
	{
		OutStatus = EParadoxInteractionRequestStatus::InvalidRequester;
		OutQueryStatus = EParadoxInteractionQueryStatus::InvalidRequester;
		OutDiagnostic = TEXT("A valid requester Actor is required.");
		return false;
	}
	if (!InteractionTag.IsValid())
	{
		OutStatus = EParadoxInteractionRequestStatus::InvalidInteractionTag;
		OutQueryStatus = EParadoxInteractionQueryStatus::InvalidInteractionTag;
		OutDiagnostic = TEXT("A valid exact Interaction Tag is required.");
		return false;
	}

	const FParadoxInteractionQueryResult Query =
		QueryInteractionOptionsByTag(Requester, InteractionTag);
	OutQueryStatus = Query.Status;
	if (!Query.IsSuccess())
	{
		OutStatus = UE::Paradox::Interaction::Private::ResolveQueryFailureStatus(Query.Status);
		OutDiagnostic = Query.DiagnosticMessage;
		return false;
	}

	TArray<const FParadoxInteractionOption*, TInlineAllocator<8>> ExactOptions;
	bool bHasExactOption = false;
	for (const FParadoxInteractionOption& Option : Query.Options)
	{
		if (Option.InteractionTag != InteractionTag)
		{
			continue;
		}
		bHasExactOption = true;
		if (Option.State == EParadoxInteractionOptionState::Free)
		{
			ExactOptions.Add(&Option);
		}
	}
	if (!bHasExactOption)
	{
		OutStatus = EParadoxInteractionRequestStatus::NoMatchingInteraction;
		OutDiagnostic = TEXT("No current interaction option matches the exact requested tag.");
		return false;
	}
	if (ExactOptions.IsEmpty())
	{
		OutStatus = EParadoxInteractionRequestStatus::SlotUnavailable;
		OutDiagnostic = TEXT("No exact matching Smart Object slot is currently free.");
		return false;
	}

	UGridWorldSubsystem* GridWorld = GetWorld()
		? GetWorld()->GetSubsystem<UGridWorldSubsystem>()
		: nullptr;
	if (!GridWorld)
	{
		OutStatus = EParadoxInteractionRequestStatus::QueryFailed;
		OutQueryStatus = EParadoxInteractionQueryStatus::MissingGridWorld;
		OutDiagnostic = TEXT("The World has no GridWorld Subsystem.");
		return false;
	}
	const FGridCellQueryResult RequesterCell = GridWorld->ProjectPoint(
		Requester->GetActorLocation(),
		GridProjectionExtent);
	const bool bRequesterCellValid =
		RequesterCell.Status == EGridQueryStatus::Success
		&& RequesterCell.CellId.IsValid();
	AController* Controller =
		UE::Paradox::Interaction::Private::ResolveMovementController(Requester);

	struct FCandidate
	{
		const FParadoxInteractionOption* Option = nullptr;
		double Cost = 0.0;
		bool bAlreadyInPlace = false;
	};
	TArray<FCandidate, TInlineAllocator<8>> Candidates;
	TMap<FGridCellId, FGridMoveToCellEvaluationResult> EvaluationsByCell;
	for (const FParadoxInteractionOption* Option : ExactOptions)
	{
		if (!Option || !Option->GridCellId.IsValid())
		{
			continue;
		}
		if (bRequesterCellValid && Option->GridCellId == RequesterCell.CellId)
		{
			Candidates.Add({Option, -1.0, true});
			continue;
		}
		if (!IsValid(Controller))
		{
			continue;
		}
		FGridMoveToCellEvaluationResult* Evaluation =
			EvaluationsByCell.Find(Option->GridCellId);
		if (!Evaluation)
		{
			FGridMoveToCellExecutionRequest MoveRequest;
			MoveRequest.Controller = Controller;
			MoveRequest.GoalLocation = Option->SlotWorldTransform.GetLocation();
			if (MovementParameters)
			{
				MoveRequest.FilterClass = MovementParameters->NavigationFilter;
				MoveRequest.AcceptanceRadius = MovementParameters->AcceptanceRadius;
				MoveRequest.bAllowStrafe = MovementParameters->bAllowStrafe;
			}
			MoveRequest.AcceptPartialPath = EAIOptionFlag::Disable;
			MoveRequest.bTrackMovingGoal = false;
			MoveRequest.GoalContentionPolicy = EGridGoalContentionPolicy::RejectOccupied;
			Evaluation = &EvaluationsByCell.Add(
				Option->GridCellId,
				UGridMoveToCellExecution::Evaluate(MoveRequest));
		}
		if (Evaluation->bCanExecute)
		{
			Candidates.Add({Option, Evaluation->PathCost, Evaluation->bAlreadyAtGoal});
		}
	}
	if (Candidates.IsEmpty())
	{
		OutStatus = EParadoxInteractionRequestStatus::InvalidCurrentPosition;
		OutDiagnostic = IsValid(Controller)
			? TEXT("No free exact interaction slot has a complete controller-aware path.")
			: TEXT("The requester is not in place and has no Controller capable of GridWorld movement.");
		return false;
	}

	Candidates.Sort([](const FCandidate& Left, const FCandidate& Right)
	{
		if (!FMath::IsNearlyEqual(Left.Cost, Right.Cost))
		{
			return Left.Cost < Right.Cost;
		}
		const FGridCellCoord& LeftCoord = Left.Option->GridCellId.Coord;
		const FGridCellCoord& RightCoord = Right.Option->GridCellId.Coord;
		if (LeftCoord.X != RightCoord.X)
		{
			return LeftCoord.X < RightCoord.X;
		}
		if (LeftCoord.Y != RightCoord.Y)
		{
			return LeftCoord.Y < RightCoord.Y;
		}
		if (LeftCoord.Layer != RightCoord.Layer)
		{
			return LeftCoord.Layer < RightCoord.Layer;
		}
		return LexToString(Left.Option->SlotHandle)
			< LexToString(Right.Option->SlotHandle);
	});
	OutOption = *Candidates[0].Option;
	OutPathCost = FMath::Max(0.0, Candidates[0].Cost);
	bOutAlreadyInPlace = Candidates[0].bAlreadyInPlace;
	OutStatus = EParadoxInteractionRequestStatus::Accepted;
	OutDiagnostic = bOutAlreadyInPlace
		? TEXT("Resolved a free interaction slot on the requester's current cell.")
		: TEXT("Resolved the lowest-cost reachable free interaction slot.");
	return true;
}

bool UParadoxInteractionComponent::ResolveExactFreeCatalogOption(
	AActor* Requester,
	const FGameplayTag InteractionTag,
	FParadoxInteractionOption& OutOption,
	EParadoxInteractionRequestStatus& OutStatus,
	EParadoxInteractionQueryStatus& OutQueryStatus,
	FString& OutDiagnostic) const
{
	OutOption = FParadoxInteractionOption();
	if (!IsValid(Requester))
	{
		OutStatus = EParadoxInteractionRequestStatus::InvalidRequester;
		OutQueryStatus = EParadoxInteractionQueryStatus::InvalidRequester;
		OutDiagnostic = TEXT("A valid requester Actor is required.");
		return false;
	}
	if (!InteractionTag.IsValid())
	{
		OutStatus = EParadoxInteractionRequestStatus::InvalidInteractionTag;
		OutQueryStatus = EParadoxInteractionQueryStatus::InvalidInteractionTag;
		OutDiagnostic = TEXT("A valid exact Interaction Tag is required.");
		return false;
	}
	const FParadoxInteractionQueryResult Query = QueryInteractionOptionsByTag(Requester, InteractionTag);
	OutQueryStatus = Query.Status;
	if (!Query.IsSuccess())
	{
		OutStatus = UE::Paradox::Interaction::Private::ResolveQueryFailureStatus(Query.Status);
		OutDiagnostic = Query.DiagnosticMessage;
		return false;
	}
	bool bHasExactOption = false;
	for (const FParadoxInteractionOption& Option : Query.Options)
	{
		if (Option.InteractionTag != InteractionTag)
		{
			continue;
		}
		bHasExactOption = true;
		if (Option.State == EParadoxInteractionOptionState::Free)
		{
			OutOption = Option;
			OutStatus = EParadoxInteractionRequestStatus::Accepted;
			OutDiagnostic = TEXT("Resolved the exact interaction catalog entry from a free current slot.");
			return true;
		}
	}
	OutStatus = bHasExactOption
		? EParadoxInteractionRequestStatus::SlotUnavailable
		: EParadoxInteractionRequestStatus::NoMatchingInteraction;
	OutDiagnostic = bHasExactOption
		? TEXT("No exact matching Smart Object slot is currently free.")
		: TEXT("No current interaction option matches the exact requested tag.");
	return false;
}

bool UParadoxInteractionComponent::BuildGameplayActionRequest(
	AActor* Requester,
	const FGameplayTag InteractionTag,
	const FGameplayTag OriginTag,
	UObject* RequestSource,
	FGameplayActionRequest& OutRequest,
	FParadoxInteractionRequestResult& OutResult) const
{
	using namespace UE::Paradox::Interaction::Private;

	OutRequest = FGameplayActionRequest();
	OutResult = FParadoxInteractionRequestResult();
	if (!IsValid(Requester))
	{
		OutResult.Status = EParadoxInteractionRequestStatus::InvalidRequester;
		OutResult.DiagnosticMessage = TEXT("A valid requester Actor is required.");
		return false;
	}
	AActor* Target = GetOwner();
	if (!IsValid(Target))
	{
		OutResult.Status = EParadoxInteractionRequestStatus::InvalidTarget;
		OutResult.DiagnosticMessage = TEXT("The interaction target is invalid.");
		return false;
	}
	if (!Target->HasAnyFlags(RF_WasLoaded))
	{
		OutResult.Status = EParadoxInteractionRequestStatus::UnrecordableTarget;
		OutResult.DiagnosticMessage = FString::Printf(
			TEXT("Target '%s' is runtime-created and has no supported replay-stable identity."),
			*GetNameSafe(Target));
		return false;
	}
	if (!InteractionTag.IsValid())
	{
		OutResult.Status = EParadoxInteractionRequestStatus::InvalidInteractionTag;
		OutResult.DiagnosticMessage = TEXT("A valid exact Interaction Tag is required.");
		return false;
	}
	UGameplayActionComponent* ActionComponent =
		Requester->FindComponentByClass<UGameplayActionComponent>();
	if (!IsValid(ActionComponent) || ActionComponent->GetOwner() != Requester)
	{
		OutResult.Status = EParadoxInteractionRequestStatus::MissingGameplayActionComponent;
		OutResult.DiagnosticMessage = TEXT(
			"The requester must directly own a Gameplay Action Component.");
		return false;
	}

	FParadoxInteractionOption Option;
	if (!ResolveExactFreeCatalogOption(
		Requester,
		InteractionTag,
		Option,
		OutResult.Status,
		OutResult.QueryStatus,
		OutResult.DiagnosticMessage))
	{
		return false;
	}

	UGameplayActionDefinition* ActionDefinition =
		Option.GameplayActionDefinition.LoadSynchronous();
	if (!IsValid(ActionDefinition))
	{
		OutResult.Status = EParadoxInteractionRequestStatus::DefinitionUnavailable;
		OutResult.DiagnosticMessage = TEXT(
			"The matching interaction has no loadable Gameplay Action Definition.");
		return false;
	}
	if (!ActionDefinition->InstanceClass
		|| !ActionDefinition->InstanceClass->IsChildOf(
			UParadoxInteractionActionBase::StaticClass())
		|| ActionDefinition->JournalRequirement
			== EGameplayActionJournalRequirement::Disabled)
	{
		OutResult.Status = EParadoxInteractionRequestStatus::InvalidDefinition;
		OutResult.DiagnosticMessage = TEXT(
			"The Gameplay Action Definition must use a Paradox interaction action class and Optional or Required journaling.");
		return false;
	}
	if (!HasRequiredParameterSchema(*ActionDefinition, OutResult.DiagnosticMessage))
	{
		OutResult.Status = EParadoxInteractionRequestStatus::ParameterSchemaMismatch;
		return false;
	}

	FGameplayActionRequestCreationResult Creation =
		UGameplayActionBlueprintLibrary::CreateActionRequest(ActionDefinition);
	if (!Creation.WasCreated())
	{
		OutResult.Status = EParadoxInteractionRequestStatus::InvalidDefinition;
		OutResult.DiagnosticMessage = Creation.DiagnosticMessage;
		return false;
	}

	FParadoxInteractionActionParameters Parameters;
	Parameters.Target = TSoftObjectPtr<AActor>(Target);
	Parameters.InteractionTag = InteractionTag;
	const FProperty* TargetProperty = FindParameterProperty(TargetParameterName);
	const FProperty* InteractionTagProperty =
		FindParameterProperty(InteractionTagParameterName);
	const EGameplayActionParameterAccessResult TargetSet =
		UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
			Creation.Request,
			TargetParameterName,
			TargetProperty,
			TargetProperty
				? TargetProperty->ContainerPtrToValuePtr<void>(&Parameters)
				: nullptr);
	const EGameplayActionParameterAccessResult TagSet =
		UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
			Creation.Request,
			InteractionTagParameterName,
			InteractionTagProperty,
			InteractionTagProperty
				? InteractionTagProperty->ContainerPtrToValuePtr<void>(&Parameters)
				: nullptr);
	if (TargetSet != EGameplayActionParameterAccessResult::Success
		|| TagSet != EGameplayActionParameterAccessResult::Success)
	{
		OutResult.Status = EParadoxInteractionRequestStatus::ParameterSchemaMismatch;
		OutResult.DiagnosticMessage = TEXT(
			"The required interaction parameters could not be written to the request.");
		return false;
	}

	UGameplayActionBlueprintLibrary::SetRequestContext(
		Creation.Request,
		OriginTag,
		IsValid(RequestSource) ? RequestSource : Requester,
		FGameplayActionCorrelationData());
	OutRequest = MoveTemp(Creation.Request);
	return true;
}

void UParadoxInteractionComponent::BroadcastInteractionAffordanceChanged()
{
	OnInteractionAffordanceChanged.Broadcast(this);
	InteractionAffordanceChangedNative.Broadcast(this);
}

void UParadoxInteractionComponent::UnbindInteractionSources()
{
	for (const TWeakObjectPtr<USmartObjectComponent>& WeakSource : InteractionSources)
	{
		if (USmartObjectComponent* Source = WeakSource.Get())
		{
			Source->GetOnSmartObjectEventNative().RemoveAll(this);
			Source->TransformUpdated.RemoveAll(this);
		}
	}
	InteractionSources.Reset();
}

void UParadoxInteractionComponent::HandleSmartObjectEvent(
	const FSmartObjectEventData& EventData,
	const AActor* Interactor)
{
	(void)EventData;
	(void)Interactor;
	BroadcastInteractionAffordanceChanged();
}

void UParadoxInteractionComponent::HandleSmartObjectTransformUpdated(
	USceneComponent* UpdatedComponent,
	const EUpdateTransformFlags UpdateTransformFlags,
	const ETeleportType Teleport)
{
	(void)UpdateTransformFlags;
	(void)Teleport;
	if (const USmartObjectComponent* SmartObjectComponent =
		Cast<USmartObjectComponent>(UpdatedComponent))
	{
		if (USmartObjectSubsystem* SmartObjects =
			USmartObjectSubsystem::GetCurrent(GetWorld()))
		{
			const FSmartObjectHandle Handle =
				SmartObjectComponent->GetRegisteredHandle();
			if (Handle.IsValid())
			{
				SmartObjects->UpdateSmartObjectTransform(
					Handle,
					SmartObjectComponent->GetComponentTransform());
			}
		}
	}
	BroadcastInteractionAffordanceChanged();
}

#undef LOCTEXT_NAMESPACE
