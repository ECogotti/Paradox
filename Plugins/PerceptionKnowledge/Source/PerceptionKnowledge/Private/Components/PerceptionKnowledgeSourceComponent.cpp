#include "Components/PerceptionKnowledgeSourceComponent.h"

#include "Components/ActorComponent.h"
#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interfaces/PerceptionKnowledgeStateProvider.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "PerceptionKnowledgeModule.h"
#include "PerceptionKnowledgeTags.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Settings/PerceptionKnowledgeDeveloperSettings.h"
#include "Subsystems/PerceptionKnowledgeWorldSubsystem.h"
#include "TimerManager.h"

#define TAG_PerceptionKnowledge_Sense_Sight (PerceptionKnowledgeTags::Sense_Sight.GetTag())
#define TAG_PerceptionKnowledge_Sense_Hearing (PerceptionKnowledgeTags::Sense_Hearing.GetTag())

namespace
{
	FPerceptionKnowledgeOperationResult MakeSourceResult(
		const EPerceptionKnowledgeOperationStatus Status,
		const FString& Message,
		const FPerceptionKnowledgeEntityId EntityId = FPerceptionKnowledgeEntityId(),
		const FGuid ObservationId = FGuid())
	{
		FPerceptionKnowledgeOperationResult Result;
		Result.Status = Status;
		Result.Message = Message;
		Result.EntityId = EntityId;
		Result.ObservationId = ObservationId;
		return Result;
	}
}

UPerceptionKnowledgeSourceComponent::UPerceptionKnowledgeSourceComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoRegisterAsSource = true;
}

void UPerceptionKnowledgeSourceComponent::EnsureStableId(const bool bForceNewId)
{
	if (IsTemplate())
	{
		EntityId.Reset();
		return;
	}
	if (bForceNewId || !EntityId.IsValid())
	{
		EntityId = FPerceptionKnowledgeEntityId::NewId();
	}
}

FPerceptionKnowledgeOperationResult
UPerceptionKnowledgeSourceComponent::AssignEntityId(
	const FPerceptionKnowledgeEntityId InEntityId)
{
	if (!IsInGameThread())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::WrongThread,
			TEXT("Entity identity can only be assigned on the Game Thread."),
			EntityId);
	}
	if (IsTemplate())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidOwner,
			TEXT("Entity identity cannot be assigned to a template Source."),
			EntityId);
	}
	if (!InEntityId.IsValid())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidEntityId,
			TEXT("The requested Entity ID is invalid."),
			EntityId);
	}
	if (bSourceEnabled || bSemanticRegistered || bSuccessfullyRegistered)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidArgument,
			TEXT("Disable and fully unregister the Source before assigning an Entity ID."),
			EntityId);
	}
	if (EntityId == InEntityId)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::Unchanged,
			TEXT("The disabled Source already owns the requested Entity ID."),
			EntityId);
	}
	if (!IsValid(GetOwner()))
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidOwner,
			TEXT("Entity identity cannot be assigned without a valid owning Actor."),
			EntityId);
	}

	UWorld* World = GetWorld();
	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		World ? World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::NotRegistered,
			TEXT("No Perception Knowledge world subsystem is available to validate identity uniqueness."),
			EntityId);
	}
	if (UPerceptionKnowledgeSourceComponent* Existing =
		Subsystem->FindSource(InEntityId))
	{
		return MakeSourceResult(
			Existing == this
				? EPerceptionKnowledgeOperationStatus::InvalidArgument
				: EPerceptionKnowledgeOperationStatus::DuplicateEntityId,
			Existing == this
				? TEXT("The disabled Source still has a stale live registry entry; unregister it before assigning identity.")
				: FString::Printf(
					TEXT("Entity ID %s is already owned by Source '%s' on Actor '%s'."),
					*InEntityId.ToString(),
					*GetNameSafe(Existing),
					*GetNameSafe(Existing->GetOwner())),
			EntityId);
	}

	EntityId = InEntityId;
	return MakeSourceResult(
		EPerceptionKnowledgeOperationStatus::Success,
		TEXT("The disabled Source accepted the requested Entity ID."),
		EntityId);
}

