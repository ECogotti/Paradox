#include "Tests/WorldStateTestTypes.h"

#include "Components/SceneComponent.h"
#include "Components/WorldStateParticipantComponent.h"
#include "Subsystems/WorldStateSubsystem.h"

UWorldStateTestDataComponent::UWorldStateTestDataComponent()
{
	// Fixtures are deterministic and event-driven, matching the production no-Tick requirement.
	PrimaryComponentTick.bCanEverTick = false;
}

AWorldStateTestActor::AWorldStateTestActor()
{
	// Native default subobjects provide stable names and a reconstructible Scene Component hierarchy.
	PrimaryActorTick.bCanEverTick = false;
	TestRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TestRoot"));
	SetRootComponent(TestRoot);
	TestPivot = CreateDefaultSubobject<USceneComponent>(TEXT("TestPivot"));
	TestPivot->SetupAttachment(TestRoot);
	TestAlternateParent = CreateDefaultSubobject<USceneComponent>(TEXT("TestAlternateParent"));
	TestAlternateParent->SetupAttachment(TestRoot);
	DataComponent = CreateDefaultSubobject<UWorldStateTestDataComponent>(TEXT("StateData"));
	Participant = CreateDefaultSubobject<UWorldStateParticipantComponent>(TEXT("WorldStateParticipant"));
}

void UWorldStateTestObserver::HandlePreCapture(FWorldStateParticipantId ParticipantId)
{
	++PreCaptureCount;
	if (bSetIntegerDuringPreCapture && WatchedActor && WatchedActor->DataComponent)
	{
		// Proves the callback runs before the serializer reads the selected property.
		WatchedActor->DataComponent->IntegerValue = PreCaptureIntegerValue;
	}
}

void UWorldStateTestObserver::HandleCaptured(FWorldStateParticipantId ParticipantId)
{
	++CapturedCount;
}

void UWorldStateTestObserver::HandlePreRestore(FWorldStateParticipantId ParticipantId)
{
	++PreRestoreCount;
	if (WatchedActor && WatchedActor->DataComponent)
	{
		IntegerSeenAtPreRestore = WatchedActor->DataComponent->IntegerValue;
		LocationSeenAtPreRestore = WatchedActor->GetActorLocation();
	}
	if (bRequestNestedRestore && Subsystem)
	{
		// Captures the public RejectedBusy result produced by synchronous reentrancy.
		NestedRestoreStatus = Subsystem->RestoreBaseline(FWorldStateRestoreRequest()).Status;
	}
}

void UWorldStateTestObserver::HandlePropertiesRestored(FWorldStateParticipantId ParticipantId)
{
	++PropertiesRestoredCount;
	if (WatchedActor && WatchedActor->DataComponent)
	{
		IntegerSeenAtPropertiesRestored = WatchedActor->DataComponent->IntegerValue;
	}
}

void UWorldStateTestObserver::HandleRestored(FWorldStateParticipantId ParticipantId)
{
	++RestoredCount;
	if (WatchedActor && WatchedActor->DataComponent)
	{
		IntegerSeenAtRestored = WatchedActor->DataComponent->IntegerValue;
		LocationSeenAtRestored = WatchedActor->GetActorLocation();
	}
	if (RestoreOrderLog)
	{
		// Optional external storage lets dependency tests compare callback order across runs.
		RestoreOrderLog->Add(ParticipantId);
	}
}

void UWorldStateTestObserver::HandleRestoreFailed(const FWorldStateParticipantResult& Result)
{
	++FailedCount;
	if (!Result.Issues.IsEmpty())
	{
		LastFailureCode = Result.Issues[0].Code;
	}
}
