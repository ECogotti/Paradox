#pragma once

#include "Components/ActorComponent.h"
#include "TimeLoop/ParadoxTimeLoopTypes.h"
#include "ParadoxTemporalEntityComponent.generated.h"

class UIntentReplayTrack;

/**
 * Project-specific temporal identity kept separate from Entity Relations' generic identity.
 */
UCLASS(ClassGroup = (Paradox), BlueprintType, meta = (BlueprintSpawnableComponent))
class PARADOX_API UParadoxTemporalEntityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UParadoxTemporalEntityComponent();

	UFUNCTION(BlueprintPure, Category = "Paradox|Temporal Entity")
	EParadoxTemporalEntityRole GetTemporalRole() const { return TemporalRole; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Temporal Entity")
	int32 GetTemporalIndex() const { return TemporalIndex; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Temporal Entity")
	bool HasValidTemporalIndex() const { return TemporalIndex >= 0; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Temporal Entity")
	UIntentReplayTrack* GetAssignedReplayTrack() const { return AssignedReplayTrack; }

	/** Assigns the currently controlled version. A player never owns a replay track. */
	bool AssignPlayer(int32 InTemporalIndex);

	/** Assigns a reconstructed clone and validates its immutable source track. */
	bool AssignClone(int32 InTemporalIndex, UIntentReplayTrack* InReplayTrack);

	/** Returns the component to its non-temporal state. */
	void ClearTemporalAssignment();

private:
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Paradox|Temporal Entity", meta = (AllowPrivateAccess = "true"))
	EParadoxTemporalEntityRole TemporalRole = EParadoxTemporalEntityRole::Unassigned;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Paradox|Temporal Entity", meta = (AllowPrivateAccess = "true"))
	int32 TemporalIndex = INDEX_NONE;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Paradox|Temporal Entity", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UIntentReplayTrack> AssignedReplayTrack = nullptr;
};

