#include "Subsystems/PerceptionKnowledgeWorldSubsystem.h"

#include "Components/PerceptionKnowledgeListenerComponent.h"
#include "Components/PerceptionKnowledgeSourceComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PerceptionKnowledgeModule.h"
#include "PerceptionKnowledgeTags.h"
#include "Settings/PerceptionKnowledgeDeveloperSettings.h"
#include "TimerManager.h"

#define TAG_PerceptionKnowledge_Sense_Hearing (PerceptionKnowledgeTags::Sense_Hearing.GetTag())

namespace
{
	struct FPendingSemanticNoise
	{
		FPerceptionKnowledgeEventObservation Event;
		TWeakObjectPtr<AActor> NativeInstigator;
		FVector Location = FVector::ZeroVector;
		double ExpirationWorldTime = 0.0;
	};

	FPerceptionKnowledgeOperationResult MakeResult(
		const EPerceptionKnowledgeOperationStatus Status,
		const FString& Message,
		const FPerceptionKnowledgeEntityId EntityId = FPerceptionKnowledgeEntityId(),
		const FGuid ObservationId = FGuid())
	{
		FPerceptionKnowledgeOperationResult Result;
		Result.Status = Status;
		Result.EntityId = EntityId;
		Result.ObservationId = ObservationId;
		Result.Message = Message;
		return Result;
	}
}

struct FPerceptionKnowledgeSubsystemRuntime
{
	TMap<FPerceptionKnowledgeEntityId, TWeakObjectPtr<UPerceptionKnowledgeSourceComponent>> Sources;
	TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>> Listeners;
	TMap<
		TWeakObjectPtr<UPerceptionKnowledgeSourceComponent>,
		TMap<FGameplayTag, TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>>>> Relationships;
	TMap<FName, FPendingSemanticNoise> PendingSemanticNoises;
	FTimerHandle SemanticNoiseCleanupTimerHandle;
	FPerceptionKnowledgeRuntimeStats Stats;
	int32 NextCorrelationNumber = 1;
	bool bShuttingDown = false;
};

UPerceptionKnowledgeWorldSubsystem::UPerceptionKnowledgeWorldSubsystem()
	: Runtime(new FPerceptionKnowledgeSubsystemRuntime())
{
}

UPerceptionKnowledgeWorldSubsystem::~UPerceptionKnowledgeWorldSubsystem()
{
	delete Runtime;
	Runtime = nullptr;
}

void UPerceptionKnowledgeWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!Runtime)
	{
		Runtime = new FPerceptionKnowledgeSubsystemRuntime();
	}
	Runtime->bShuttingDown = false;
}

void UPerceptionKnowledgeWorldSubsystem::Deinitialize()
{
	if (Runtime)
	{
		Runtime->bShuttingDown = true;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(Runtime->SemanticNoiseCleanupTimerHandle);
		}
		Runtime->Relationships.Reset();
		Runtime->PendingSemanticNoises.Reset();
		Runtime->Listeners.Reset();
		Runtime->Sources.Reset();
		Runtime->Stats = FPerceptionKnowledgeRuntimeStats();
	}
	Super::Deinitialize();
}

bool UPerceptionKnowledgeWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::GamePreview;
}

UPerceptionKnowledgeSourceComponent* UPerceptionKnowledgeWorldSubsystem::FindSource(
	const FPerceptionKnowledgeEntityId EntityId)
{
	if (!Runtime || !EntityId.IsValid())
	{
		return nullptr;
	}

	if (TWeakObjectPtr<UPerceptionKnowledgeSourceComponent>* Entry = Runtime->Sources.Find(EntityId))
	{
		if (UPerceptionKnowledgeSourceComponent* Source = Entry->Get())
		{
			return Source;
		}
		Runtime->Sources.Remove(EntityId);
		Runtime->Stats.RegisteredSources = Runtime->Sources.Num();
	}
	return nullptr;
}

TArray<UPerceptionKnowledgeSourceComponent*> UPerceptionKnowledgeWorldSubsystem::GetRegisteredSources()
{
	TArray<UPerceptionKnowledgeSourceComponent*> Result;
	if (!Runtime)
	{
		return Result;
	}

	for (auto Iterator = Runtime->Sources.CreateIterator(); Iterator; ++Iterator)
	{
		if (UPerceptionKnowledgeSourceComponent* Source = Iterator.Value().Get())
		{
			Result.Add(Source);
		}
		else
		{
			Runtime->Relationships.Remove(Iterator.Value());
			Iterator.RemoveCurrent();
		}
	}
	Runtime->Stats.RegisteredSources = Runtime->Sources.Num();
	return Result;
}

