#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Types/PerceptionKnowledgeTypes.h"
#include "PerceptionKnowledgeWorldSubsystem.generated.h"

class UPerceptionKnowledgeListenerComponent;
class UPerceptionKnowledgeSourceComponent;
struct FPerceptionKnowledgeSubsystemRuntime;

/** Per-world weak registry and event-driven routing layer. Knowledge remains Listener-owned. */
UCLASS()
class PERCEPTIONKNOWLEDGE_API UPerceptionKnowledgeWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UPerceptionKnowledgeWorldSubsystem();
	virtual ~UPerceptionKnowledgeWorldSubsystem() override;

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Registry")
	UPerceptionKnowledgeSourceComponent* FindSource(FPerceptionKnowledgeEntityId EntityId);

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Registry")
	TArray<UPerceptionKnowledgeSourceComponent*> GetRegisteredSources();

	UFUNCTION(BlueprintPure, Category = "Perception Knowledge|Debug")
	FPerceptionKnowledgeRuntimeStats GetRuntimeStats() const;

	UFUNCTION(BlueprintCallable, Category = "Perception Knowledge|Debug")
	void DumpRegistryToLog() const;

	FPerceptionKnowledgeEntityId ResolveEntityId(const AActor* Actor) const;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	FPerceptionKnowledgeOperationResult RegisterSource(UPerceptionKnowledgeSourceComponent* Source);
	void UnregisterSource(UPerceptionKnowledgeSourceComponent* Source);
	void RegisterListener(UPerceptionKnowledgeListenerComponent* Listener);
	void UnregisterListener(UPerceptionKnowledgeListenerComponent* Listener);
	void UpdatePerceptionRelationship(
		UPerceptionKnowledgeSourceComponent* Source,
		UPerceptionKnowledgeListenerComponent* Listener,
		FGameplayTag SenseTag,
		bool bPerceived);
	int32 NotifySourceStateChanged(
		UPerceptionKnowledgeSourceComponent* Source,
		const FPerceptionKnowledgeExposedState& State,
		bool bRemoved);
	int32 NotifyProviderStatesChanged(UPerceptionKnowledgeSourceComponent* Source);
	int32 RouteObservableEvent(
		UPerceptionKnowledgeSourceComponent* Source,
		const FPerceptionKnowledgeEventObservation& Event);
	FPerceptionKnowledgeOperationResult RegisterSemanticNoise(
		UPerceptionKnowledgeSourceComponent* Source,
		const FPerceptionKnowledgeNoiseRequest& Request,
		FName& OutCorrelationTag,
		FPerceptionKnowledgeEventObservation& OutEvent);
	bool ResolveSemanticNoise(
		FName CorrelationTag,
		const AActor* NativeInstigator,
		const FVector& StimulusLocation,
		FPerceptionKnowledgeEventObservation& OutEvent);
	void CleanupSemanticNoises();
	void RecordProducedObservation();
	void RecordDiscardedDuplicate();
	void RecordVisibleRefresh(double Milliseconds);
	void RecordDebugDraw(double Milliseconds);

	FPerceptionKnowledgeSubsystemRuntime* Runtime = nullptr;

	friend class UPerceptionKnowledgeSourceComponent;
	friend class UPerceptionKnowledgeListenerComponent;
	friend struct FPerceptionKnowledgeTestAccessor;
};
