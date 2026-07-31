#include "Components/PerceptionKnowledgeListenerComponent.h"

#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Data/PerceptionKnowledgeProfile.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Perception/AISense.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"
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
	FPerceptionKnowledgeOperationResult MakeListenerResult(
		const EPerceptionKnowledgeOperationStatus Status,
		const FString& Message,
		const FPerceptionKnowledgeEntityId EntityId = FPerceptionKnowledgeEntityId())
	{
		FPerceptionKnowledgeOperationResult Result;
		Result.Status = Status;
		Result.Message = Message;
		Result.EntityId = EntityId;
		return Result;
	}

	bool IsSameSemanticState(
		const FPerceptionKnowledgeKnownState& Known,
		const FPerceptionKnowledgeStateObservation& Observation)
	{
		return Known.Status == Observation.Status && Known.Value == Observation.Value;
	}
}

UPerceptionKnowledgeListenerComponent::UPerceptionKnowledgeListenerComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("PerceptionKnowledgeSight"));
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("PerceptionKnowledgeHearing"));
	ConfigureSense(*SightConfig);
	ConfigureSense(*HearingConfig);
	SetDominantSense(UAISense_Sight::StaticClass());
}

void UPerceptionKnowledgeListenerComponent::ApplyProfileBeforeRegistration()
{
	bObservationSuspended = true;
	if (!SightConfig || !HearingConfig)
	{
		LastRegistrationResult = MakeListenerResult(
			EPerceptionKnowledgeOperationStatus::InvalidArgument,
			TEXT("Native Sight or Hearing configuration subobject is missing."));
		return;
	}
	if (!bListenerEnabled)
	{
		SightConfig->SetStartsEnabled(false);
		HearingConfig->SetStartsEnabled(false);
		LastRegistrationResult = MakeListenerResult(
			EPerceptionKnowledgeOperationStatus::Disabled,
			TEXT("Listener registration is disabled."));
		return;
	}
	if (!Profile)
	{
		SightConfig->SetStartsEnabled(false);
		HearingConfig->SetStartsEnabled(false);
		LastRegistrationResult = MakeListenerResult(
			EPerceptionKnowledgeOperationStatus::MissingProfile,
			TEXT("A Perception Knowledge Profile is required."));
		PERCEPTIONKNOWLEDGE_LOG_ERROR(
			TEXT("Listener=%s Owner=%s is suspended because its required Profile is missing."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
		return;
	}

	FString ProfileError;
	if (!Profile->IsConfigurationValid(ProfileError))
	{
		SightConfig->SetStartsEnabled(false);
		HearingConfig->SetStartsEnabled(false);
		LastRegistrationResult = MakeListenerResult(
			EPerceptionKnowledgeOperationStatus::InvalidArgument,
			FString::Printf(TEXT("Listener Profile is invalid: %s"), *ProfileError));
		PERCEPTIONKNOWLEDGE_LOG_ERROR(
			TEXT("Listener=%s Owner=%s is suspended because Profile=%s is invalid: %s"),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(Profile),
			*ProfileError);
		return;
	}

	SightConfig->SightRadius = Profile->SightRadius;
	SightConfig->LoseSightRadius = Profile->LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = Profile->PeripheralVisionHalfAngle;
	SightConfig->DetectionByAffiliation = Profile->SightAffiliation;
	SightConfig->SetMaxAge(Profile->SightMaxAge);
	SightConfig->SetStartsEnabled(Profile->bEnableSight);

	HearingConfig->HearingRange = Profile->HearingRange;
	HearingConfig->DetectionByAffiliation = Profile->HearingAffiliation;
	HearingConfig->SetMaxAge(Profile->HearingMaxAge);
	HearingConfig->SetStartsEnabled(Profile->bEnableHearing);

	LastRegistrationResult = MakeListenerResult(
		EPerceptionKnowledgeOperationStatus::Success,
		TEXT("Listener Profile is valid."));
	bObservationSuspended = !HasValidObservationBody();
	if (bObservationSuspended)
	{
		LastRegistrationResult.Message =
			TEXT("Listener Profile is valid, but observations are suspended until a valid Body Actor exists.");
	}
}

void UPerceptionKnowledgeListenerComponent::OnRegister()
{
	BindProfileChanges();
	ApplyProfileBeforeRegistration();
	Super::OnRegister();

	OnTargetPerceptionUpdated.AddUniqueDynamic(
		this,
		&UPerceptionKnowledgeListenerComponent::HandleTargetPerceptionUpdated);
	OnTargetPerceptionForgotten.AddUniqueDynamic(
		this,
		&UPerceptionKnowledgeListenerComponent::HandleTargetPerceptionForgotten);
	if (AController* Controller = Cast<AController>(GetOwner()))
	{
		Controller->OnPossessedPawnChanged.AddUniqueDynamic(
			this,
			&UPerceptionKnowledgeListenerComponent::HandlePossessedPawnChanged);
	}

	if (bListenerEnabled && Profile && LastRegistrationResult.Status != EPerceptionKnowledgeOperationStatus::InvalidArgument)
	{
		RegisterSemanticListener();
	}
	if (bObservationSuspended && bListenerEnabled && Profile)
	{
		PERCEPTIONKNOWLEDGE_LOG_WARNING(
			TEXT("Listener=%s Owner=%s registered in suspended mode: no valid Pawn/Body Actor."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
	}
	UpdateTimers();
}

void UPerceptionKnowledgeListenerComponent::OnUnregister()
{
	UnbindProfileChanges();
	if (AController* Controller = Cast<AController>(GetOwner()))
	{
		Controller->OnPossessedPawnChanged.RemoveDynamic(
			this,
			&UPerceptionKnowledgeListenerComponent::HandlePossessedPawnChanged);
	}
	OnTargetPerceptionUpdated.RemoveDynamic(
		this,
		&UPerceptionKnowledgeListenerComponent::HandleTargetPerceptionUpdated);
	OnTargetPerceptionForgotten.RemoveDynamic(
		this,
		&UPerceptionKnowledgeListenerComponent::HandleTargetPerceptionForgotten);

	ClearCurrentPerception();
	UnregisterSemanticListener();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecentEventCleanupTimerHandle);
		World->GetTimerManager().ClearTimer(VisibleStateRefreshTimerHandle);
		World->GetTimerManager().ClearTimer(DebugTimerHandle);
	}
	Super::OnUnregister();
}

void UPerceptionKnowledgeListenerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindProfileChanges();
	ClearCurrentPerception();
	UnregisterSemanticListener();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecentEventCleanupTimerHandle);
		World->GetTimerManager().ClearTimer(VisibleStateRefreshTimerHandle);
		World->GetTimerManager().ClearTimer(DebugTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UPerceptionKnowledgeListenerComponent::BindProfileChanges()
{
	if (!Profile || ProfileChangedHandle.IsValid())
	{
		return;
	}

	ProfileChangedHandle =
		Profile->OnProfileChangedNative().AddUObject(
			this,
			&ThisClass::HandleProfileChanged);
}

void UPerceptionKnowledgeListenerComponent::UnbindProfileChanges()
{
	if (Profile && ProfileChangedHandle.IsValid())
	{
		Profile->OnProfileChangedNative().Remove(ProfileChangedHandle);
	}
	ProfileChangedHandle.Reset();
}

void UPerceptionKnowledgeListenerComponent::HandleProfileChanged()
{
	ApplyProfileRuntimeConfiguration();
}

void UPerceptionKnowledgeListenerComponent::ApplyProfileRuntimeConfiguration()
{
	ApplyProfileBeforeRegistration();
	const bool bCanObserve =
		bListenerEnabled
		&& Profile
		&& SightConfig
		&& HearingConfig
		&& LastRegistrationResult.Status
			!= EPerceptionKnowledgeOperationStatus::InvalidArgument;

	if (!bCanObserve)
	{
		ClearCurrentPerception();
		ForgetAll();
		if (IsRegistered())
		{
			SetSenseEnabled(UAISense_Sight::StaticClass(), false);
			SetSenseEnabled(UAISense_Hearing::StaticClass(), false);
			RequestStimuliListenerUpdate();
		}
		UnregisterSemanticListener();
		UpdateTimers();
		ListenerConfigurationChangedNative.Broadcast();
		return;
	}

	if (IsRegistered())
	{
		ConfigureSense(*SightConfig);
		ConfigureSense(*HearingConfig);
		SetSenseEnabled(
			UAISense_Sight::StaticClass(),
			Profile->bEnableSight);
		SetSenseEnabled(
			UAISense_Hearing::StaticClass(),
			Profile->bEnableHearing);
	}
	if (IsRegistered())
	{
		RegisterSemanticListener();
	}
	if (IsRegistered() && !bObservationSuspended)
	{
		RequestStimuliListenerUpdate();
	}
	UpdateTimers();
	ListenerConfigurationChangedNative.Broadcast();

	PERCEPTIONKNOWLEDGE_LOG_INFO(
		TEXT("Listener=%s Owner=%s applied runtime Profile=%s: Sight=%s %.0f/%.0f, Hearing=%s %.0f."),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(Profile),
		Profile->bEnableSight ? TEXT("enabled") : TEXT("disabled"),
		GetEffectiveSightRadius(),
		GetEffectiveLoseSightRadius(),
		Profile->bEnableHearing ? TEXT("enabled") : TEXT("disabled"),
		GetEffectiveHearingRange());
}

bool UPerceptionKnowledgeListenerComponent::HasValidObservationBody() const
{
	return IsValid(GetBodyActor());
}

void UPerceptionKnowledgeListenerComponent::RegisterSemanticListener()
{
	if (bSemanticRegistered)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UPerceptionKnowledgeWorldSubsystem* Subsystem =
			World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>())
		{
			Subsystem->RegisterListener(this);
			bSemanticRegistered = true;
			LastRegistrationResult = MakeListenerResult(
				EPerceptionKnowledgeOperationStatus::Success,
				bObservationSuspended
					? TEXT("Listener registered; observations remain suspended until a valid Body Actor exists.")
					: TEXT("Listener registered and ready."));
		}
	}
}