FPerceptionKnowledgeRuntimeStats UPerceptionKnowledgeWorldSubsystem::GetRuntimeStats() const
{
	return Runtime ? Runtime->Stats : FPerceptionKnowledgeRuntimeStats();
}

void UPerceptionKnowledgeWorldSubsystem::DumpRegistryToLog() const
{
	if (!Runtime)
	{
		PERCEPTIONKNOWLEDGE_LOG_WARNING(TEXT("Registry dump requested after subsystem runtime teardown."));
		return;
	}

	PERCEPTIONKNOWLEDGE_LOG_INFO(
		TEXT("Registry World=%s Sources=%d Listeners=%d Relationships=%d PendingNoise=%d Observations=%lld Duplicates=%lld VisibleRefreshes=%lld LastRefreshMs=%.3f DebugFrames=%lld LastDebugMs=%.3f"),
		*GetNameSafe(GetWorld()),
		Runtime->Sources.Num(),
		Runtime->Listeners.Num(),
		Runtime->Relationships.Num(),
		Runtime->PendingSemanticNoises.Num(),
		Runtime->Stats.ProducedObservations,
		Runtime->Stats.DuplicateObservationsDiscarded,
		Runtime->Stats.VisibleSourceRefreshes,
		Runtime->Stats.LastVisibleRefreshMilliseconds,
		Runtime->Stats.DebugFramesBuilt,
		Runtime->Stats.LastDebugDrawMilliseconds);

	for (const TPair<FPerceptionKnowledgeEntityId, TWeakObjectPtr<UPerceptionKnowledgeSourceComponent>>& Pair : Runtime->Sources)
	{
		PERCEPTIONKNOWLEDGE_LOG_INFO(
			TEXT("  Source Id=%s Component=%s Owner=%s Valid=%s"),
			*Pair.Key.ToString(),
			*GetNameSafe(Pair.Value.Get()),
			*GetNameSafe(Pair.Value.IsValid() ? Pair.Value->GetOwner() : nullptr),
			Pair.Value.IsValid() ? TEXT("true") : TEXT("false"));
	}
}

