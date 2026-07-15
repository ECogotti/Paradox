#include "Controllers/PuzzleController.h"

#include "Conditions/PuzzleCondition.h"
#include "Components/BillboardComponent.h"
#include "DrawDebugHelpers.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/Texture2D.h"
#include "PuzzleSystem.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* PuzzleControllerIconPath = TEXT("/PuzzleSystem/Textures/T_PuzzleControllerIcon.T_PuzzleControllerIcon");
}

APuzzleController::APuzzleController()
{
	PuzzleBillboardComponent = CreateDefaultSubobject<UBillboardComponent>(TEXT("PuzzleBillboard"));

	// Use the plugin icon when it is available, but keep Unreal's default billboard sprite as a safe fallback.
	static ConstructorHelpers::FObjectFinder<UTexture2D> PuzzleControllerIcon(PuzzleControllerIconPath);
	if (PuzzleControllerIcon.Succeeded() && IsValid(PuzzleControllerIcon.Object))
	{
		PuzzleBillboardComponent->SetSprite(PuzzleControllerIcon.Object);
	}

	PuzzleBillboardComponent->SetHiddenInGame(true);
	RootComponent = PuzzleBillboardComponent;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
}

void APuzzleController::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	UpdateDebugTickState();
}

void APuzzleController::BeginPlay()
{
	Super::BeginPlay();
	InitializePuzzleController();
	UpdateDebugTickState();
}

void APuzzleController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ShutdownPuzzleController();
	Super::EndPlay(EndPlayReason);
}

void APuzzleController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (ShouldDrawPuzzleDebug())
	{
		DrawPuzzleDebug();
	}
}

bool APuzzleController::ShouldTickIfViewportsOnly() const
{
	return bEnableDebug;
}

#if WITH_EDITOR
void APuzzleController::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName ChangedPropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (ChangedPropertyName.IsNone() || ChangedPropertyName == GET_MEMBER_NAME_CHECKED(APuzzleController, bEnableDebug))
	{
		UpdateDebugTickState();
	}
}
#endif

bool APuzzleController::InitializePuzzleController()
{
	ShutdownPuzzleController();

	bConfigurationValid = ValidateAndResolveConfiguration();
	bHasEvaluationResult = false;
	bLastEvaluationResult = false;

	if (!bConfigurationValid)
	{
		PUZZLESYSTEM_LOG_WARNING("Puzzle Controller '%s' failed configuration validation and will evaluate inactive.", *GetNameSafe(this));
		return false;
	}

	BindEmitters();
	InitializeInputCache();

	bIsInitialized = true;
	EvaluateController();
	return true;
}

void APuzzleController::ShutdownPuzzleController()
{
	UnbindEmitters();

	for (const TWeakObjectPtr<UPuzzleReceiverComponent>& ReceiverPtr : ResolvedReceivers)
	{
		if (UPuzzleReceiverComponent* Receiver = ReceiverPtr.Get())
		{
			Receiver->RemoveControllerRequest(this);
		}
	}

	ResolvedInputBindings.Reset();
	BoundEmitters.Reset();
	ResolvedReceivers.Reset();
	RuntimeInputCache.Reset();
	ConfiguredInputIds.Reset();

	bIsInitialized = false;
	bConfigurationValid = false;
	bHasEvaluationResult = false;
	bLastEvaluationResult = false;
	bIsEvaluating = false;
	bReevaluationRequested = false;
}

void APuzzleController::EvaluateController()
{
	if (bIsEvaluating)
	{
		bReevaluationRequested = true;
		return;
	}

	bIsEvaluating = true;

	do
	{
		bReevaluationRequested = false;

		const bool bNewResult = bConfigurationValid && IsValid(RootCondition) && RootCondition->EvaluateCondition(this);
		if (!bHasEvaluationResult || bNewResult != bLastEvaluationResult)
		{
			bLastEvaluationResult = bNewResult;
			bHasEvaluationResult = true;
			ApplyEvaluationResultToReceivers(bLastEvaluationResult);
		}
	}
	while (bReevaluationRequested);

	bIsEvaluating = false;
}