void UPerceptionKnowledgeListenerComponent::UnregisterSemanticListener()
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
			Subsystem->UnregisterListener(this);
		}
	}
	bSemanticRegistered = false;
}

void UPerceptionKnowledgeListenerComponent::HandleTargetPerceptionUpdated(
	AActor* Actor,
	FAIStimulus Stimulus)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PerceptionKnowledge_HandleNativeStimulus);
	if (bObservationSuspended || !bListenerEnabled || !Profile || !Actor)
	{
		return;
	}

	const FAISenseID SightId = UAISense::GetSenseID<UAISense_Sight>();
	const FAISenseID HearingId = UAISense::GetSenseID<UAISense_Hearing>();
	UWorld* World = GetWorld();
	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		World ? World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	if (Stimulus.Type == SightId)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PerceptionKnowledge_ProcessSightStimulus);
		UPerceptionKnowledgeSourceComponent* Source =
			Actor->FindComponentByClass<UPerceptionKnowledgeSourceComponent>();
		if (!Source || !Source->IsSemanticallyRegistered())
		{
			return;
		}
		if (Stimulus.WasSuccessfullySensed())
		{
			const bool bAcquisition = !IsEntityCurrentlyPerceived(
				Source->GetEntityId(),
				TAG_PerceptionKnowledge_Sense_Sight);
			SetPerceptionRelationship(Source, TAG_PerceptionKnowledge_Sense_Sight, true);
			RefreshSourceStates(
				Source,
				TAG_PerceptionKnowledge_Sense_Sight,
				FMath::Clamp(Stimulus.Strength, 0.0f, 1.0f),
				Stimulus.StimulusLocation,
				bAcquisition);
		}
		else
		{
			SetPerceptionRelationship(Source, TAG_PerceptionKnowledge_Sense_Sight, false);
		}
		return;
	}

	if (Stimulus.Type == HearingId && Stimulus.WasSuccessfullySensed())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PerceptionKnowledge_ProcessHearingStimulus);
		FPerceptionKnowledgeEventObservation Event;
		if (!Subsystem->ResolveSemanticNoise(
			Stimulus.Tag,
			Actor,
			Stimulus.StimulusLocation,
			Event))
		{
			FCorrelationFailureEntry Failure;
			Failure.Location = Stimulus.StimulusLocation;
			Failure.ExpirationWorldTime = (World ? World->GetTimeSeconds() : 0.0) + 2.0;
			CorrelationFailures.Add(Failure);
			UpdateTimers();
			return;
		}

		Event.Confidence = FMath::Clamp(Stimulus.Strength, 0.0f, 1.0f);
		Event.WorldTimestamp = World ? World->GetTimeSeconds() : Event.WorldTimestamp;
		if (UPerceptionKnowledgeSourceComponent* Source = Subsystem->FindSource(Event.SourceEntityId))
		{
			SetPerceptionRelationship(Source, TAG_PerceptionKnowledge_Sense_Hearing, true);
		}
		ReceiveEventObservation(Event);
	}
}

void UPerceptionKnowledgeListenerComponent::HandleTargetPerceptionForgotten(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}
	if (UPerceptionKnowledgeSourceComponent* Source =
		Actor->FindComponentByClass<UPerceptionKnowledgeSourceComponent>())
	{
		SetPerceptionRelationship(Source, TAG_PerceptionKnowledge_Sense_Sight, false);
		SetPerceptionRelationship(Source, TAG_PerceptionKnowledge_Sense_Hearing, false);
	}
}

void UPerceptionKnowledgeListenerComponent::HandlePossessedPawnChanged(
	APawn* OldPawn,
	APawn* NewPawn)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PerceptionKnowledge_HandlePossessionChanged);
	ClearCurrentPerception();
	ForgetAll();

	bObservationSuspended = !bListenerEnabled || !Profile || !IsValid(NewPawn);
	if (!bObservationSuspended)
	{
		RequestStimuliListenerUpdate();
		PERCEPTIONKNOWLEDGE_LOG_INFO(
			TEXT("Listener=%s Owner=%s changed Body Actor from %s to %s; native perception reset and knowledge preserved at revision %lld."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(OldPawn),
			*GetNameSafe(NewPawn),
			KnowledgeRevision);
	}
	else
	{
		PERCEPTIONKNOWLEDGE_LOG_WARNING(
			TEXT("Listener=%s Owner=%s has no possessed Pawn; observation production is suspended while knowledge revision %lld is preserved."),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()),
			KnowledgeRevision);
	}
	UpdateTimers();
	ListenerConfigurationChangedNative.Broadcast();
}

