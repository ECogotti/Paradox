#pragma once

#include "GameFramework/Actor.h"
#include "Types/PerceptionKnowledgeTypes.h"
#include "ParadoxSemanticNoiseSphere.generated.h"

class UPerceptionKnowledgeSourceComponent;
class USphereComponent;
class UStaticMeshComponent;

/** Placeable hearing fixture that emits only through PerceptionKnowledge semantic noise. */
UCLASS(BlueprintType, Blueprintable)
class PARADOX_API AParadoxSemanticNoiseSphere : public AActor
{
	GENERATED_BODY()

public:
	AParadoxSemanticNoiseSphere();

	UFUNCTION(BlueprintCallable, Category = "Paradox|Perception Test")
	FPerceptionKnowledgeOperationResult EmitSemanticNoise(AActor* InstigatorActor);

	UFUNCTION(BlueprintCallable, Category = "Paradox|Perception Test")
	void ResetEmission();

	UFUNCTION(BlueprintPure, Category = "Paradox|Perception Test")
	UPerceptionKnowledgeSourceComponent* GetPerceptionSource() const
	{
		return PerceptionSource.Get();
	}

private:
	UFUNCTION()
	void HandleOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	FPerceptionKnowledgeOperationResult MakeSuppressedResult(
		const FString& Diagnostic) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPerceptionKnowledgeSourceComponent> PerceptionSource;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Perception Test", meta = (AllowPrivateAccess = "true"))
	FGameplayTag EventTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Perception Test", meta = (AllowPrivateAccess = "true"))
	bool bEmitOnOverlap = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Perception Test", meta = (AllowPrivateAccess = "true"))
	bool bOneShot = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Perception Test", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "Seconds"))
	float CooldownSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Perception Test", meta = (AllowPrivateAccess = "true", ClampMin = "0.0"))
	float Loudness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paradox|Perception Test", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "Centimeters"))
	float MaxRange = 0.0f;

	double LastEmissionWorldTime = -DBL_MAX;
	bool bHasEmitted = false;
};