bool APuzzleController::TryGetInputState(FName InputId, FPuzzleSignalState& OutInputState) const
{
	const FPuzzleSignalState* InputState = RuntimeInputCache.Find(InputId);
	if (!InputState)
	{
		OutInputState = FPuzzleSignalState();
		return false;
	}

	OutInputState = *InputState;
	return InputState->bIsValid;
}

bool APuzzleController::IsInputValid(FName InputId) const
{
	FPuzzleSignalState InputState;
	return TryGetInputState(InputId, InputState);
}

bool APuzzleController::IsInputActive(FName InputId) const
{
	FPuzzleSignalState InputState;
	return TryGetInputState(InputId, InputState) && InputState.bIsActive;
}

UPuzzleSignalPayload* APuzzleController::GetInputPayload(FName InputId) const
{
	const FPuzzleSignalState* InputState = RuntimeInputCache.Find(InputId);
	return InputState && InputState->bIsValid ? InputState->Payload.Get() : nullptr;
}

int64 APuzzleController::GetInputRevision(FName InputId) const
{
	const FPuzzleSignalState* InputState = RuntimeInputCache.Find(InputId);
	return InputState && InputState->bIsValid ? InputState->Revision : 0;
}

bool APuzzleController::IsControllerActive() const
{
	return bHasEvaluationResult && bLastEvaluationResult;
}

bool APuzzleController::IsPuzzleControllerConfigurationValid() const
{
	return bConfigurationValid;
}

void APuzzleController::SetPuzzleDebugEnabled(bool bInEnableDebug)
{
	bEnableDebug = bInEnableDebug;
	UpdateDebugTickState();
}

bool APuzzleController::HasConfiguredInput(FName InputId) const
{
	return ConfiguredInputIds.Contains(InputId);
}

void APuzzleController::HandleEmitterSignalChanged(UPuzzleEmitterComponent* Emitter, FGameplayTag SignalTag, FPuzzleSignalState SignalState)
{
	bool bUpdatedAnyInput = false;

	for (const FResolvedInputBinding& Binding : ResolvedInputBindings)
	{
		if (Binding.Emitter.Get() == Emitter && Binding.SignalTag == SignalTag)
		{
			SignalState.bIsValid = true;
			RuntimeInputCache.Add(Binding.InputId, SignalState);
			bUpdatedAnyInput = true;
		}
	}

	if (bUpdatedAnyInput)
	{
		EvaluateController();
	}
}

void APuzzleController::HandleEmitterInvalidated(UPuzzleEmitterComponent* Emitter)
{
	MarkInputsFromEmitterInvalid(Emitter);
	EvaluateController();
}