void UPerceptionKnowledgeListenerComponent::RefreshSourceStates(
	UPerceptionKnowledgeSourceComponent* Source,
	const FGameplayTag SenseTag,
	const float Confidence,
	const FVector& ObservationLocation,
	const bool bAcquisition)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PerceptionKnowledge_RefreshVisibleSourceStates);
	if (bObservationSuspended || !Source || !SenseTag.IsValid())
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	TArray<FPerceptionKnowledgeExposedState> States;
	const FPerceptionKnowledgeOperationResult GatherResult =
		Source->GetObservableStatesForSense(this, SenseTag, States);
	if (!GatherResult.IsSuccess())
	{
		PERCEPTIONKNOWLEDGE_LOG_WARNING(
			TEXT("State refresh failed Listener=%s Source=%s EntityId=%s Sense=%s: %s"),
			*GetNameSafe(this),
			*GetNameSafe(Source),
			*Source->GetEntityId().ToString(),
			*SenseTag.ToString(),
			*GatherResult.Message);
		return;
	}

	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (const FPerceptionKnowledgeExposedState& State : States)
	{
		FPerceptionKnowledgeStateObservation Observation;
		Observation.Key.EntityId = Source->GetEntityId();
		Observation.Key.StateTag = State.StateTag;
		Observation.Value = State.Value;
		Observation.Status = State.Status;
		Observation.SenseTag = SenseTag;
		Observation.Confidence = FMath::Clamp(Confidence, 0.0f, 1.0f);
		Observation.WorldTimestamp = CurrentTime;
		Observation.ObservationLocation = ObservationLocation;
		StoreStateObservation(Observation, bAcquisition);
	}

	if (UWorld* World = GetWorld())
	{
		if (UPerceptionKnowledgeWorldSubsystem* Subsystem =
			World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>())
		{
			Subsystem->RecordVisibleRefresh((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		}
	}
}

void UPerceptionKnowledgeListenerComponent::HandleSourceStateChanged(
	UPerceptionKnowledgeSourceComponent* Source,
	const FPerceptionKnowledgeExposedState& State,
	const bool bRemoved)
{
	if (bObservationSuspended || !Source)
	{
		return;
	}

	FGameplayTag ActiveSense;
	for (const FGameplayTag& SenseTag : State.ObservableThroughSenses)
	{
		if (IsEntityCurrentlyPerceived(Source->GetEntityId(), SenseTag))
		{
			ActiveSense = SenseTag;
			break;
		}
	}
	if (!ActiveSense.IsValid())
	{
		return;
	}

	FPerceptionKnowledgeStateObservation Observation;
	Observation.Key.EntityId = Source->GetEntityId();
	Observation.Key.StateTag = State.StateTag;
	Observation.Value = State.Value;
	Observation.Status = bRemoved
		? EPerceptionKnowledgeFactStatus::Invalidated
		: State.Status;
	Observation.SenseTag = ActiveSense;
	Observation.Confidence = 1.0f;
	Observation.WorldTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Observation.ObservationLocation =
		Source->GetOwner() ? Source->GetOwner()->GetActorLocation() : FVector::ZeroVector;
	StoreStateObservation(Observation, false);
}

void UPerceptionKnowledgeListenerComponent::HandleSourceUnregistered(
	UPerceptionKnowledgeSourceComponent* Source)
{
	if (!Source)
	{
		return;
	}
	const FPerceptionKnowledgeEntityId EntityId = Source->GetEntityId();
	FGameplayTagContainer PreviousSenses;
	if (!CurrentlyPerceivedSenses.RemoveAndCopyValue(EntityId, PreviousSenses))
	{
		return;
	}
	for (const FGameplayTag& SenseTag : PreviousSenses)
	{
		OnEntityPerceptionChanged.Broadcast(EntityId, SenseTag, false);
		EntityPerceptionChangedNative.Broadcast(EntityId, SenseTag, false);
	}
}

bool UPerceptionKnowledgeListenerComponent::StoreStateObservation(
	const FPerceptionKnowledgeStateObservation& Observation,
	const bool bAcquisition)
{
	if (!Observation.Key.EntityId.IsValid()
		|| !Observation.Key.StateTag.IsValid()
		|| !Observation.SenseTag.IsValid()
		|| (Observation.Status == EPerceptionKnowledgeFactStatus::Known && !Observation.Value.IsValid()))
	{
		PERCEPTIONKNOWLEDGE_LOG_WARNING(
			TEXT("Rejected invalid state observation Listener=%s EntityId=%s StateTag=%s Sense=%s."),
			*GetNameSafe(this),
			*Observation.Key.EntityId.ToString(),
			*Observation.Key.StateTag.ToString(),
			*Observation.SenseTag.ToString());
		return false;
	}

	FPerceptionKnowledgeKnownState* Existing = KnownStates.Find(Observation.Key);
	if (Existing
		&& Existing->Value.GetType() != EPerceptionKnowledgeValueType::None
		&& Observation.Value.GetType() != EPerceptionKnowledgeValueType::None
		&& Existing->Value.GetType() != Observation.Value.GetType())
	{
		PERCEPTIONKNOWLEDGE_LOG_ERROR(
			TEXT("Rejected state type change Listener=%s EntityId=%s StateTag=%s ExistingType=%d IncomingType=%d."),
			*GetNameSafe(this),
			*Observation.Key.EntityId.ToString(),
			*Observation.Key.StateTag.ToString(),
			static_cast<int32>(Existing->Value.GetType()),
			static_cast<int32>(Observation.Value.GetType()));
		return false;
	}

	const bool bFirstLearning = Existing == nullptr;
	const bool bSemanticChange = bFirstLearning || !IsSameSemanticState(*Existing, Observation);
	FPerceptionKnowledgeKnownState Previous;
	if (Existing)
	{
		Previous = *Existing;
	}

	FPerceptionKnowledgeKnownState Current = Existing
		? *Existing
		: FPerceptionKnowledgeKnownState();
	Current.Key = Observation.Key;
	Current.Value = Observation.Value;
	Current.Status = Observation.Status;
	Current.SourceSenseTag = Observation.SenseTag;
	Current.Confidence = Observation.Confidence;
	Current.LastObservedWorldTime = Observation.WorldTimestamp;
	Current.LastObservationLocation = Observation.ObservationLocation;
	if (bFirstLearning)
	{
		Current.FactRevision = 1;
	}
	else if (bSemanticChange)
	{
		++Current.FactRevision;
	}

	++KnowledgeRevision;
	Current.KnowledgeRevision = KnowledgeRevision;
	KnownStates.Add(Observation.Key, Current);

	bool bBroadcastObservation = true;
	if (Profile)
	{
		switch (Profile->RepeatedObservationPolicy)
		{
		case EPerceptionKnowledgeRepeatedObservationPolicy::Always:
			break;
		case EPerceptionKnowledgeRepeatedObservationPolicy::AcquisitionsAndChanges:
			bBroadcastObservation = bAcquisition || bSemanticChange;
			break;
		case EPerceptionKnowledgeRepeatedObservationPolicy::ChangesOnly:
			bBroadcastObservation = bSemanticChange;
			break;
		default:
			break;
		}
	}

	if (bBroadcastObservation)
	{
		BroadcastObservation(FPerceptionKnowledgeObservation::FromState(Observation));
	}
	else if (UWorld* World = GetWorld())
	{
		if (UPerceptionKnowledgeWorldSubsystem* Subsystem =
			World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>())
		{
			Subsystem->RecordDiscardedDuplicate();
		}
	}

	if (bSemanticChange)
	{
		OnKnownStateChanged.Broadcast(Previous, Current);
		KnownStateChangedNative.Broadcast(Previous, Current);
		if (Current.Status == EPerceptionKnowledgeFactStatus::Invalidated)
		{
			OnKnownStateInvalidated.Broadcast(Current);
			KnownStateInvalidatedNative.Broadcast(Current);
		}
	}
	return true;
}

void UPerceptionKnowledgeListenerComponent::ReceiveEventObservation(
	const FPerceptionKnowledgeEventObservation& Event)
{
	if (bObservationSuspended || !Event.ObservationId.IsValid()
		|| !Event.EventTag.IsValid() || !Event.SenseTag.IsValid())
	{
		return;
	}

	const float Lifetime = Profile ? FMath::Max(0.1f, Profile->RecentEventLifetime) : 5.0f;
	const int32 Capacity = Profile ? FMath::Max(1, Profile->MaxRecentEvents) : 128;
	FRecentEventEntry Entry;
	Entry.Observation = Event;
	Entry.ExpirationWorldTime = (GetWorld() ? GetWorld()->GetTimeSeconds() : Event.WorldTimestamp)
		+ Lifetime;
	RecentEvents.Add(MoveTemp(Entry));
	if (RecentEvents.Num() > Capacity)
	{
		RecentEvents.RemoveAt(0, RecentEvents.Num() - Capacity, EAllowShrinking::No);
	}

	BroadcastObservation(FPerceptionKnowledgeObservation::FromEvent(Event));
	OnRecentEventAdded.Broadcast(Event);
	RecentEventAddedNative.Broadcast(Event);
	UpdateTimers();
}

void UPerceptionKnowledgeListenerComponent::BroadcastObservation(
	const FPerceptionKnowledgeObservation& Observation)
{
	OnObservationProduced.Broadcast(Observation);
	ObservationProducedNative.Broadcast(Observation);
	if (UWorld* World = GetWorld())
	{
		if (UPerceptionKnowledgeWorldSubsystem* Subsystem =
			World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>())
		{
			Subsystem->RecordProducedObservation();
		}
	}
}

void UPerceptionKnowledgeListenerComponent::SetPerceptionRelationship(
	UPerceptionKnowledgeSourceComponent* Source,
	const FGameplayTag SenseTag,
	const bool bPerceived)
{
	if (!Source || !SenseTag.IsValid())
	{
		return;
	}
	const FPerceptionKnowledgeEntityId EntityId = Source->GetEntityId();
	if (!EntityId.IsValid())
	{
		return;
	}

	FGameplayTagContainer& CurrentSenses = CurrentlyPerceivedSenses.FindOrAdd(EntityId);
	const bool bWasPerceived = CurrentSenses.HasTagExact(SenseTag);
	if (bWasPerceived == bPerceived)
	{
		return;
	}

	if (bPerceived)
	{
		CurrentSenses.AddTag(SenseTag);
	}
	else
	{
		CurrentSenses.RemoveTag(SenseTag);
		if (CurrentSenses.IsEmpty())
		{
			CurrentlyPerceivedSenses.Remove(EntityId);
		}
	}

	if (UWorld* World = GetWorld())
	{
		if (UPerceptionKnowledgeWorldSubsystem* Subsystem =
			World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>())
		{
			Subsystem->UpdatePerceptionRelationship(Source, this, SenseTag, bPerceived);
		}
	}
	OnEntityPerceptionChanged.Broadcast(EntityId, SenseTag, bPerceived);
	EntityPerceptionChangedNative.Broadcast(EntityId, SenseTag, bPerceived);
}

void UPerceptionKnowledgeListenerComponent::ClearCurrentPerception()
{
	TArray<TPair<FPerceptionKnowledgeEntityId, FGameplayTag>> Relationships;
	for (const TPair<FPerceptionKnowledgeEntityId, FGameplayTagContainer>& EntityPair :
		CurrentlyPerceivedSenses)
	{
		for (const FGameplayTag& SenseTag : EntityPair.Value)
		{
			Relationships.Emplace(EntityPair.Key, SenseTag);
		}
	}

	UPerceptionKnowledgeWorldSubsystem* Subsystem = GetWorld()
		? GetWorld()->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>()
		: nullptr;
	for (const TPair<FPerceptionKnowledgeEntityId, FGameplayTag>& Relationship : Relationships)
	{
		if (UPerceptionKnowledgeSourceComponent* Source =
			Subsystem ? Subsystem->FindSource(Relationship.Key) : nullptr)
		{
			SetPerceptionRelationship(Source, Relationship.Value, false);
		}
		else
		{
			OnEntityPerceptionChanged.Broadcast(Relationship.Key, Relationship.Value, false);
			EntityPerceptionChangedNative.Broadcast(Relationship.Key, Relationship.Value, false);
		}
	}
	CurrentlyPerceivedSenses.Reset();
}

bool UPerceptionKnowledgeListenerComponent::GetKnownState(
	const FPerceptionKnowledgeEntityId EntityId,
	const FGameplayTag StateTag,
	FPerceptionKnowledgeKnownState& OutState) const
{
	FPerceptionKnowledgeStateKey Key;
	Key.EntityId = EntityId;
	Key.StateTag = StateTag;
	if (const FPerceptionKnowledgeKnownState* State = KnownStates.Find(Key))
	{
		OutState = *State;
		return true;
	}
	OutState = FPerceptionKnowledgeKnownState();
	return false;
}

TArray<FPerceptionKnowledgeKnownState>
UPerceptionKnowledgeListenerComponent::GetKnownStatesForEntity(
	const FPerceptionKnowledgeEntityId EntityId) const
{
	TArray<FPerceptionKnowledgeKnownState> Result;
	for (const TPair<FPerceptionKnowledgeStateKey, FPerceptionKnowledgeKnownState>& Pair : KnownStates)
	{
		if (Pair.Key.EntityId == EntityId)
		{
			Result.Add(Pair.Value);
		}
	}
	Result.Sort([](const FPerceptionKnowledgeKnownState& Left, const FPerceptionKnowledgeKnownState& Right)
	{
		return Left.Key.StateTag.ToString() < Right.Key.StateTag.ToString();
	});
	return Result;
}

TArray<FPerceptionKnowledgeEventObservation>
UPerceptionKnowledgeListenerComponent::GetRecentEvents() const
{
	TArray<FPerceptionKnowledgeEventObservation> Result;
	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (const FRecentEventEntry& Entry : RecentEvents)
	{
		if (Entry.ExpirationWorldTime > CurrentTime)
		{
			Result.Add(Entry.Observation);
		}
	}
	return Result;
}

FPerceptionKnowledgeSnapshot UPerceptionKnowledgeListenerComponent::BuildKnowledgeSnapshot(
	const FPerceptionKnowledgeSnapshotFilter& Filter) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PerceptionKnowledge_BuildKnowledgeSnapshot);
	FPerceptionKnowledgeSnapshot Snapshot;
	Snapshot.KnowledgeRevision = KnowledgeRevision;
	Snapshot.BuiltAtWorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	for (const TPair<FPerceptionKnowledgeStateKey, FPerceptionKnowledgeKnownState>& Pair : KnownStates)
	{
		const FPerceptionKnowledgeKnownState& State = Pair.Value;
		if (!Filter.EntityIds.IsEmpty() && !Filter.EntityIds.Contains(State.Key.EntityId))
		{
			continue;
		}
		if (!Filter.StateTags.IsEmpty() && !Filter.StateTags.HasTagExact(State.Key.StateTag))
		{
			continue;
		}
		if (!Filter.SenseTags.IsEmpty() && !Filter.SenseTags.HasTagExact(State.SourceSenseTag))
		{
			continue;
		}
		if (Filter.MaxAgeSeconds >= 0.0
			&& Snapshot.BuiltAtWorldTime - State.LastObservedWorldTime > Filter.MaxAgeSeconds)
		{
			continue;
		}
		Snapshot.States.Add(State);
	}
	Snapshot.States.Sort(
		[](const FPerceptionKnowledgeKnownState& Left, const FPerceptionKnowledgeKnownState& Right)
		{
			if (Left.Key.EntityId != Right.Key.EntityId)
			{
				return Left.Key.EntityId.ToString() < Right.Key.EntityId.ToString();
			}
			return Left.Key.StateTag.ToString() < Right.Key.StateTag.ToString();
		});
	return Snapshot;
}