void UPerceptionKnowledgeSourceComponent::PostLoad()
{
	Super::PostLoad();
	EnsureStableId();
}

void UPerceptionKnowledgeSourceComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
	EnsureStableId();
}

void UPerceptionKnowledgeSourceComponent::PostDuplicate(const EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	EnsureStableId(DuplicateMode != EDuplicateMode::PIE);
}

#if WITH_EDITOR
void UPerceptionKnowledgeSourceComponent::PostEditImport()
{
	Super::PostEditImport();
	EnsureStableId(true);
}
#endif

void UPerceptionKnowledgeSourceComponent::SynchronizeNativeSenseRegistration()
{
	RegisterAsSourceForSenses.Reset();
	if (bRegisterForSight)
	{
		RegisterAsSourceForSenses.Add(UAISense_Sight::StaticClass());
	}
	if (bRegisterForHearing)
	{
		RegisterAsSourceForSenses.Add(UAISense_Hearing::StaticClass());
	}
	bAutoRegisterAsSource = bSourceEnabled;
}

void UPerceptionKnowledgeSourceComponent::InitializeRuntimeStates()
{
	if (bRuntimeStatesInitialized)
	{
		return;
	}
	bRuntimeStatesInitialized = true;
	RuntimeStates.Reset();

	for (FPerceptionKnowledgeExposedState State : InitialObservableStates)
	{
		if (State.ObservableThroughSenses.IsEmpty())
		{
			State.ObservableThroughSenses.AddTag(TAG_PerceptionKnowledge_Sense_Sight);
		}
		if (!State.IsValid())
		{
			PERCEPTIONKNOWLEDGE_LOG_WARNING(
				TEXT("Ignored invalid initial state on Source=%s Owner=%s StateTag=%s."),
				*GetNameSafe(this),
				*GetNameSafe(GetOwner()),
				*State.StateTag.ToString());
			continue;
		}
		if (RuntimeStates.Contains(State.StateTag))
		{
			PERCEPTIONKNOWLEDGE_LOG_WARNING(
				TEXT("Ignored duplicate initial StateTag=%s on Source=%s Owner=%s; the first value remains authoritative."),
				*State.StateTag.ToString(),
				*GetNameSafe(this),
				*GetNameSafe(GetOwner()));
			continue;
		}
		RuntimeStates.Add(State.StateTag, MoveTemp(State));
	}
}

void UPerceptionKnowledgeSourceComponent::OnRegister()
{
	EnsureStableId();
	SynchronizeNativeSenseRegistration();
	InitializeRuntimeStates();
	Super::OnRegister();

	if (bSourceEnabled)
	{
		LastRegistrationResult = RegisterSemanticSource();
		if (!LastRegistrationResult.IsSuccess() && bSuccessfullyRegistered)
		{
			UnregisterFromPerceptionSystem();
		}
	}
	else
	{
		LastRegistrationResult = MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::Disabled,
			TEXT("Source registration is disabled."),
			EntityId);
	}
	UpdateDebugTimer();
}

void UPerceptionKnowledgeSourceComponent::OnUnregister()
{
	if (bSuccessfullyRegistered)
	{
		UnregisterFromPerceptionSystem();
	}
	UnregisterSemanticSource();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DebugTimerHandle);
	}
	Super::OnUnregister();
}

void UPerceptionKnowledgeSourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bSuccessfullyRegistered)
	{
		UnregisterFromPerceptionSystem();
	}
	UnregisterSemanticSource();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DebugTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeSourceComponent::RegisterSemanticSource()
{
	if (bSemanticRegistered)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::Unchanged,
			TEXT("Source is already registered."),
			EntityId);
	}
	if (!bSourceEnabled)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::Disabled,
			TEXT("Source registration is disabled."),
			EntityId);
	}
	if (!GetOwner())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidOwner,
			TEXT("Source has no owning Actor."),
			EntityId);
	}
	if (!EntityId.IsValid())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidEntityId,
			TEXT("Source has no valid Entity ID."),
			EntityId);
	}
	if (!bRegisterForSight && !bRegisterForHearing)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidArgument,
			TEXT("At least one native sense must be enabled for the Source."),
			EntityId);
	}

	UWorld* World = GetWorld();
	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		World ? World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::NotRegistered,
			TEXT("No Perception Knowledge world subsystem is available."),
			EntityId);
	}

	FPerceptionKnowledgeOperationResult Result = Subsystem->RegisterSource(this);
	bSemanticRegistered = Result.Status == EPerceptionKnowledgeOperationStatus::Success
		|| Result.Status == EPerceptionKnowledgeOperationStatus::Unchanged;
	return Result;
}