bool APuzzleController::ValidateAndResolveConfiguration()
{
	bool bIsValidConfiguration = true;

	if (!IsValid(RootCondition))
	{
		PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' has no RootCondition.", *GetNameSafe(this));
		bIsValidConfiguration = false;
	}

	FString ConditionError;
	if (IsValid(RootCondition) && !RootCondition->ValidateCondition(ConditionError))
	{
		PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' has invalid RootCondition: %s", *GetNameSafe(this), *ConditionError);
		bIsValidConfiguration = false;
	}

	TSet<FName> SeenInputIds;
	for (const FPuzzleInputBinding& Binding : InputBindings)
	{
		if (Binding.InputId.IsNone())
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' has an input binding with no InputId.", *GetNameSafe(this));
			bIsValidConfiguration = false;
			continue;
		}

		if (SeenInputIds.Contains(Binding.InputId))
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' has duplicate InputId '%s'.", *GetNameSafe(this), *Binding.InputId.ToString());
			bIsValidConfiguration = false;
			continue;
		}

		SeenInputIds.Add(Binding.InputId);
		ConfiguredInputIds.Add(Binding.InputId);

		if (!Binding.SignalTag.IsValid())
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' input '%s' has an invalid signal tag.", *GetNameSafe(this), *Binding.InputId.ToString());
			bIsValidConfiguration = false;
			continue;
		}

		UPuzzleEmitterComponent* Emitter = nullptr;
		if (!ResolveEmitterComponent(Binding, Emitter))
		{
			bIsValidConfiguration = false;
			continue;
		}

		FResolvedInputBinding& ResolvedBinding = ResolvedInputBindings.AddDefaulted_GetRef();
		ResolvedBinding.InputId = Binding.InputId;
		ResolvedBinding.Emitter = Emitter;
		ResolvedBinding.SignalTag = Binding.SignalTag;
	}

	if (IsValid(RootCondition))
	{
		TSet<FName> ReferencedInputIds;
		RootCondition->GetReferencedInputIds(ReferencedInputIds);
		for (const FName ReferencedInputId : ReferencedInputIds)
		{
			if (!ConfiguredInputIds.Contains(ReferencedInputId))
			{
				PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' condition references unknown InputId '%s'.", *GetNameSafe(this), *ReferencedInputId.ToString());
				bIsValidConfiguration = false;
			}
		}
	}

	TSet<UPuzzleReceiverComponent*> SeenReceivers;
	for (const FPuzzleReceiverBinding& Binding : ReceiverBindings)
	{
		UPuzzleReceiverComponent* Receiver = nullptr;
		if (!ResolveReceiverComponent(Binding, Receiver))
		{
			bIsValidConfiguration = false;
			continue;
		}

		if (SeenReceivers.Contains(Receiver))
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' has duplicate Receiver '%s'.", *GetNameSafe(this), *GetNameSafe(Receiver));
			bIsValidConfiguration = false;
			continue;
		}

		SeenReceivers.Add(Receiver);
		ResolvedReceivers.Add(Receiver);
	}

	if (ResolvedReceivers.IsEmpty())
	{
		PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' has no valid Receivers.", *GetNameSafe(this));
		bIsValidConfiguration = false;
	}

	return bIsValidConfiguration;
}

bool APuzzleController::ResolveEmitterComponent(const FPuzzleInputBinding& Binding, UPuzzleEmitterComponent*& OutEmitter) const
{
	OutEmitter = nullptr;

	if (!IsValid(Binding.EmitterActor))
	{
		PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' input '%s' has no EmitterActor.", *GetNameSafe(this), *Binding.InputId.ToString());
		return false;
	}

	TArray<UPuzzleEmitterComponent*> Components;
	Binding.EmitterActor->GetComponents<UPuzzleEmitterComponent>(Components);

	if (!Binding.EmitterComponentName.IsNone())
	{
		Components.RemoveAll([&Binding](const UPuzzleEmitterComponent* Component)
		{
			return !Component || Component->GetFName() != Binding.EmitterComponentName;
		});
	}

	if (Components.Num() != 1)
	{
		PUZZLESYSTEM_LOG_ERROR(
			"Puzzle Controller '%s' input '%s' expected exactly one Emitter on Actor '%s' using component name '%s' but found %d.",
			*GetNameSafe(this),
			*Binding.InputId.ToString(),
			*GetNameSafe(Binding.EmitterActor),
			*Binding.EmitterComponentName.ToString(),
			Components.Num());
		return false;
	}

	OutEmitter = Components[0];
	return IsValid(OutEmitter);
}