bool UPerceptionKnowledgeListenerComponent::IsEntityCurrentlyPerceived(
	const FPerceptionKnowledgeEntityId EntityId,
	const FGameplayTag SenseTag) const
{
	const FGameplayTagContainer* Senses = CurrentlyPerceivedSenses.Find(EntityId);
	return Senses && Senses->HasTagExact(SenseTag);
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeListenerComponent::ForgetEntity(
	const FPerceptionKnowledgeEntityId EntityId)
{
	if (!EntityId.IsValid())
	{
		return MakeListenerResult(
			EPerceptionKnowledgeOperationStatus::InvalidEntityId,
			TEXT("A valid Entity ID is required."));
	}

	TArray<FPerceptionKnowledgeKnownState> RemovedStates;
	for (auto Iterator = KnownStates.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator.Key().EntityId == EntityId)
		{
			FPerceptionKnowledgeKnownState Invalidated = Iterator.Value();
			Invalidated.Status = EPerceptionKnowledgeFactStatus::Invalidated;
			RemovedStates.Add(Invalidated);
			Iterator.RemoveCurrent();
		}
	}
	if (RemovedStates.IsEmpty())
	{
		return MakeListenerResult(
			EPerceptionKnowledgeOperationStatus::Unchanged,
			TEXT("No knowledge was stored for this Entity ID."),
			EntityId);
	}

	++KnowledgeRevision;
	for (const FPerceptionKnowledgeKnownState& State : RemovedStates)
	{
		OnKnownStateInvalidated.Broadcast(State);
		KnownStateInvalidatedNative.Broadcast(State);
	}
	return MakeListenerResult(
		EPerceptionKnowledgeOperationStatus::Success,
		FString::Printf(TEXT("Forgot %d state(s) for the Entity ID."), RemovedStates.Num()),
		EntityId);
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeListenerComponent::InvalidateKnownState(
	const FPerceptionKnowledgeEntityId EntityId,
	const FGameplayTag StateTag)
{
	FPerceptionKnowledgeStateKey Key;
	Key.EntityId = EntityId;
	Key.StateTag = StateTag;
	const FPerceptionKnowledgeKnownState* Existing = KnownStates.Find(Key);
	if (!Existing)
	{
		return MakeListenerResult(
			EPerceptionKnowledgeOperationStatus::InvalidArgument,
			TEXT("The requested known state does not exist."),
			EntityId);
	}

	FPerceptionKnowledgeStateObservation Observation;
	Observation.Key = Key;
	Observation.Value = Existing->Value;
	Observation.Status = EPerceptionKnowledgeFactStatus::Invalidated;
	Observation.SenseTag = Existing->SourceSenseTag;
	Observation.Confidence = Existing->Confidence;
	Observation.WorldTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	Observation.ObservationLocation = Existing->LastObservationLocation;
	StoreStateObservation(Observation, false);
	return MakeListenerResult(
		EPerceptionKnowledgeOperationStatus::Success,
		TEXT("Known state invalidated."),
		EntityId);
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeListenerComponent::SetListenerEnabled(
	const bool bEnabled)
{
	if (bListenerEnabled == bEnabled)
	{
		return MakeListenerResult(
			EPerceptionKnowledgeOperationStatus::Unchanged,
			TEXT("Listener enabled state is unchanged."));
	}
	bListenerEnabled = bEnabled;

	if (!bEnabled)
	{
		ClearCurrentPerception();
		ForgetAll();
		bObservationSuspended = true;
		SetSenseEnabled(UAISense_Sight::StaticClass(), false);
		SetSenseEnabled(UAISense_Hearing::StaticClass(), false);
		UnregisterSemanticListener();
		LastRegistrationResult = MakeListenerResult(
			EPerceptionKnowledgeOperationStatus::Success,
			TEXT("Listener disabled; native perception cleared and knowledge preserved."));
		UpdateTimers();
		ListenerConfigurationChangedNative.Broadcast();
		return LastRegistrationResult;
	}

	ApplyProfileBeforeRegistration();
	if (!Profile)
	{
		ListenerConfigurationChangedNative.Broadcast();
		return LastRegistrationResult;
	}
	FString Error;
	if (!Profile->IsConfigurationValid(Error))
	{
		ListenerConfigurationChangedNative.Broadcast();
		return LastRegistrationResult;
	}
	if (IsRegistered())
	{
		ConfigureSense(*SightConfig);
		ConfigureSense(*HearingConfig);
		SetSenseEnabled(UAISense_Sight::StaticClass(), Profile->bEnableSight);
		SetSenseEnabled(UAISense_Hearing::StaticClass(), Profile->bEnableHearing);
	}
	bObservationSuspended = !HasValidObservationBody();
	RegisterSemanticListener();
	if (!bObservationSuspended)
	{
		RequestStimuliListenerUpdate();
	}
	UpdateTimers();
	ListenerConfigurationChangedNative.Broadcast();
	return LastRegistrationResult;
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeListenerComponent::SetListenerProfile(
	UPerceptionKnowledgeProfile* InProfile)
{
	UnbindProfileChanges();
	Profile = InProfile;
	BindProfileChanges();
	if (!Profile)
	{
		ClearCurrentPerception();
		ForgetAll();
		bObservationSuspended = true;
		if (SightConfig)
		{
			SightConfig->SetStartsEnabled(false);
		}
		if (HearingConfig)
		{
			HearingConfig->SetStartsEnabled(false);
		}
		if (IsRegistered())
		{
			SetSenseEnabled(UAISense_Sight::StaticClass(), false);
			SetSenseEnabled(UAISense_Hearing::StaticClass(), false);
		}
		UnregisterSemanticListener();
		LastRegistrationResult = MakeListenerResult(
			EPerceptionKnowledgeOperationStatus::MissingProfile,
			TEXT("Listener Profile cleared; observations are suspended."));
		UpdateTimers();
		ListenerConfigurationChangedNative.Broadcast();
		return LastRegistrationResult;
	}

	ApplyProfileRuntimeConfiguration();
	return LastRegistrationResult;
}

AActor* UPerceptionKnowledgeListenerComponent::GetResolvedBodyActor() const
{
	return const_cast<AActor*>(GetBodyActor());
}

float UPerceptionKnowledgeListenerComponent::GetEffectiveHearingRange() const
{
	return bListenerEnabled && HearingConfig && HearingConfig->GetStartsEnabled()
		? FMath::Max(0.0f, HearingConfig->HearingRange)
		: 0.0f;
}

float UPerceptionKnowledgeListenerComponent::GetEffectiveSightRadius() const
{
	return bListenerEnabled && SightConfig && SightConfig->GetStartsEnabled()
		? FMath::Max(0.0f, SightConfig->SightRadius)
		: 0.0f;
}

float UPerceptionKnowledgeListenerComponent::GetEffectiveLoseSightRadius() const
{
	return bListenerEnabled && SightConfig && SightConfig->GetStartsEnabled()
		? FMath::Max(0.0f, SightConfig->LoseSightRadius)
		: 0.0f;
}

bool UPerceptionKnowledgeListenerComponent::GetListenerViewpoint(
	FVector& OutLocation,
	FVector& OutDirection) const
{
	OutLocation = FVector::ZeroVector;
	OutDirection = FVector::ForwardVector;
	if (!HasValidObservationBody())
	{
		return false;
	}
	GetLocationAndDirection(OutLocation, OutDirection);
	return !OutLocation.ContainsNaN() && !OutDirection.ContainsNaN() && !OutDirection.IsNearlyZero();
}

void UPerceptionKnowledgeListenerComponent::CleanupRecentEvents()
{
	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (auto Iterator = RecentEvents.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator->ExpirationWorldTime <= CurrentTime)
		{
			Iterator.RemoveCurrent();
		}
	}
	for (auto Iterator = CorrelationFailures.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator->ExpirationWorldTime <= CurrentTime)
		{
			Iterator.RemoveCurrent();
		}
	}

	TSet<FPerceptionKnowledgeEntityId> ActiveHearingEntities;
	for (const FRecentEventEntry& Entry : RecentEvents)
	{
		if (Entry.Observation.SenseTag == TAG_PerceptionKnowledge_Sense_Hearing)
		{
			ActiveHearingEntities.Add(Entry.Observation.SourceEntityId);
		}
	}
	TArray<FPerceptionKnowledgeEntityId> HearingToClear;
	for (const TPair<FPerceptionKnowledgeEntityId, FGameplayTagContainer>& Pair :
		CurrentlyPerceivedSenses)
	{
		if (Pair.Value.HasTagExact(TAG_PerceptionKnowledge_Sense_Hearing)
			&& !ActiveHearingEntities.Contains(Pair.Key))
		{
			HearingToClear.Add(Pair.Key);
		}
	}
	UPerceptionKnowledgeWorldSubsystem* Subsystem = GetWorld()
		? GetWorld()->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>()
		: nullptr;
	for (const FPerceptionKnowledgeEntityId EntityId : HearingToClear)
	{
		if (UPerceptionKnowledgeSourceComponent* Source =
			Subsystem ? Subsystem->FindSource(EntityId) : nullptr)
		{
			SetPerceptionRelationship(Source, TAG_PerceptionKnowledge_Sense_Hearing, false);
		}
		else if (FGameplayTagContainer* Senses = CurrentlyPerceivedSenses.Find(EntityId))
		{
			Senses->RemoveTag(TAG_PerceptionKnowledge_Sense_Hearing);
			OnEntityPerceptionChanged.Broadcast(
				EntityId,
				TAG_PerceptionKnowledge_Sense_Hearing,
				false);
			EntityPerceptionChangedNative.Broadcast(
				EntityId,
				TAG_PerceptionKnowledge_Sense_Hearing,
				false);
			if (Senses->IsEmpty())
			{
				CurrentlyPerceivedSenses.Remove(EntityId);
			}
		}
	}
	UpdateTimers();
}

void UPerceptionKnowledgeListenerComponent::RefreshVisibleSources()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PerceptionKnowledge_OptionalVisibleRescan);
	if (bObservationSuspended)
	{
		return;
	}
	UWorld* World = GetWorld();
	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		World ? World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}

	TArray<FPerceptionKnowledgeEntityId> VisibleEntities;
	for (const TPair<FPerceptionKnowledgeEntityId, FGameplayTagContainer>& Pair :
		CurrentlyPerceivedSenses)
	{
		if (Pair.Value.HasTagExact(TAG_PerceptionKnowledge_Sense_Sight))
		{
			VisibleEntities.Add(Pair.Key);
		}
	}
	for (const FPerceptionKnowledgeEntityId EntityId : VisibleEntities)
	{
		if (UPerceptionKnowledgeSourceComponent* Source = Subsystem->FindSource(EntityId))
		{
			RefreshSourceStates(
				Source,
				TAG_PerceptionKnowledge_Sense_Sight,
				1.0f,
				Source->GetOwner()->GetActorLocation(),
				false);
		}
	}
}