void UPerceptionKnowledgeSourceComponent::UnregisterSemanticSource()
{
	if (!bSemanticRegistered)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UPerceptionKnowledgeWorldSubsystem* Subsystem =
			World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>())
		{
			Subsystem->UnregisterSource(this);
		}
	}
	bSemanticRegistered = false;
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeSourceComponent::SetObservableState(
	const FGameplayTag StateTag,
	const FPerceptionKnowledgeValue& Value)
{
	return SetStateInternal(StateTag, Value, EPerceptionKnowledgeFactStatus::Known);
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeSourceComponent::SetObservableStateUnknown(
	const FGameplayTag StateTag)
{
	const FPerceptionKnowledgeExposedState* Existing = RuntimeStates.Find(StateTag);
	if (!Existing)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidArgument,
			TEXT("Unknown can only be applied to an existing State Tag so its value type remains defined."),
			EntityId);
	}
	return SetStateInternal(StateTag, Existing->Value, EPerceptionKnowledgeFactStatus::Unknown);
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeSourceComponent::InvalidateObservableState(
	const FGameplayTag StateTag)
{
	const FPerceptionKnowledgeExposedState* Existing = RuntimeStates.Find(StateTag);
	if (!Existing)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidArgument,
			TEXT("The requested State Tag is not exposed."),
			EntityId);
	}
	return SetStateInternal(StateTag, Existing->Value, EPerceptionKnowledgeFactStatus::Invalidated);
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeSourceComponent::SetStateInternal(
	const FGameplayTag StateTag,
	const FPerceptionKnowledgeValue& Value,
	const EPerceptionKnowledgeFactStatus Status)
{
	if (!IsInGameThread())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::WrongThread,
			TEXT("Observable state mutations must run on the game thread."),
			EntityId);
	}
	if (!StateTag.IsValid())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidTag,
			TEXT("A valid State Tag is required."),
			EntityId);
	}
	if (Status == EPerceptionKnowledgeFactStatus::Known && !Value.IsValid())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidValue,
			TEXT("Known state requires a valid typed value."),
			EntityId);
	}

	InitializeRuntimeStates();
	FPerceptionKnowledgeExposedState* Existing = RuntimeStates.Find(StateTag);
	if (Existing
		&& Existing->Value.GetType() != EPerceptionKnowledgeValueType::None
		&& Value.GetType() != EPerceptionKnowledgeValueType::None
		&& Existing->Value.GetType() != Value.GetType())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::TypeMismatch,
			FString::Printf(
				TEXT("State Tag %s is already bound to value type %d and cannot change to type %d."),
				*StateTag.ToString(),
				static_cast<int32>(Existing->Value.GetType()),
				static_cast<int32>(Value.GetType())),
			EntityId);
	}

	if (Existing && Existing->Status == Status && Existing->Value == Value)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::Unchanged,
			TEXT("The exposed state already has this semantic value and status."),
			EntityId);
	}

	FPerceptionKnowledgeExposedState NewState;
	if (Existing)
	{
		NewState = *Existing;
	}
	else
	{
		NewState.StateTag = StateTag;
		NewState.ObservableThroughSenses.AddTag(TAG_PerceptionKnowledge_Sense_Sight);
	}
	NewState.Value = Value;
	NewState.Status = Status;
	RuntimeStates.Add(StateTag, NewState);

	int32 NotifiedListeners = 0;
	if (bSemanticRegistered)
	{
		if (UWorld* World = GetWorld())
		{
			if (UPerceptionKnowledgeWorldSubsystem* Subsystem =
				World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>())
			{
				NotifiedListeners = Subsystem->NotifySourceStateChanged(this, NewState, false);
			}
		}
	}
	return MakeSourceResult(
		NotifiedListeners > 0
			? EPerceptionKnowledgeOperationStatus::Success
			: EPerceptionKnowledgeOperationStatus::NoObservers,
		FString::Printf(TEXT("Observable state updated; %d current listener(s) refreshed."), NotifiedListeners),
		EntityId);
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeSourceComponent::RemoveObservableState(
	const FGameplayTag StateTag)
{
	if (!IsInGameThread())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::WrongThread,
			TEXT("Observable state mutations must run on the game thread."),
			EntityId);
	}
	FPerceptionKnowledgeExposedState Existing;
	if (!RuntimeStates.RemoveAndCopyValue(StateTag, Existing))
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidArgument,
			TEXT("The requested State Tag is not exposed."),
			EntityId);
	}

	int32 NotifiedListeners = 0;
	if (bSemanticRegistered)
	{
		if (UWorld* World = GetWorld())
		{
			if (UPerceptionKnowledgeWorldSubsystem* Subsystem =
				World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>())
			{
				NotifiedListeners = Subsystem->NotifySourceStateChanged(this, Existing, true);
			}
		}
	}
	return MakeSourceResult(
		NotifiedListeners > 0
			? EPerceptionKnowledgeOperationStatus::Success
			: EPerceptionKnowledgeOperationStatus::NoObservers,
		FString::Printf(
			TEXT("Observable state removed and invalidated for %d current listener(s)."),
			NotifiedListeners),
		EntityId);
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeSourceComponent::NotifyProviderStatesChanged()
{
	if (!bSemanticRegistered)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::NotRegistered,
			TEXT("The Source must be registered before provider changes can be refreshed."),
			EntityId);
	}
	UWorld* World = GetWorld();
	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		World ? World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::NotRegistered,
			TEXT("No Perception Knowledge world subsystem is available."),
			EntityId);
	}
	const int32 Count = Subsystem->NotifyProviderStatesChanged(this);
	return MakeSourceResult(
		Count > 0
			? EPerceptionKnowledgeOperationStatus::Success
			: EPerceptionKnowledgeOperationStatus::NoObservers,
		FString::Printf(TEXT("Provider states refreshed for %d current listener(s)."), Count),
		EntityId);
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeSourceComponent::GetObservableStatesForSense(
	UPerceptionKnowledgeListenerComponent* Observer,
	const FGameplayTag SenseTag,
	TArray<FPerceptionKnowledgeExposedState>& OutStates) const
{
	OutStates.Reset();
	if (!SenseTag.IsValid())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidTag,
			TEXT("A valid Sense Tag is required."),
			EntityId);
	}

	TSet<FGameplayTag> SeenTags;
	for (const TPair<FGameplayTag, FPerceptionKnowledgeExposedState>& Pair : RuntimeStates)
	{
		if (Pair.Value.IsValid() && Pair.Value.IsObservableThrough(SenseTag))
		{
			OutStates.Add(Pair.Value);
			SeenTags.Add(Pair.Key);
		}
	}

	TArray<UObject*> Providers;
	AActor* OwnerActor = GetOwner();
	if (OwnerActor && OwnerActor->GetClass()->ImplementsInterface(UPerceptionKnowledgeStateProvider::StaticClass()))
	{
		Providers.Add(OwnerActor);
	}
	if (OwnerActor)
	{
		TArray<UActorComponent*> Components;
		OwnerActor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (Component
				&& Component != this
				&& Component->GetClass()->ImplementsInterface(UPerceptionKnowledgeStateProvider::StaticClass()))
			{
				Providers.Add(Component);
			}
		}
	}

	for (UObject* Provider : Providers)
	{
		TArray<FPerceptionKnowledgeExposedState> ProviderStates;
		IPerceptionKnowledgeStateProvider::Execute_GatherObservableStates(
			Provider,
			Observer,
			SenseTag,
			ProviderStates);
		for (const FPerceptionKnowledgeExposedState& State : ProviderStates)
		{
			if (!State.IsValid() || !State.IsObservableThrough(SenseTag))
			{
				PERCEPTIONKNOWLEDGE_LOG_WARNING(
					TEXT("Provider=%s returned invalid or non-observable StateTag=%s for Sense=%s on Source=%s."),
					*GetNameSafe(Provider),
					*State.StateTag.ToString(),
					*SenseTag.ToString(),
					*GetNameSafe(this));
				continue;
			}
			if (SeenTags.Contains(State.StateTag))
			{
				PERCEPTIONKNOWLEDGE_LOG_WARNING(
					TEXT("Provider=%s returned duplicate StateTag=%s for Source=%s; the first value remains authoritative."),
					*GetNameSafe(Provider),
					*State.StateTag.ToString(),
					*GetNameSafe(this));
				continue;
			}
			SeenTags.Add(State.StateTag);
			OutStates.Add(State);
		}
	}

	return MakeSourceResult(
		EPerceptionKnowledgeOperationStatus::Success,
		FString::Printf(TEXT("Gathered %d observable state(s)."), OutStates.Num()),
		EntityId);
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeSourceComponent::EmitObservableEvent(
	const FPerceptionKnowledgeEventRequest& Request)
{
	if (!IsInGameThread())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::WrongThread,
			TEXT("Observable events must be emitted on the game thread."),
			EntityId);
	}
	if (!bSemanticRegistered)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::NotRegistered,
			TEXT("The Source must be registered before emitting events."),
			EntityId);
	}
	if (!Request.EventTag.IsValid())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidTag,
			TEXT("A valid Event Tag is required."),
			EntityId);
	}
	if (Request.SenseTag == TAG_PerceptionKnowledge_Sense_Hearing)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::UnsupportedSense,
			TEXT("Hearing events must use EmitSemanticNoise so native range and affiliation filtering is preserved."),
			EntityId);
	}
	if (Request.SenseTag != TAG_PerceptionKnowledge_Sense_Sight)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::UnsupportedSense,
			TEXT("Milestone 1 routes direct events only through an active Sight relationship."),
			EntityId);
	}
	if (Request.Strength < 0.0f || !FMath::IsFinite(Request.Strength))
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::InvalidArgument,
			TEXT("Event strength must be finite and non-negative."),
			EntityId);
	}

	UWorld* World = GetWorld();
	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		World ? World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::NotRegistered,
			TEXT("No Perception Knowledge world subsystem is available."),
			EntityId);
	}

	FPerceptionKnowledgeEventObservation Event;
	Event.ObservationId = FGuid::NewGuid();
	Event.EventTag = Request.EventTag;
	Event.SenseTag = Request.SenseTag;
	Event.SourceEntityId = EntityId;
	Event.InstigatorEntityId = Subsystem->ResolveEntityId(
		Request.Instigator ? Request.Instigator.Get() : GetOwner());
	Event.WorldLocation = Request.bUseSourceLocation
		? GetOwner()->GetActorLocation()
		: Request.WorldLocation;
	Event.Strength = Request.Strength;
	Event.Confidence = 1.0f;
	Event.WorldTimestamp = World->GetTimeSeconds();
	Event.CauseTag = Request.CauseTag;

	const int32 Count = Subsystem->RouteObservableEvent(this, Event);
	return MakeSourceResult(
		Count > 0
			? EPerceptionKnowledgeOperationStatus::Success
			: EPerceptionKnowledgeOperationStatus::NoObservers,
		FString::Printf(TEXT("Observable event delivered to %d current listener(s)."), Count),
		EntityId,
		Event.ObservationId);
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeSourceComponent::EmitSemanticNoise(
	const FPerceptionKnowledgeNoiseRequest& Request)
{
	if (!bRegisterForHearing)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::Disabled,
			TEXT("Hearing registration is disabled for this Source."),
			EntityId);
	}

	UWorld* World = GetWorld();
	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		World ? World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::NotRegistered,
			TEXT("No Perception Knowledge world subsystem is available."),
			EntityId);
	}

	FName CorrelationTag;
	FPerceptionKnowledgeEventObservation Event;
	FPerceptionKnowledgeOperationResult Result =
		Subsystem->RegisterSemanticNoise(this, Request, CorrelationTag, Event);
	if (!Result.IsSuccess())
	{
		return Result;
	}

	AActor* NativeInstigator = Request.Instigator ? Request.Instigator.Get() : GetOwner();
	UAISense_Hearing::ReportNoiseEvent(
		this,
		Event.WorldLocation,
		Request.Loudness,
		NativeInstigator,
		Request.MaxRange,
		CorrelationTag);
	return Result;
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeSourceComponent::SetSourceEnabled(
	const bool bEnabled)
{
	if (bSourceEnabled == bEnabled)
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::Unchanged,
			TEXT("Source enabled state is unchanged."),
			EntityId);
	}

	bSourceEnabled = bEnabled;
	SynchronizeNativeSenseRegistration();
	if (!IsRegistered())
	{
		return MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::Success,
			TEXT("Source enabled state will be applied when the component registers."),
			EntityId);
	}

	if (bEnabled)
	{
		RegisterWithPerceptionSystem();
		LastRegistrationResult = RegisterSemanticSource();
		if (!LastRegistrationResult.IsSuccess() && bSuccessfullyRegistered)
		{
			UnregisterFromPerceptionSystem();
		}
	}
	else
	{
		if (bSuccessfullyRegistered)
		{
			UnregisterFromPerceptionSystem();
		}
		UnregisterSemanticSource();
		LastRegistrationResult = MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::Success,
			TEXT("Source disabled and unregistered."),
			EntityId);
	}
	return LastRegistrationResult;
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeSourceComponent::RetryRegistration()
{
	if (!bSourceEnabled)
	{
		LastRegistrationResult = MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::Disabled,
			TEXT("Enable the Source before retrying registration."),
			EntityId);
		return LastRegistrationResult;
	}
	if (bSemanticRegistered)
	{
		LastRegistrationResult = MakeSourceResult(
			EPerceptionKnowledgeOperationStatus::Unchanged,
			TEXT("Source is already registered."),
			EntityId);
		return LastRegistrationResult;
	}

	EnsureStableId();
	SynchronizeNativeSenseRegistration();
	if (IsRegistered() && !bSuccessfullyRegistered)
	{
		RegisterWithPerceptionSystem();
	}
	LastRegistrationResult = RegisterSemanticSource();
	if (!LastRegistrationResult.IsSuccess() && bSuccessfullyRegistered)
	{
		UnregisterFromPerceptionSystem();
	}
	return LastRegistrationResult;
}