bool APuzzleController::ResolveReceiverComponent(const FPuzzleReceiverBinding& Binding, UPuzzleReceiverComponent*& OutReceiver) const
{
	OutReceiver = nullptr;

	if (!IsValid(Binding.ReceiverActor))
	{
		PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' has a Receiver binding with no ReceiverActor.", *GetNameSafe(this));
		return false;
	}

	TArray<UPuzzleReceiverComponent*> Components;
	Binding.ReceiverActor->GetComponents<UPuzzleReceiverComponent>(Components);

	if (!Binding.ReceiverComponentName.IsNone())
	{
		Components.RemoveAll([&Binding](const UPuzzleReceiverComponent* Component)
		{
			return !Component || Component->GetFName() != Binding.ReceiverComponentName;
		});
	}

	if (Components.Num() != 1)
	{
		PUZZLESYSTEM_LOG_ERROR(
			"Puzzle Controller '%s' expected exactly one Receiver on Actor '%s' using component name '%s' but found %d.",
			*GetNameSafe(this),
			*GetNameSafe(Binding.ReceiverActor),
			*Binding.ReceiverComponentName.ToString(),
			Components.Num());
		return false;
	}

	OutReceiver = Components[0];
	return IsValid(OutReceiver);
}

void APuzzleController::BindEmitters()
{
	TSet<UPuzzleEmitterComponent*> UniqueEmitters;
	for (const FResolvedInputBinding& Binding : ResolvedInputBindings)
	{
		if (UPuzzleEmitterComponent* Emitter = Binding.Emitter.Get())
		{
			UniqueEmitters.Add(Emitter);
		}
	}

	for (UPuzzleEmitterComponent* Emitter : UniqueEmitters)
	{
		Emitter->OnSignalChangedNative.AddUObject(this, &APuzzleController::HandleEmitterSignalChanged);
		Emitter->OnEmitterInvalidatedNative.AddUObject(this, &APuzzleController::HandleEmitterInvalidated);
		BoundEmitters.Add(Emitter);
	}
}

void APuzzleController::UnbindEmitters()
{
	for (const TWeakObjectPtr<UPuzzleEmitterComponent>& EmitterPtr : BoundEmitters)
	{
		if (UPuzzleEmitterComponent* Emitter = EmitterPtr.Get())
		{
			Emitter->OnSignalChangedNative.RemoveAll(this);
			Emitter->OnEmitterInvalidatedNative.RemoveAll(this);
		}
	}
}

void APuzzleController::InitializeInputCache()
{
	for (const FResolvedInputBinding& Binding : ResolvedInputBindings)
	{
		FPuzzleSignalState CurrentState;
		if (UPuzzleEmitterComponent* Emitter = Binding.Emitter.Get())
		{
			if (Emitter->TryGetSignalState(Binding.SignalTag, CurrentState))
			{
				RuntimeInputCache.Add(Binding.InputId, CurrentState);
				continue;
			}
		}

		RuntimeInputCache.Add(Binding.InputId, FPuzzleSignalState());
	}
}

void APuzzleController::ApplyEvaluationResultToReceivers(bool bResult)
{
	for (const TWeakObjectPtr<UPuzzleReceiverComponent>& ReceiverPtr : ResolvedReceivers)
	{
		if (UPuzzleReceiverComponent* Receiver = ReceiverPtr.Get())
		{
			Receiver->SetControllerRequest(this, bResult);
		}
	}

	if (IsPuzzleSystemDebugEnabled())
	{
		PUZZLESYSTEM_LOG_INFO(
			"Puzzle Controller '%s' evaluated %s for %d Receivers.",
			*GetNameSafe(this),
			bResult ? TEXT("active") : TEXT("inactive"),
			ResolvedReceivers.Num());
	}
}

void APuzzleController::MarkInputsFromEmitterInvalid(UPuzzleEmitterComponent* Emitter)
{
	for (const FResolvedInputBinding& Binding : ResolvedInputBindings)
	{
		if (Binding.Emitter.Get() == Emitter)
		{
			RuntimeInputCache.Add(Binding.InputId, FPuzzleSignalState());
		}
	}
}