void UPerceptionKnowledgeListenerComponent::UpdateTimers()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FTimerManager& TimerManager = World->GetTimerManager();

	const bool bNeedsCleanup = !RecentEvents.IsEmpty() || !CorrelationFailures.IsEmpty();
	if (bNeedsCleanup)
	{
		if (!TimerManager.IsTimerActive(RecentEventCleanupTimerHandle))
		{
			TimerManager.SetTimer(
				RecentEventCleanupTimerHandle,
				this,
				&UPerceptionKnowledgeListenerComponent::CleanupRecentEvents,
				0.25f,
				true);
		}
	}
	else
	{
		TimerManager.ClearTimer(RecentEventCleanupTimerHandle);
	}

	TimerManager.ClearTimer(VisibleStateRefreshTimerHandle);
	if (!bObservationSuspended && Profile && Profile->VisibleStateValidationInterval > 0.0f)
	{
		TimerManager.SetTimer(
			VisibleStateRefreshTimerHandle,
			this,
			&UPerceptionKnowledgeListenerComponent::RefreshVisibleSources,
			FMath::Max(0.1f, Profile->VisibleStateValidationInterval),
			true);
	}

	TimerManager.ClearTimer(DebugTimerHandle);
	if (bEnableDebug && IsRegistered())
	{
		const UPerceptionKnowledgeDeveloperSettings* Settings =
			GetDefault<UPerceptionKnowledgeDeveloperSettings>();
		TimerManager.SetTimer(
			DebugTimerHandle,
			this,
			&UPerceptionKnowledgeListenerComponent::DrawListenerDebug,
			FMath::Max(0.05f, Settings->DebugDrawInterval),
			true);
	}
}