FPerceptionKnowledgeEntityId UPerceptionKnowledgeWorldSubsystem::ResolveEntityId(const AActor* Actor) const
{
	if (!Actor)
	{
		return FPerceptionKnowledgeEntityId();
	}

	if (const UPerceptionKnowledgeSourceComponent* Source =
		Actor->FindComponentByClass<UPerceptionKnowledgeSourceComponent>())
	{
		return Source->GetEntityId();
	}
	return FPerceptionKnowledgeEntityId();
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeWorldSubsystem::RegisterSource(
	UPerceptionKnowledgeSourceComponent* Source)
{
	if (!IsInGameThread())
	{
		return MakeResult(
			EPerceptionKnowledgeOperationStatus::WrongThread,
			TEXT("Sources must be registered on the game thread."));
	}
	if (!Runtime || Runtime->bShuttingDown)
	{
		return MakeResult(
			EPerceptionKnowledgeOperationStatus::ShuttingDown,
			TEXT("The world subsystem is shutting down."));
	}
	if (!IsValid(Source) || !IsValid(Source->GetOwner()))
	{
		return MakeResult(
			EPerceptionKnowledgeOperationStatus::InvalidOwner,
			TEXT("The Source or its owning Actor is invalid."));
	}
	const FPerceptionKnowledgeEntityId EntityId = Source->GetEntityId();
	if (!EntityId.IsValid())
	{
		return MakeResult(
			EPerceptionKnowledgeOperationStatus::InvalidEntityId,
			TEXT("The Source has no valid semantic Entity ID."));
	}

	if (TWeakObjectPtr<UPerceptionKnowledgeSourceComponent>* Existing = Runtime->Sources.Find(EntityId))
	{
		if (Existing->IsValid() && Existing->Get() != Source)
		{
			PERCEPTIONKNOWLEDGE_LOG_ERROR(
				TEXT("Rejected duplicate EntityId=%s Source=%s Owner=%s; first Source=%s Owner=%s remains registered."),
				*EntityId.ToString(),
				*GetNameSafe(Source),
				*GetNameSafe(Source->GetOwner()),
				*GetNameSafe(Existing->Get()),
				*GetNameSafe(Existing->IsValid() ? Existing->Get()->GetOwner() : nullptr));
			return MakeResult(
				EPerceptionKnowledgeOperationStatus::DuplicateEntityId,
				TEXT("Another live Source already owns this Entity ID; the first registration was preserved."),
				EntityId);
		}
		Runtime->Sources.Remove(EntityId);
	}

	Runtime->Sources.Add(EntityId, Source);
	Runtime->Stats.RegisteredSources = Runtime->Sources.Num();
	PERCEPTIONKNOWLEDGE_LOG_INFO(
		TEXT("Registered Source=%s Owner=%s EntityId=%s World=%s"),
		*GetNameSafe(Source),
		*GetNameSafe(Source->GetOwner()),
		*EntityId.ToString(),
		*GetNameSafe(GetWorld()));
	return MakeResult(
		EPerceptionKnowledgeOperationStatus::Success,
		TEXT("Source registered."),
		EntityId);
}

void UPerceptionKnowledgeWorldSubsystem::UnregisterSource(UPerceptionKnowledgeSourceComponent* Source)
{
	if (!Runtime || !Source)
	{
		return;
	}

	const FPerceptionKnowledgeEntityId EntityId = Source->GetEntityId();
	const TWeakObjectPtr<UPerceptionKnowledgeSourceComponent>* Existing = Runtime->Sources.Find(EntityId);
	if (!Existing || Existing->Get() != Source)
	{
		return;
	}

	TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>> InterestedListeners;
	if (const TMap<FGameplayTag, TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>>>* BySense =
		Runtime->Relationships.Find(Source))
	{
		for (const TPair<FGameplayTag, TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>>>& SensePair : *BySense)
		{
			InterestedListeners.Append(SensePair.Value);
		}
	}
	Runtime->Relationships.Remove(Source);
	for (const TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>& Listener : InterestedListeners)
	{
		if (Listener.IsValid())
		{
			Listener->HandleSourceUnregistered(Source);
		}
	}

	Runtime->Sources.Remove(EntityId);
	Runtime->Stats.RegisteredSources = Runtime->Sources.Num();
	PERCEPTIONKNOWLEDGE_LOG_INFO(
		TEXT("Unregistered Source=%s Owner=%s EntityId=%s World=%s"),
		*GetNameSafe(Source),
		*GetNameSafe(Source->GetOwner()),
		*EntityId.ToString(),
		*GetNameSafe(GetWorld()));
}

void UPerceptionKnowledgeWorldSubsystem::RegisterListener(
	UPerceptionKnowledgeListenerComponent* Listener)
{
	if (!Runtime || Runtime->bShuttingDown || !IsValid(Listener))
	{
		return;
	}
	Runtime->Listeners.Add(Listener);
	for (auto Iterator = Runtime->Listeners.CreateIterator(); Iterator; ++Iterator)
	{
		if (!Iterator->IsValid())
		{
			Iterator.RemoveCurrent();
		}
	}
	Runtime->Stats.RegisteredListeners = Runtime->Listeners.Num();
	PERCEPTIONKNOWLEDGE_LOG_INFO(
		TEXT("Registered Listener=%s Owner=%s Body=%s World=%s"),
		*GetNameSafe(Listener),
		*GetNameSafe(Listener->GetOwner()),
		*GetNameSafe(Listener->GetResolvedBodyActor()),
		*GetNameSafe(GetWorld()));
}

void UPerceptionKnowledgeWorldSubsystem::UnregisterListener(
	UPerceptionKnowledgeListenerComponent* Listener)
{
	if (!Runtime || !Listener)
	{
		return;
	}
	Runtime->Listeners.Remove(Listener);
	for (auto SourceIterator = Runtime->Relationships.CreateIterator(); SourceIterator; ++SourceIterator)
	{
		for (auto SenseIterator = SourceIterator.Value().CreateIterator(); SenseIterator; ++SenseIterator)
		{
			SenseIterator.Value().Remove(Listener);
			for (auto ListenerIterator = SenseIterator.Value().CreateIterator(); ListenerIterator; ++ListenerIterator)
			{
				if (!ListenerIterator->IsValid())
				{
					ListenerIterator.RemoveCurrent();
				}
			}
			if (SenseIterator.Value().IsEmpty())
			{
				SenseIterator.RemoveCurrent();
			}
		}
		if (SourceIterator.Value().IsEmpty())
		{
			SourceIterator.RemoveCurrent();
		}
	}
	Runtime->Stats.RegisteredListeners = Runtime->Listeners.Num();
}

void UPerceptionKnowledgeWorldSubsystem::UpdatePerceptionRelationship(
	UPerceptionKnowledgeSourceComponent* Source,
	UPerceptionKnowledgeListenerComponent* Listener,
	const FGameplayTag SenseTag,
	const bool bPerceived)
{
	if (!Runtime || Runtime->bShuttingDown || !Source || !Listener || !SenseTag.IsValid())
	{
		return;
	}

	if (bPerceived)
	{
		Runtime->Relationships.FindOrAdd(Source).FindOrAdd(SenseTag).Add(Listener);
		return;
	}

	if (TMap<FGameplayTag, TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>>>* BySense =
		Runtime->Relationships.Find(Source))
	{
		if (TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>>* Listeners = BySense->Find(SenseTag))
		{
			Listeners->Remove(Listener);
			if (Listeners->IsEmpty())
			{
				BySense->Remove(SenseTag);
			}
		}
		if (BySense->IsEmpty())
		{
			Runtime->Relationships.Remove(Source);
		}
	}
}

int32 UPerceptionKnowledgeWorldSubsystem::NotifySourceStateChanged(
	UPerceptionKnowledgeSourceComponent* Source,
	const FPerceptionKnowledgeExposedState& State,
	const bool bRemoved)
{
	if (!Runtime || Runtime->bShuttingDown || !Source)
	{
		return 0;
	}

	TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>> Recipients;
	if (const TMap<FGameplayTag, TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>>>* BySense =
		Runtime->Relationships.Find(Source))
	{
		for (const FGameplayTag& SenseTag : State.ObservableThroughSenses)
		{
			if (const TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>>* Listeners =
				BySense->Find(SenseTag))
			{
				Recipients.Append(*Listeners);
			}
		}
	}

	int32 Delivered = 0;
	for (const TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>& Listener : Recipients)
	{
		if (Listener.IsValid())
		{
			Listener->HandleSourceStateChanged(Source, State, bRemoved);
			++Delivered;
		}
	}
	return Delivered;
}

int32 UPerceptionKnowledgeWorldSubsystem::NotifyProviderStatesChanged(
	UPerceptionKnowledgeSourceComponent* Source)
{
	if (!Runtime || Runtime->bShuttingDown || !Source)
	{
		return 0;
	}

	const TMap<FGameplayTag, TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>>>* BySense =
		Runtime->Relationships.Find(Source);
	if (!BySense)
	{
		return 0;
	}

	TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>> Refreshed;
	for (const TPair<FGameplayTag, TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>>>& SensePair : *BySense)
	{
		for (const TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>& Listener : SensePair.Value)
		{
			if (Listener.IsValid())
			{
				Listener->RefreshSourceStates(
					Source,
					SensePair.Key,
					1.0f,
					Source->GetOwner()->GetActorLocation(),
					false);
				Refreshed.Add(Listener);
			}
		}
	}
	return Refreshed.Num();
}

int32 UPerceptionKnowledgeWorldSubsystem::RouteObservableEvent(
	UPerceptionKnowledgeSourceComponent* Source,
	const FPerceptionKnowledgeEventObservation& Event)
{
	if (!Runtime || Runtime->bShuttingDown || !Source)
	{
		return 0;
	}

	const TMap<FGameplayTag, TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>>>* BySense =
		Runtime->Relationships.Find(Source);
	const TSet<TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>>* Listeners =
		BySense ? BySense->Find(Event.SenseTag) : nullptr;
	if (!Listeners)
	{
		return 0;
	}

	int32 Delivered = 0;
	for (const TWeakObjectPtr<UPerceptionKnowledgeListenerComponent>& Listener : *Listeners)
	{
		if (Listener.IsValid())
		{
			Listener->ReceiveEventObservation(Event);
			++Delivered;
		}
	}
	return Delivered;
}

FPerceptionKnowledgeOperationResult UPerceptionKnowledgeWorldSubsystem::RegisterSemanticNoise(
	UPerceptionKnowledgeSourceComponent* Source,
	const FPerceptionKnowledgeNoiseRequest& Request,
	FName& OutCorrelationTag,
	FPerceptionKnowledgeEventObservation& OutEvent)
{
	OutCorrelationTag = NAME_None;
	OutEvent = FPerceptionKnowledgeEventObservation();
	if (!IsInGameThread())
	{
		return MakeResult(
			EPerceptionKnowledgeOperationStatus::WrongThread,
			TEXT("Semantic noises must be emitted on the game thread."));
	}
	if (!Runtime || Runtime->bShuttingDown)
	{
		return MakeResult(
			EPerceptionKnowledgeOperationStatus::ShuttingDown,
			TEXT("The world subsystem is shutting down."));
	}
	if (!Source || !Source->bSemanticRegistered || !FindSource(Source->GetEntityId()))
	{
		return MakeResult(
			EPerceptionKnowledgeOperationStatus::NotRegistered,
			TEXT("The Source must be semantically registered before emitting noise."),
			Source ? Source->GetEntityId() : FPerceptionKnowledgeEntityId());
	}
	if (!Request.EventTag.IsValid() || Request.Loudness < 0.0f || Request.Strength < 0.0f
		|| !FMath::IsFinite(Request.Loudness) || !FMath::IsFinite(Request.Strength))
	{
		return MakeResult(
			EPerceptionKnowledgeOperationStatus::InvalidArgument,
			TEXT("The noise requires a valid Event Tag and finite non-negative loudness and strength."),
			Source->GetEntityId());
	}

	CleanupSemanticNoises();
	const UPerceptionKnowledgeDeveloperSettings* Settings =
		GetDefault<UPerceptionKnowledgeDeveloperSettings>();
	const int32 Capacity = FMath::Max(1, Settings->MaxPendingSemanticNoises);
	if (Runtime->PendingSemanticNoises.Num() >= Capacity)
	{
		PERCEPTIONKNOWLEDGE_LOG_ERROR(
			TEXT("Rejected semantic noise from Source=%s EntityId=%s: pending registry capacity %d reached."),
			*GetNameSafe(Source),
			*Source->GetEntityId().ToString(),
			Capacity);
		return MakeResult(
			EPerceptionKnowledgeOperationStatus::CapacityExceeded,
			TEXT("The bounded semantic-noise correlation registry is full."),
			Source->GetEntityId());
	}

	if (Runtime->NextCorrelationNumber <= 0)
	{
		Runtime->NextCorrelationNumber = 1;
	}
	const FName CorrelationTag(FName(TEXT("PerceptionKnowledgeNoise")), Runtime->NextCorrelationNumber++);
	const UWorld* World = GetWorld();
	const FVector Location = Request.bUseSourceLocation
		? Source->GetOwner()->GetActorLocation()
		: Request.WorldLocation;
	AActor* NativeInstigator = Request.Instigator ? Request.Instigator.Get() : Source->GetOwner();

	OutEvent.ObservationId = FGuid::NewGuid();
	OutEvent.EventTag = Request.EventTag;
	OutEvent.SenseTag = TAG_PerceptionKnowledge_Sense_Hearing;
	OutEvent.SourceEntityId = Source->GetEntityId();
	OutEvent.InstigatorEntityId = ResolveEntityId(NativeInstigator);
	OutEvent.WorldLocation = Location;
	OutEvent.Loudness = Request.Loudness;
	OutEvent.Strength = Request.Strength;
	OutEvent.Confidence = 0.0f;
	OutEvent.WorldTimestamp = World ? World->GetTimeSeconds() : 0.0;
	OutEvent.CauseTag = Request.CauseTag;

	FPendingSemanticNoise Pending;
	Pending.Event = OutEvent;
	Pending.NativeInstigator = NativeInstigator;
	Pending.Location = Location;
	Pending.ExpirationWorldTime = OutEvent.WorldTimestamp
		+ FMath::Max(0.1f, Settings->SemanticNoiseCorrelationLifetime);
	Runtime->PendingSemanticNoises.Add(CorrelationTag, MoveTemp(Pending));
	Runtime->Stats.PendingSemanticNoises = Runtime->PendingSemanticNoises.Num();
	OutCorrelationTag = CorrelationTag;

	if (UWorld* MutableWorld = GetWorld();
		MutableWorld && !MutableWorld->GetTimerManager().IsTimerActive(Runtime->SemanticNoiseCleanupTimerHandle))
	{
		MutableWorld->GetTimerManager().SetTimer(
			Runtime->SemanticNoiseCleanupTimerHandle,
			this,
			&UPerceptionKnowledgeWorldSubsystem::CleanupSemanticNoises,
			FMath::Max(0.1f, Settings->SemanticNoiseCleanupInterval),
			true);
	}

	return MakeResult(
		EPerceptionKnowledgeOperationStatus::Success,
		TEXT("Semantic noise registered for native Hearing correlation."),
		Source->GetEntityId(),
		OutEvent.ObservationId);
}

bool UPerceptionKnowledgeWorldSubsystem::ResolveSemanticNoise(
	const FName CorrelationTag,
	const AActor* NativeInstigator,
	const FVector& StimulusLocation,
	FPerceptionKnowledgeEventObservation& OutEvent)
{
	OutEvent = FPerceptionKnowledgeEventObservation();
	if (!Runtime || Runtime->bShuttingDown || CorrelationTag.IsNone())
	{
		return false;
	}

	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const FPendingSemanticNoise* Pending = Runtime->PendingSemanticNoises.Find(CorrelationTag);
	if (!Pending)
	{
		PERCEPTIONKNOWLEDGE_LOG_WARNING(
			TEXT("Hearing correlation failed: Tag=%s Instigator=%s Location=%s is missing."),
			*CorrelationTag.ToString(),
			*GetNameSafe(NativeInstigator),
			*StimulusLocation.ToCompactString());
		return false;
	}
	if (Pending->ExpirationWorldTime <= CurrentTime)
	{
		Runtime->PendingSemanticNoises.Remove(CorrelationTag);
		Runtime->Stats.PendingSemanticNoises = Runtime->PendingSemanticNoises.Num();
		PERCEPTIONKNOWLEDGE_LOG_WARNING(
			TEXT("Hearing correlation expired: Tag=%s Instigator=%s Location=%s."),
			*CorrelationTag.ToString(),
			*GetNameSafe(NativeInstigator),
			*StimulusLocation.ToCompactString());
		return false;
	}
	if ((Pending->NativeInstigator.IsValid() && Pending->NativeInstigator.Get() != NativeInstigator)
		|| !Pending->Location.Equals(StimulusLocation, 1.0f))
	{
		PERCEPTIONKNOWLEDGE_LOG_WARNING(
			TEXT("Hearing correlation mismatch: Tag=%s ExpectedInstigator=%s ReceivedInstigator=%s ExpectedLocation=%s ReceivedLocation=%s."),
			*CorrelationTag.ToString(),
			*GetNameSafe(Pending->NativeInstigator.Get()),
			*GetNameSafe(NativeInstigator),
			*Pending->Location.ToCompactString(),
			*StimulusLocation.ToCompactString());
		return false;
	}

	OutEvent = Pending->Event;
	return true;
}

void UPerceptionKnowledgeWorldSubsystem::CleanupSemanticNoises()
{
	if (!Runtime)
	{
		return;
	}

	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (auto Iterator = Runtime->PendingSemanticNoises.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator.Value().ExpirationWorldTime <= CurrentTime)
		{
			Iterator.RemoveCurrent();
		}
	}
	Runtime->Stats.PendingSemanticNoises = Runtime->PendingSemanticNoises.Num();
	if (Runtime->PendingSemanticNoises.IsEmpty())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(Runtime->SemanticNoiseCleanupTimerHandle);
		}
	}
}

void UPerceptionKnowledgeWorldSubsystem::RecordProducedObservation()
{
	if (Runtime)
	{
		++Runtime->Stats.ProducedObservations;
	}
}

void UPerceptionKnowledgeWorldSubsystem::RecordDiscardedDuplicate()
{
	if (Runtime)
	{
		++Runtime->Stats.DuplicateObservationsDiscarded;
	}
}

void UPerceptionKnowledgeWorldSubsystem::RecordVisibleRefresh(const double Milliseconds)
{
	if (Runtime)
	{
		++Runtime->Stats.VisibleSourceRefreshes;
		Runtime->Stats.LastVisibleRefreshMilliseconds = Milliseconds;
	}
}

void UPerceptionKnowledgeWorldSubsystem::RecordDebugDraw(const double Milliseconds)
{
	if (Runtime)
	{
		++Runtime->Stats.DebugFramesBuilt;
		Runtime->Stats.LastDebugDrawMilliseconds = Milliseconds;
	}
}

#undef TAG_PerceptionKnowledge_Sense_Hearing