void APuzzleController::DrawPuzzleDebug() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector ControllerLocation = GetActorLocation();
	FString Label = FString::Printf(TEXT("%s\nResult: %s"), *GetNameSafe(this), IsControllerActive() ? TEXT("Active") : TEXT("Inactive"));

	auto DrawDebugTarget = [World, ControllerLocation](const AActor* TargetActor, const FColor& Color, float Radius, float Thickness)
	{
		if (!TargetActor)
		{
			return;
		}

		const FVector TargetLocation = TargetActor->GetActorLocation();
		DrawDebugLine(World, ControllerLocation, TargetLocation, Color, false, 0.0f, 0, Thickness);
		DrawDebugSphere(World, TargetLocation, Radius, 16, Color, false, 0.0f, 0, Thickness);
	};

	DrawDebugBox(World, ControllerLocation, FVector(24.0f), FColor::Yellow, false, 0.0f, 0, 2.0f);

	if (!ResolvedInputBindings.IsEmpty())
	{
		for (const FResolvedInputBinding& Binding : ResolvedInputBindings)
		{
			const UPuzzleEmitterComponent* Emitter = Binding.Emitter.Get();
			const AActor* EmitterOwner = Emitter ? Emitter->GetOwner() : nullptr;
			DrawDebugTarget(EmitterOwner, FColor::Cyan, 32.0f, 2.0f);

			const FPuzzleSignalState* InputState = RuntimeInputCache.Find(Binding.InputId);
			Label += FString::Printf(
				TEXT("\nIn %s <- %s/%s: %s r%lld"),
				*Binding.InputId.ToString(),
				*GetNameSafe(EmitterOwner),
				*Binding.SignalTag.ToString(),
				InputState && InputState->bIsValid ? (InputState->bIsActive ? TEXT("Active") : TEXT("Inactive")) : TEXT("Invalid"),
				InputState && InputState->bIsValid ? InputState->Revision : 0);
		}
	}
	else
	{
		for (const FPuzzleInputBinding& Binding : InputBindings)
		{
			DrawDebugTarget(Binding.EmitterActor.Get(), FColor::Cyan, 32.0f, 2.0f);
			Label += FString::Printf(
				TEXT("\nIn %s <- %s/%s: Configured"),
				*Binding.InputId.ToString(),
				*GetNameSafe(Binding.EmitterActor.Get()),
				*Binding.SignalTag.ToString());
		}
	}

	if (!ResolvedReceivers.IsEmpty())
	{
		for (const TWeakObjectPtr<UPuzzleReceiverComponent>& ReceiverPtr : ResolvedReceivers)
		{
			const UPuzzleReceiverComponent* Receiver = ReceiverPtr.Get();
			const AActor* ReceiverOwner = Receiver ? Receiver->GetOwner() : nullptr;
			DrawDebugTarget(ReceiverOwner, FColor::Green, 40.0f, 3.0f);

			if (ReceiverOwner)
			{
				Label += FString::Printf(TEXT("\nOut -> %s/%s"), *GetNameSafe(ReceiverOwner), *GetNameSafe(Receiver));
			}
		}
	}
	else
	{
		for (const FPuzzleReceiverBinding& Binding : ReceiverBindings)
		{
			DrawDebugTarget(Binding.ReceiverActor.Get(), FColor::Green, 40.0f, 3.0f);
			Label += FString::Printf(TEXT("\nOut -> %s/%s: Configured"), *GetNameSafe(Binding.ReceiverActor.Get()), *Binding.ReceiverComponentName.ToString());
		}
	}

	DrawDebugString(World, ControllerLocation + FVector(0.0f, 0.0f, DebugVerticalOffset), Label, nullptr, FColor::White, 0.0f, true);
}

bool APuzzleController::ShouldDrawPuzzleDebug() const
{
	return bEnableDebug && IsPuzzleSystemDebugVisualEnabled();
}

void APuzzleController::UpdateDebugTickState()
{
	SetActorTickEnabled(bEnableDebug);
}