void UPerceptionKnowledgeListenerComponent::SetDebugEnabled(const bool bEnabled)
{
	bEnableDebug = bEnabled;
	UpdateTimers();
}

FPerceptionKnowledgeDebugFrame UPerceptionKnowledgeListenerComponent::BuildDebugFrame() const
{
	FPerceptionKnowledgeDebugFrame Frame;
	if (!bEnableDebug || !IsPerceptionKnowledgeDebugEnabled())
	{
		return Frame;
	}

	UWorld* World = GetWorld();
	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		World ? World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>() : nullptr;
	if (!World || !Subsystem)
	{
		return Frame;
	}

	Frame.bShouldDraw = true;
	Frame.bExpensiveDataBuilt = true;
	Frame.bHasValidViewpoint = GetListenerViewpoint(Frame.ViewLocation, Frame.ViewDirection);
	Frame.ListenerColor = Frame.bHasValidViewpoint ? FColor::White : FColor::Magenta;
	if (const AActor* BodyActor = GetBodyActor())
	{
		Frame.BodyLocation = BodyActor->GetActorLocation();
	}

	TSet<FPerceptionKnowledgeEntityId> KnownEntities;
	for (const TPair<FPerceptionKnowledgeStateKey, FPerceptionKnowledgeKnownState>& Pair : KnownStates)
	{
		KnownEntities.Add(Pair.Key.EntityId);
	}

	for (UPerceptionKnowledgeSourceComponent* Source : Subsystem->GetRegisteredSources())
	{
		if (!Source || !Source->GetOwner())
		{
			continue;
		}
		FPerceptionKnowledgeDebugSourceFrame SourceFrame;
		SourceFrame.EntityId = Source->GetEntityId();
		if (DebugFilter.SourceFilter.IsValid()
			&& DebugFilter.SourceFilter != SourceFrame.EntityId)
		{
			continue;
		}
		SourceFrame.ActorName = Source->GetOwner()->GetName();
		Source->GetOwner()->GetActorBounds(
			false,
			SourceFrame.BoundsOrigin,
			SourceFrame.BoundsExtent);
		SourceFrame.bCurrentlySeen = IsEntityCurrentlyPerceived(
			SourceFrame.EntityId,
			TAG_PerceptionKnowledge_Sense_Sight);
		SourceFrame.bCurrentlyHeard = IsEntityCurrentlyPerceived(
			SourceFrame.EntityId,
			TAG_PerceptionKnowledge_Sense_Hearing);
		SourceFrame.bKnownNotPerceived = KnownEntities.Contains(SourceFrame.EntityId)
			&& !SourceFrame.bCurrentlySeen
			&& !SourceFrame.bCurrentlyHeard;
		if (SourceFrame.bKnownNotPerceived && !DebugFilter.bDrawKnownNotPerceived)
		{
			continue;
		}
		SourceFrame.ExposedStateCount = Source->GetExposedStateCount();

		SourceFrame.Color = FColor::Blue;
		if (!Source->IsSemanticallyRegistered() || !SourceFrame.EntityId.IsValid())
		{
			SourceFrame.Color = FColor::Magenta;
		}
		else if (SourceFrame.bCurrentlySeen)
		{
			SourceFrame.Color = FColor::Cyan;
		}
		else if (SourceFrame.bCurrentlyHeard)
		{
			SourceFrame.Color = FColor::Yellow;
		}
		else if (SourceFrame.bKnownNotPerceived)
		{
			SourceFrame.Color = FColor::Silver;
		}
		Frame.Sources.Add(MoveTemp(SourceFrame));
	}

	const double CurrentTime = World->GetTimeSeconds();
	for (const FRecentEventEntry& Entry : RecentEvents)
	{
		if (Entry.ExpirationWorldTime > CurrentTime)
		{
			Frame.RecentEvents.Add(Entry.Observation);
		}
	}
	for (const FCorrelationFailureEntry& Failure : CorrelationFailures)
	{
		if (Failure.ExpirationWorldTime > CurrentTime)
		{
			Frame.CorrelationFailureLocations.Add(Failure.Location);
		}
	}
	return Frame;
}

