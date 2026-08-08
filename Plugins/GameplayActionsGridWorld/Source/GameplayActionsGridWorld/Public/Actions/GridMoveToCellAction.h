#pragma once

#include "CoreMinimal.h"
#include "Actions/GameplayActionInstance.h"
#include "Execution/GridMoveToCellExecution.h"
#include "GridMoveToCellAction.generated.h"

class AController;

/** GameplayActions adapter around the reusable controller-aware GridWorld movement executor. */
UCLASS(Blueprintable)
class GAMEPLAYACTIONSGRIDWORLD_API UGridMoveToCellAction : public UGameplayActionInstance
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

protected:
	virtual bool CanStartAction_Implementation(
		FGameplayTag& OutFailureReason,
		FString& OutDiagnostic) const override;
	virtual void OnActionInit_Implementation() override;
	virtual void OnActionStarted_Implementation() override;
	virtual void OnActionPaused_Implementation() override;
	virtual void OnActionResumed_Implementation() override;
	virtual void OnActionCleanup_Implementation() override;

private:
	bool ReadSettings(
		FGridMoveToCellExecutionRequest& OutSettings,
		FString& OutDiagnostic) const;
	AController* ResolveController() const;
	void HandleExecutionFinished(const FGridMoveToCellExecutionResult& Result);
	void ReleaseExecution(bool bCancel);

	UPROPERTY(Transient)
	FGridMoveToCellExecutionRequest CachedSettings;

	UPROPERTY(Transient)
	TObjectPtr<UGridMoveToCellExecution> MoveExecution;

	FDelegateHandle ExecutionFinishedHandle;
};
