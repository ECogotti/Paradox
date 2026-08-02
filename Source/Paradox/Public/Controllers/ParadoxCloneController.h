#pragma once

#include "AI/GridWorldAIController.h"
#include "ParadoxCloneController.generated.h"

class UBehaviorTree;
class UPerceptionKnowledgeHearingRangeRendererComponent;
class UPerceptionKnowledgeListenerComponent;
class UPerceptionKnowledgeProfile;

/**
 * Non-player controller for reconstructed Paradox clones.
 *
 * GridWorld supplies the precise path-following component required by future Intent Replay
 * playback. This class intentionally owns no input or player UI state.
 */
UCLASS(Blueprintable)
class PARADOX_API AParadoxCloneController : public AGridWorldAIController
{
	GENERATED_BODY()

public:
	AParadoxCloneController(
		const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UFUNCTION(BlueprintPure, Category = "Paradox|Perception")
	UPerceptionKnowledgeListenerComponent* GetPerceptionKnowledgeListener() const
	{
		return PerceptionKnowledgeListener.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Perception")
	UPerceptionKnowledgeHearingRangeRendererComponent*
	GetHearingRangeRenderer() const
	{
		return HearingRangeRenderer.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Paradox|Clone Behavior")
	UBehaviorTree* GetCloneBehaviorTree() const { return CloneBehaviorTree; }

	/** Starts and validates the authored tree after replay/comparison are fully prepared. */
	bool StartCloneBehaviorTree(FString& OutDiagnostic);

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	bool ValidateBlackboardContract(FString& OutDiagnostic) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPerceptionKnowledgeListenerComponent> PerceptionKnowledgeListener;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPerceptionKnowledgeHearingRangeRendererComponent> HearingRangeRenderer;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Perception", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPerceptionKnowledgeProfile> PerceptionProfile;

	/** Authored BT using the documented native task and Blackboard contract. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Paradox|Clone Behavior", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBehaviorTree> CloneBehaviorTree;
};
