#pragma once

#include "Components/StaticMeshComponent.h"
#include "PerceptionKnowledgeHearingRangeRendererComponent.generated.h"

class APawn;
class UPerceptionKnowledgeListenerComponent;
class UStaticMeshComponent;

/**
 * Event-driven, non-colliding visualization of a listener's effective Hearing Range.
 *
 * The component may be owned by a Controller; it anchors itself to the listener's current Body
 * Actor and follows possession changes without ticking.
 */
UCLASS(
	ClassGroup = (PerceptionKnowledge),
	BlueprintType,
	Blueprintable,
	meta = (BlueprintSpawnableComponent))
class PERCEPTIONKNOWLEDGE_API
	UPerceptionKnowledgeHearingRangeRendererComponent
		: public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UPerceptionKnowledgeHearingRangeRendererComponent();

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Hearing Renderer")
	void SetListener(UPerceptionKnowledgeListenerComponent* InListener);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Hearing Renderer")
	void SetGameplayVisible(bool bInVisible);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Hearing Renderer")
	void SetLocalDebugEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Hearing Renderer")
	void RefreshRenderer();

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Hearing Renderer")
	float GetRenderedHearingRange() const { return RenderedHearingRange; }

	/**
	 * Returns the primitive that is currently presenting the range.
	 *
	 * A Controller-owned renderer uses a transient primitive owned by the Body Actor because
	 * Controllers are hidden rendering owners. A Pawn-owned renderer may return itself.
	 */
	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Hearing Renderer")
	UStaticMeshComponent* GetActiveRenderComponent() const;

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Hearing Renderer")
	bool IsHearingRangeVisible() const;

	/** Current readiness/visibility explanation, intended for setup diagnostics. */
	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Hearing Renderer")
	FString GetRendererDiagnostic() const { return RendererDiagnostic; }

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	void BindListener(UPerceptionKnowledgeListenerComponent* InListener);
	void UnbindListener();
	void HandleListenerConfigurationChanged();
	void HandleGlobalDebugConfigurationChanged();
	UStaticMeshComponent* ResolveRenderComponent(
		AActor* BodyActor,
		USceneComponent* BodyRoot,
		bool bCreateIfMissing);
	void ConfigureRenderComponent(UStaticMeshComponent& Component) const;
	void SynchronizeRenderAssets(UStaticMeshComponent& Component) const;
	void DestroyBodyRenderComponent();
	void SetRendererDiagnostic(FString InDiagnostic, bool bLogWarning);

	/** Radius represented by an unscaled authored mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Hearing Renderer", meta = (AllowPrivateAccess = "true", ClampMin = "0.01", Units = "Centimeters"))
	float AuthoredMeshRadius = 50.0f;

	/** Keeps a spherical source mesh visually flat while preserving the range in X/Y. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Hearing Renderer", meta = (AllowPrivateAccess = "true", ClampMin = "0.001"))
	float VerticalScaleMultiplier = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Hearing Renderer", meta = (AllowPrivateAccess = "true"))
	bool bVisibleInGameplay = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perception Knowledge|Hearing Renderer|Debug", meta = (AllowPrivateAccess = "true"))
	bool bEnableDebug = false;

	UPROPERTY(Transient)
	TObjectPtr<UPerceptionKnowledgeListenerComponent> Listener;

	/**
	 * Controller Actors are hidden by Unreal and cannot directly own a visible primitive.
	 * This transient proxy is therefore owned and registered by the resolved Body Actor.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BodyRenderComponent;

	FDelegateHandle ListenerConfigurationHandle;
	FDelegateHandle GlobalDebugConfigurationHandle;
	float RenderedHearingRange = 0.0f;
	FString RendererDiagnostic;
};