bool UPerceptionKnowledgeSourceComponent::RegenerateEntityId()
{
	if (bSemanticRegistered)
	{
		PERCEPTIONKNOWLEDGE_LOG_WARNING(
			TEXT("Rejected Entity ID regeneration for registered Source=%s Owner=%s EntityId=%s."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			*EntityId.ToString());
		return false;
	}
	EnsureStableId(true);
	return EntityId.IsValid();
}

void UPerceptionKnowledgeSourceComponent::SetDebugEnabled(const bool bEnabled)
{
	bEnableDebug = bEnabled;
	UpdateDebugTimer();
}

void UPerceptionKnowledgeSourceComponent::UpdateDebugTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(DebugTimerHandle);
	if (!bEnableDebug || !IsRegistered())
	{
		return;
	}

	const UPerceptionKnowledgeDeveloperSettings* Settings =
		GetDefault<UPerceptionKnowledgeDeveloperSettings>();
	World->GetTimerManager().SetTimer(
		DebugTimerHandle,
		this,
		&UPerceptionKnowledgeSourceComponent::DrawSourceDebug,
		FMath::Max(0.05f, Settings->DebugDrawInterval),
		true);
}

void UPerceptionKnowledgeSourceComponent::DrawSourceDebug()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PerceptionKnowledge_DrawDebug);
	if (!bEnableDebug || !IsPerceptionKnowledgeDebugEnabled())
	{
		UpdateDebugTimer();
		return;
	}
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	FVector Origin;
	FVector Extent;
	OwnerActor->GetActorBounds(false, Origin, Extent);
	const FColor Color = bSemanticRegistered ? FColor::Blue : FColor::Magenta;
	DrawDebugBox(World, Origin, Extent, Color, false, 0.0f, 0, 1.5f);
	DrawDebugString(
		World,
		Origin + FVector(0.0, 0.0, Extent.Z + 25.0),
		FString::Printf(
			TEXT("PK Source %s %s Sight=%s Hearing=%s Native=%s Semantic=%s States=%d"),
			*GetNameSafe(OwnerActor),
			*EntityId.ToShortString(),
			bRegisterForSight ? TEXT("yes") : TEXT("no"),
			bRegisterForHearing ? TEXT("yes") : TEXT("no"),
			bSuccessfullyRegistered ? TEXT("yes") : TEXT("no"),
			bSemanticRegistered ? TEXT("yes") : TEXT("no"),
			RuntimeStates.Num()),
		nullptr,
		Color,
		0.0f,
		false,
		1.0f);
	if (UPerceptionKnowledgeWorldSubsystem* Subsystem =
		World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>())
	{
		Subsystem->RecordDebugDraw((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
	}
}

void UPerceptionKnowledgeSourceComponent::DumpSourceToLog() const
{
	PERCEPTIONKNOWLEDGE_LOG_INFO(
		TEXT("Source dump Component=%s Owner=%s EntityId=%s Enabled=%s NativeRegistered=%s SemanticRegistered=%s States=%d RegistrationStatus=%d Message=%s"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*EntityId.ToString(),
		bSourceEnabled ? TEXT("true") : TEXT("false"),
		bSuccessfullyRegistered ? TEXT("true") : TEXT("false"),
		bSemanticRegistered ? TEXT("true") : TEXT("false"),
		RuntimeStates.Num(),
		static_cast<int32>(LastRegistrationResult.Status),
		*LastRegistrationResult.Message);

	for (const TPair<FGameplayTag, FPerceptionKnowledgeExposedState>& Pair : RuntimeStates)
	{
		PERCEPTIONKNOWLEDGE_LOG_INFO(
			TEXT("  State=%s Status=%d Type=%d Value=%s Senses=%s"),
			*Pair.Key.ToString(),
			static_cast<int32>(Pair.Value.Status),
			static_cast<int32>(Pair.Value.Value.GetType()),
			*Pair.Value.Value.ToString(),
			*Pair.Value.ObservableThroughSenses.ToString());
	}
}

#undef TAG_PerceptionKnowledge_Sense_Sight
#undef TAG_PerceptionKnowledge_Sense_Hearing