void UPerceptionKnowledgeListenerComponent::DrawListenerDebug()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PerceptionKnowledge_DrawDebug);
	const FPerceptionKnowledgeDebugFrame Frame = BuildDebugFrame();
	if (!Frame.bShouldDraw)
	{
		return;
	}
	UWorld* World = GetWorld();
	UPerceptionKnowledgeWorldSubsystem* Subsystem =
		World ? World->GetSubsystem<UPerceptionKnowledgeWorldSubsystem>() : nullptr;
	if (!World || !Subsystem)
	{
		return;
	}
	const double StartSeconds = FPlatformTime::Seconds();

	const FVector ViewLocation = Frame.ViewLocation;
	const FVector ViewDirection = Frame.ViewDirection;
	const bool bValidViewpoint = Frame.bHasValidViewpoint;
	const FColor ListenerColor = Frame.ListenerColor;
	const float EffectiveSightRadius = GetEffectiveSightRadius();
	const float EffectiveLoseSightRadius = GetEffectiveLoseSightRadius();
	const float EffectiveHearingRange = GetEffectiveHearingRange();
	const float EffectivePeripheralVisionHalfAngle =
		SightConfig && SightConfig->GetStartsEnabled()
			? SightConfig->PeripheralVisionAngleDegrees
			: 0.0f;
	if (bValidViewpoint)
	{
		DrawDebugPoint(World, ViewLocation, 12.0f, ListenerColor, false, 0.0f);
		DrawDebugDirectionalArrow(
			World,
			ViewLocation,
			ViewLocation + ViewDirection * 150.0,
			30.0f,
			ListenerColor,
			false,
			0.0f,
			0,
			2.0f);
		if (bListenerEnabled
			&& DebugFilter.bDrawSight
			&& SightConfig
			&& SightConfig->GetStartsEnabled())
		{
			DrawDebugCircle(
				World,
				ViewLocation,
				EffectiveSightRadius,
				48,
				FColor::Cyan,
				false,
				0.0f,
				0,
				1.0f,
				FVector::ForwardVector,
				FVector::RightVector,
				false);
			DrawDebugCircle(
				World,
				ViewLocation,
				EffectiveLoseSightRadius,
				48,
				FColor(0, 120, 160),
				false,
				0.0f,
				0,
				0.75f,
				FVector::ForwardVector,
				FVector::RightVector,
				false);
			const FVector LeftDirection =
				ViewDirection.RotateAngleAxis(
					-EffectivePeripheralVisionHalfAngle,
					FVector::UpVector);
			const FVector RightDirection =
				ViewDirection.RotateAngleAxis(
					EffectivePeripheralVisionHalfAngle,
					FVector::UpVector);
			DrawDebugLine(
				World,
				ViewLocation,
				ViewLocation + LeftDirection * EffectiveSightRadius,
				FColor::Cyan,
				false,
				0.0f,
				0,
				1.0f);
			DrawDebugLine(
				World,
				ViewLocation,
				ViewLocation + RightDirection * EffectiveSightRadius,
				FColor::Cyan,
				false,
				0.0f,
				0,
				1.0f);
		}
		if (bListenerEnabled
			&& DebugFilter.bDrawHearing
			&& HearingConfig
			&& HearingConfig->GetStartsEnabled())
		{
			DrawDebugCircle(
				World,
				ViewLocation,
				EffectiveHearingRange,
				48,
				FColor::Yellow,
				false,
				0.0f,
				0,
				1.0f,
				FVector::ForwardVector,
				FVector::RightVector,
				false);
		}
	}

	for (const FPerceptionKnowledgeDebugSourceFrame& SourceFrame : Frame.Sources)
	{
		const FPerceptionKnowledgeEntityId EntityId = SourceFrame.EntityId;
		const bool bSight = SourceFrame.bCurrentlySeen;
		const bool bHearing = SourceFrame.bCurrentlyHeard;
		const FColor Color = SourceFrame.Color;
		const FVector Origin = SourceFrame.BoundsOrigin;
		const FVector Extent = SourceFrame.BoundsExtent;
		if (DebugFilter.bDrawBounds)
		{
			DrawDebugBox(World, Origin, Extent, Color, false, 0.0f, 0, 1.5f);
		}
		if (bValidViewpoint && DebugFilter.bDrawLines)
		{
			DrawDebugLine(World, ViewLocation, Origin, Color, false, 0.0f, 0, 1.0f);
		}
		if (DebugFilter.bDrawLabels)
		{
			const TArray<FPerceptionKnowledgeKnownState> States =
				GetKnownStatesForEntity(EntityId);
			FString StateText;
			if (DebugFilter.bDrawStates)
			{
				const int32 Count = FMath::Min(DebugFilter.MaxStatesPerSource, States.Num());
				for (int32 Index = 0; Index < Count; ++Index)
				{
					const double Age = FMath::Max(
						0.0,
						World->GetTimeSeconds() - States[Index].LastObservedWorldTime);
					StateText += FString::Printf(
						TEXT("\n%s=%s [%d] %s c%.2f age%.2fs r%lld/k%lld"),
						*States[Index].Key.StateTag.ToString(),
						*States[Index].Value.ToString(),
						static_cast<int32>(States[Index].Status),
						*States[Index].SourceSenseTag.ToString(),
						States[Index].Confidence,
						Age,
						States[Index].FactRevision,
						States[Index].KnowledgeRevision);
				}
			}
			DrawDebugString(
				World,
				Origin + FVector(0.0, 0.0, Extent.Z + 25.0),
				FString::Printf(
					TEXT("%s %s Sight=%s Hearing=%s States=%d%s"),
					*SourceFrame.ActorName,
					*EntityId.ToShortString(),
					bSight ? TEXT("yes") : TEXT("no"),
					bHearing ? TEXT("yes") : TEXT("no"),
					SourceFrame.ExposedStateCount,
					*StateText),
				nullptr,
				Color,
				0.0f,
				false,
				1.0f);
		}
	}

	if (DebugFilter.bDrawEvents)
	{
		for (const FPerceptionKnowledgeEventObservation& Event : Frame.RecentEvents)
		{
			if (Event.SenseTag != TAG_PerceptionKnowledge_Sense_Hearing)
			{
				continue;
			}
			DrawDebugSphere(
				World,
				Event.WorldLocation,
				20.0f,
				12,
				FColor::Yellow,
				false,
				0.0f,
				0,
				1.5f);
			DrawDebugString(
				World,
				Event.WorldLocation + FVector(0.0, 0.0, 25.0),
				FString::Printf(
					TEXT("%s\nSource=%s Instigator=%s\nLoud=%.2f Strength=%.2f Age=%.2fs Correlated=yes"),
					*Event.EventTag.ToString(),
					*Event.SourceEntityId.ToShortString(),
					*Event.InstigatorEntityId.ToShortString(),
					Event.Loudness,
					Event.Strength,
					FMath::Max(0.0, World->GetTimeSeconds() - Event.WorldTimestamp)),
				nullptr,
				FColor::Yellow,
				0.0f,
				false,
				1.0f);
			if (bValidViewpoint && DebugFilter.bDrawLines)
			{
				DrawDebugLine(
					World,
					ViewLocation,
					Event.WorldLocation,
					FColor::Yellow,
					false,
					0.0f,
					0,
					1.0f);
			}
		}
		for (const FVector& FailureLocation : Frame.CorrelationFailureLocations)
		{
			DrawDebugSphere(
				World,
				FailureLocation,
				35.0f,
				12,
				FColor::Magenta,
				false,
				0.0f,
				0,
				3.0f);
		}
	}

	DrawDebugString(
		World,
		bValidViewpoint ? ViewLocation + FVector(0.0, 0.0, 50.0) : GetOwner()->GetActorLocation(),
		FString::Printf(
			TEXT("PK Listener %s Body=%s Suspended=%s Rev=%lld States=%d Events=%d Sight=%.0f/Lose=%.0f FOV=%.0f Hearing=%.0f"),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(GetResolvedBodyActor()),
			bObservationSuspended ? TEXT("yes") : TEXT("no"),
			KnowledgeRevision,
			KnownStates.Num(),
			RecentEvents.Num(),
			EffectiveSightRadius,
			EffectiveLoseSightRadius,
			EffectivePeripheralVisionHalfAngle,
			EffectiveHearingRange),
		nullptr,
		ListenerColor,
		0.0f,
		false,
		1.0f);

	Subsystem->RecordDebugDraw((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

void UPerceptionKnowledgeListenerComponent::DumpKnowledgeToLog() const
{
	PERCEPTIONKNOWLEDGE_LOG_INFO(
		TEXT("Listener dump Component=%s Owner=%s Body=%s Profile=%s Enabled=%s Suspended=%s SemanticRegistered=%s Revision=%lld States=%d Events=%d PerceivedEntities=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(GetResolvedBodyActor()),
		*GetNameSafe(Profile),
		bListenerEnabled ? TEXT("true") : TEXT("false"),
		bObservationSuspended ? TEXT("true") : TEXT("false"),
		bSemanticRegistered ? TEXT("true") : TEXT("false"),
		KnowledgeRevision,
		KnownStates.Num(),
		RecentEvents.Num(),
		CurrentlyPerceivedSenses.Num());

	for (const TPair<FPerceptionKnowledgeStateKey, FPerceptionKnowledgeKnownState>& Pair : KnownStates)
	{
		PERCEPTIONKNOWLEDGE_LOG_INFO(
			TEXT("  State EntityId=%s Tag=%s Status=%d Value=%s Sense=%s Confidence=%.3f Time=%.3f FactRevision=%lld"),
			*Pair.Key.EntityId.ToString(),
			*Pair.Key.StateTag.ToString(),
			static_cast<int32>(Pair.Value.Status),
			*Pair.Value.Value.ToString(),
			*Pair.Value.SourceSenseTag.ToString(),
			Pair.Value.Confidence,
			Pair.Value.LastObservedWorldTime,
			Pair.Value.FactRevision);
	}
	for (const FRecentEventEntry& Entry : RecentEvents)
	{
		PERCEPTIONKNOWLEDGE_LOG_INFO(
			TEXT("  Event ObservationId=%s Event=%s Source=%s Instigator=%s Sense=%s Location=%s Strength=%.3f Loudness=%.3f Confidence=%.3f"),
			*Entry.Observation.ObservationId.ToString(),
			*Entry.Observation.EventTag.ToString(),
			*Entry.Observation.SourceEntityId.ToString(),
			*Entry.Observation.InstigatorEntityId.ToString(),
			*Entry.Observation.SenseTag.ToString(),
			*Entry.Observation.WorldLocation.ToCompactString(),
			Entry.Observation.Strength,
			Entry.Observation.Loudness,
			Entry.Observation.Confidence);
	}
}

#undef TAG_PerceptionKnowledge_Sense_Sight
#undef TAG_PerceptionKnowledge_Sense_Hearing
