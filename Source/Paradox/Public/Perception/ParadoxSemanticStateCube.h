#pragma once

#include "GameFramework/Actor.h"
#include "Types/PerceptionKnowledgeTypes.h"
#include "ParadoxSemanticStateCube.generated.h"

class UMaterialInterface;
class UPerceptionKnowledgeSourceComponent;
class UStaticMeshComponent;

/** Placeable semantic-state fixture with immediate visual feedback. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API AParadoxSemanticStateCube : public AActor
{
	GENERATED_BODY()

public:
	AParadoxSemanticStateCube();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Perception Test")
	FPerceptionKnowledgeOperationResult SetPowered(bool bInPowered);

	UFUNCTION(BlueprintPure, Category = "Paradox|Perception Test")
	bool IsPowered() const { return bPowered; }

	UFUNCTION(BlueprintPure, Category = "Paradox|Perception Test")
	UPerceptionKnowledgeSourceComponent* GetPerceptionSource() const
	{
		return PerceptionSource.Get();
	}

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void UpdateVisualFeedback();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPerceptionKnowledgeSourceComponent> PerceptionSource;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Perception Test", meta = (AllowPrivateAccess = "true"))
	FGameplayTag StateTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Perception Test", meta = (AllowPrivateAccess = "true"))
	bool bPowered = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Perception Test|Visual", meta = (AllowPrivateAccess = "true"))
	FLinearColor PoweredColor = FLinearColor(0.05f, 1.0f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Perception Test|Visual", meta = (AllowPrivateAccess = "true"))
	FLinearColor UnpoweredColor = FLinearColor(0.08f, 0.08f, 0.08f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Perception Test|Visual", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialInterface> PoweredMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Perception Test|Visual", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialInterface> UnpoweredMaterial;
};
