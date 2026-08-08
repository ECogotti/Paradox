#include "Controllers/PuzzleController.h"

#include "Conditions/PuzzleCondition.h"
#include "Components/BillboardComponent.h"
#include "DrawDebugHelpers.h"
#include "Emitters/PuzzleEmitterComponent.h"
#include "Engine/Texture2D.h"
#include "Graph/PuzzleGraphSubsystem.h"
#include "PuzzleSystem.h"
#include "Receivers/PuzzleReceiverComponent.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "Engine/Blueprint.h"
#include "Misc/DataValidation.h"
#endif

namespace
{
	const TCHAR* PuzzleControllerIconPath = TEXT("/PuzzleSystem/Textures/T_PuzzleControllerIcon.T_PuzzleControllerIcon");

	FString BuildPrimaryBindingContext(const int32 BindingIndex, const FName InputId)
	{
		return FString::Printf(TEXT("primary input %d ('%s')"), BindingIndex, *InputId.ToString());
	}

	FString BuildGateBindingContext(const int32 BindingIndex, const FName PrimaryInputId, const int32 GateIndex, const FName GateInputId)
	{
		return FString::Printf(
			TEXT("primary input %d ('%s'), gate %d ('%s')"),
			BindingIndex,
			*PrimaryInputId.ToString(),
			GateIndex,
			*GateInputId.ToString());
	}
}

APuzzleController::APuzzleController()
{
	PuzzleBillboardComponent = CreateDefaultSubobject<UBillboardComponent>(TEXT("PuzzleBillboard"));

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
EDataValidationResult APuzzleController::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	const UClass* ObjectClass = GetClass();
	const UBlueprint* GeneratingBlueprint = ObjectClass ? Cast<UBlueprint>(ObjectClass->ClassGeneratedBy) : nullptr;
	const bool bIsSkeletonClass = GeneratingBlueprint && GeneratingBlueprint->SkeletonGeneratedClass == ObjectClass;
	const bool bShouldValidate = GetOutermost() != GetTransientPackage()
		&& !HasAnyFlags(RF_ClassDefaultObject | RF_Transient | RF_BeginDestroyed | RF_FinishDestroyed)
		&& ObjectClass
		&& !ObjectClass->HasAnyClassFlags(CLASS_NewerVersionExists)
		&& !bIsSkeletonClass;
	if (!bShouldValidate)
	{
		return Result;
	}

	if (Result == EDataValidationResult::NotValidated)
	{
		Result = EDataValidationResult::Valid;
	}

	auto AddError = [&Context, &Result](const FString& Error)
	{
		Context.AddError(FText::FromString(Error));
		Result = EDataValidationResult::Invalid;
	};
	auto AddWarning = [&Context](const FString& Warning)
	{
		Context.AddWarning(FText::FromString(Warning));
	};
	auto ResolveEmitterWithoutLogging = [](AActor* EmitterActor, const bool bSpecifyComponent, const FName ComponentName)
	{
		if (!IsValid(EmitterActor))
		{
			return static_cast<UPuzzleEmitterComponent*>(nullptr);
		}

		TArray<UPuzzleEmitterComponent*> Components;
		EmitterActor->GetComponents<UPuzzleEmitterComponent>(Components);
		Components.RemoveAll([](const UPuzzleEmitterComponent* Component)
		{
			return !IsValid(Component);
		});
		if (Components.IsEmpty())
		{
			return static_cast<UPuzzleEmitterComponent*>(nullptr);
		}
		if (!bSpecifyComponent)
		{
			return Components[0];
		}
		if (ComponentName.IsNone())
		{
			return static_cast<UPuzzleEmitterComponent*>(nullptr);
		}
		for (UPuzzleEmitterComponent* Component : Components)
		{
			if (Component->GetFName() == ComponentName)
			{
				return Component;
			}
		}
		return static_cast<UPuzzleEmitterComponent*>(nullptr);
	};

	if (!IsValid(RootCondition))
	{
		AddError(FString::Printf(TEXT("Puzzle Controller '%s' has no RootCondition."), *GetNameSafe(this)));
	}
	else
	{
		FString ConditionError;
		if (!RootCondition->ValidateCondition(ConditionError))
		{
			AddError(FString::Printf(TEXT("Puzzle Controller '%s' has invalid RootCondition: %s"), *GetNameSafe(this), *ConditionError));
		}
	}

	TSet<FName> PrimaryInputIds;
	for (int32 BindingIndex = 0; BindingIndex < InputBindings.Num(); ++BindingIndex)
	{
		const FPuzzleInputBinding& Binding = InputBindings[BindingIndex];
		const FString PrimaryContext = BuildPrimaryBindingContext(BindingIndex, Binding.InputId);
		if (Binding.InputId.IsNone())
		{
			AddError(FString::Printf(TEXT("Puzzle Controller '%s' %s has no InputId."), *GetNameSafe(this), *PrimaryContext));
		}
		else if (PrimaryInputIds.Contains(Binding.InputId))
		{
			AddError(FString::Printf(TEXT("Puzzle Controller '%s' has duplicate primary InputId '%s'."), *GetNameSafe(this), *Binding.InputId.ToString()));
		}
		else
		{
			PrimaryInputIds.Add(Binding.InputId);
		}

		if (!Binding.SignalTag.IsValid())
		{
			AddError(FString::Printf(TEXT("Puzzle Controller '%s' %s has an invalid signal tag."), *GetNameSafe(this), *PrimaryContext));
		}
		if (!ResolveEmitterWithoutLogging(Binding.EmitterActor, Binding.bSpecifyEmitterComponent, Binding.EmitterComponentName))
		{
			AddError(FString::Printf(TEXT("Puzzle Controller '%s' %s cannot resolve its configured Emitter component."), *GetNameSafe(this), *PrimaryContext));
		}

		const bool bHasGateEmitters = !Binding.EmitterGates.IsEmpty();
		const bool bHasGateConditions = !Binding.GateConditions.IsEmpty();
		if (bHasGateEmitters != bHasGateConditions)
		{
			AddWarning(FString::Printf(
				TEXT("Puzzle Controller '%s' %s bypasses gate evaluation because both Emitter Gates and Gate Conditions are required."),
				*GetNameSafe(this),
				*PrimaryContext));
			continue;
		}
		if (!bHasGateEmitters)
		{
			continue;
		}

		TSet<FName> GateInputIds;
		TArray<TPair<TWeakObjectPtr<UPuzzleEmitterComponent>, FGameplayTag>> ResolvedGateSources;
		for (int32 GateIndex = 0; GateIndex < Binding.EmitterGates.Num(); ++GateIndex)
		{
			const FPuzzleEmitterGateBinding& GateBinding = Binding.EmitterGates[GateIndex];
			const FString GateContext = BuildGateBindingContext(BindingIndex, Binding.InputId, GateIndex, GateBinding.InputId);
			if (GateBinding.InputId.IsNone())
			{
				AddError(FString::Printf(TEXT("Puzzle Controller '%s' %s has no gate InputId."), *GetNameSafe(this), *GateContext));
			}
			else if (GateInputIds.Contains(GateBinding.InputId))
			{
				AddError(FString::Printf(TEXT("Puzzle Controller '%s' %s duplicates a gate-local InputId."), *GetNameSafe(this), *GateContext));
			}
			else
			{
				GateInputIds.Add(GateBinding.InputId);
			}

			if (!GateBinding.SignalTag.IsValid())
			{
				AddError(FString::Printf(TEXT("Puzzle Controller '%s' %s has an invalid signal tag."), *GetNameSafe(this), *GateContext));
			}

			UPuzzleEmitterComponent* GateEmitter = ResolveEmitterWithoutLogging(
				GateBinding.EmitterActor,
				GateBinding.bSpecifyEmitterComponent,
				GateBinding.EmitterComponentName);
			if (!GateEmitter)
			{
				AddError(FString::Printf(TEXT("Puzzle Controller '%s' %s cannot resolve its configured Emitter component."), *GetNameSafe(this), *GateContext));
				continue;
			}

			const bool bDuplicateSource = ResolvedGateSources.ContainsByPredicate(
				[GateEmitter, &GateBinding](const TPair<TWeakObjectPtr<UPuzzleEmitterComponent>, FGameplayTag>& Existing)
				{
					return Existing.Key.Get() == GateEmitter && Existing.Value == GateBinding.SignalTag;
				});
			if (bDuplicateSource)
			{
				AddError(FString::Printf(TEXT("Puzzle Controller '%s' %s duplicates an Emitter/signal gate source already used by this primary input."), *GetNameSafe(this), *GateContext));
			}
			else
			{
				ResolvedGateSources.Emplace(GateEmitter, GateBinding.SignalTag);
			}
		}

		for (int32 ConditionIndex = 0; ConditionIndex < Binding.GateConditions.Num(); ++ConditionIndex)
		{
			const UPuzzleCondition* Condition = Binding.GateConditions[ConditionIndex];
			if (!Condition)
			{
				AddError(FString::Printf(
					TEXT("Puzzle Controller '%s' %s has a null Gate Condition at index %d."),
					*GetNameSafe(this),
					*PrimaryContext,
					ConditionIndex));
				continue;
			}
			if (!Condition->IsIn(this))
			{
				AddError(FString::Printf(
					TEXT("Puzzle Controller '%s' %s Gate Condition %d is not an instanced object owned by this Controller."),
					*GetNameSafe(this),
					*PrimaryContext,
					ConditionIndex));
			}

			FString ConditionError;
			if (!Condition->ValidateCondition(ConditionError))
			{
				AddError(FString::Printf(
					TEXT("Puzzle Controller '%s' %s Gate Condition %d is invalid: %s"),
					*GetNameSafe(this),
					*PrimaryContext,
					ConditionIndex,
					*ConditionError));
			}

			TSet<FName> ReferencedInputIds;
			Condition->GetReferencedInputIds(ReferencedInputIds);
			for (const FName ReferencedInputId : ReferencedInputIds)
			{
				if (!GateInputIds.Contains(ReferencedInputId))
				{
					AddError(FString::Printf(
						TEXT("Puzzle Controller '%s' %s Gate Condition %d references unknown gate-local InputId '%s'."),
						*GetNameSafe(this),
						*PrimaryContext,
						ConditionIndex,
						*ReferencedInputId.ToString()));
				}
			}
		}
	}

	if (RootCondition)
	{
		TSet<FName> ReferencedInputIds;
		RootCondition->GetReferencedInputIds(ReferencedInputIds);
		for (const FName ReferencedInputId : ReferencedInputIds)
		{
			if (!PrimaryInputIds.Contains(ReferencedInputId))
			{
				AddError(FString::Printf(
					TEXT("Puzzle Controller '%s' RootCondition references unknown primary InputId '%s'."),
					*GetNameSafe(this),
					*ReferencedInputId.ToString()));
			}
		}
	}

	TSet<UPuzzleReceiverComponent*> ResolvedReceiverSet;
	for (int32 ReceiverIndex = 0; ReceiverIndex < ReceiverBindings.Num(); ++ReceiverIndex)
	{
		const FPuzzleReceiverBinding& Binding = ReceiverBindings[ReceiverIndex];
		UPuzzleReceiverComponent* Receiver = nullptr;
		if (!IsValid(Binding.ReceiverActor))
		{
			AddError(FString::Printf(TEXT("Puzzle Controller '%s' Receiver binding %d has no Actor."), *GetNameSafe(this), ReceiverIndex));
			continue;
		}

		TArray<UPuzzleReceiverComponent*> Components;
		Binding.ReceiverActor->GetComponents<UPuzzleReceiverComponent>(Components);
		Components.RemoveAll([](const UPuzzleReceiverComponent* Component)
		{
			return !IsValid(Component);
		});
		if (!Binding.bSpecifyReceiverComponent)
		{
			Receiver = Components.IsEmpty() ? nullptr : Components[0];
		}
		else if (!Binding.ReceiverComponentName.IsNone())
		{
			if (UPuzzleReceiverComponent** MatchingReceiver = Components.FindByPredicate([&Binding](const UPuzzleReceiverComponent* Component)
			{
				return Component && Component->GetFName() == Binding.ReceiverComponentName;
			}))
			{
				Receiver = *MatchingReceiver;
			}
		}

		if (!Receiver)
		{
			AddError(FString::Printf(TEXT("Puzzle Controller '%s' Receiver binding %d cannot resolve its configured component."), *GetNameSafe(this), ReceiverIndex));
		}
		else if (ResolvedReceiverSet.Contains(Receiver))
		{
			AddError(FString::Printf(TEXT("Puzzle Controller '%s' Receiver binding %d duplicates Receiver '%s'."), *GetNameSafe(this), ReceiverIndex, *GetNameSafe(Receiver)));
		}
		else
		{
			ResolvedReceiverSet.Add(Receiver);
		}
	}
	if (ReceiverBindings.IsEmpty())
	{
		AddError(FString::Printf(TEXT("Puzzle Controller '%s' has no Receiver bindings."), *GetNameSafe(this)));
	}

	return Result;
}

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
	if (UWorld* World = GetWorld())
	{
		if (UPuzzleGraphSubsystem* GraphSubsystem = World->GetSubsystem<UPuzzleGraphSubsystem>())
		{
			GraphSubsystem->RegisterOrRefreshController(this);
		}
	}
	return true;
}

void APuzzleController::ShutdownPuzzleController()
{
	if (UWorld* World = GetWorld())
	{
		if (UPuzzleGraphSubsystem* GraphSubsystem = World->GetSubsystem<UPuzzleGraphSubsystem>())
		{
			GraphSubsystem->UnregisterController(this);
		}
	}

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
	SignalRoutes.Reset();
	ResolvedInputIndices.Reset();
	RuntimeInputCache.Reset();
	ConfiguredInputIds.Reset();

	bIsInitialized = false;
	bConfigurationValid = false;
	bHasEvaluationResult = false;
	bLastEvaluationResult = false;
	bIsEvaluating = false;
	bReevaluationRequested = false;
	ActiveGateEvaluationBindingIndex = INDEX_NONE;
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
		TGuardValue<int32> RootConditionScope(ActiveGateEvaluationBindingIndex, INDEX_NONE);
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
	if (UWorld* World = GetWorld())
	{
		if (UPuzzleGraphSubsystem* GraphSubsystem = World->GetSubsystem<UPuzzleGraphSubsystem>())
		{
			GraphSubsystem->RefreshControllerState(this);
		}
	}
}

bool APuzzleController::TryGetInputState(FName InputId, FPuzzleSignalState& OutInputState) const
{
	const FPuzzleSignalState* InputState = FindConditionInputState(InputId);
	if (!InputState)
	{
		OutInputState = FPuzzleSignalState();
		return false;
	}

	OutInputState = *InputState;
	return InputState->bIsValid;
}

bool APuzzleController::TryGetEffectiveInputState(FName InputId, FPuzzleSignalState& OutInputState) const
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

bool APuzzleController::TryGetRawInputState(FName InputId, FPuzzleSignalState& OutInputState) const
{
	const FResolvedInputBinding* Binding = FindResolvedInput(InputId);
	if (!Binding)
	{
		OutInputState = FPuzzleSignalState();
		return false;
	}

	OutInputState = Binding->RawState;
	return Binding->RawState.bIsValid;
}

bool APuzzleController::TryGetGateInputState(FName PrimaryInputId, FName GateInputId, FPuzzleSignalState& OutInputState) const
{
	const FResolvedInputBinding* Binding = FindResolvedInput(PrimaryInputId);
	if (!Binding || !Binding->bGateEnabled)
	{
		OutInputState = FPuzzleSignalState();
		return false;
	}

	for (const FResolvedGateInputBinding& GateInput : Binding->GateInputs)
	{
		if (GateInput.InputId == GateInputId)
		{
			OutInputState = GateInput.State;
			return GateInput.State.bIsValid;
		}
	}

	OutInputState = FPuzzleSignalState();
	return false;
}

bool APuzzleController::IsInputGateBypassed(FName PrimaryInputId) const
{
	if (const FResolvedInputBinding* Binding = FindResolvedInput(PrimaryInputId))
	{
		return !Binding->bGateEnabled;
	}

	for (const FPuzzleInputBinding& Binding : InputBindings)
	{
		if (Binding.InputId == PrimaryInputId)
		{
			return Binding.EmitterGates.IsEmpty() || Binding.GateConditions.IsEmpty();
		}
	}
	return false;
}

bool APuzzleController::IsInputGateValid(FName PrimaryInputId) const
{
	const FResolvedInputBinding* Binding = FindResolvedInput(PrimaryInputId);
	return Binding && (!Binding->bGateEnabled || Binding->bGateValid);
}

bool APuzzleController::DoesInputGateAllowSignal(FName PrimaryInputId) const
{
	const FResolvedInputBinding* Binding = FindResolvedInput(PrimaryInputId);
	return Binding && (!Binding->bGateEnabled || (Binding->bGateValid && Binding->bGateAllowsSignal));
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
	FPuzzleSignalState InputState;
	return TryGetInputState(InputId, InputState) ? InputState.Payload.Get() : nullptr;
}

int64 APuzzleController::GetInputRevision(FName InputId) const
{
	FPuzzleSignalState InputState;
	return TryGetInputState(InputId, InputState) ? InputState.Revision : 0;
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
	if (ResolvedInputBindings.IsValidIndex(ActiveGateEvaluationBindingIndex))
	{
		return ResolvedInputBindings[ActiveGateEvaluationBindingIndex].ConfiguredGateInputIds.Contains(InputId);
	}
	return ConfiguredInputIds.Contains(InputId);
}

void APuzzleController::HandleEmitterSignalChanged(UPuzzleEmitterComponent* Emitter, FGameplayTag SignalTag, FPuzzleSignalState SignalState)
{
	const FEmitterSignalRoutes* Routes = SignalRoutes.Find(TWeakObjectPtr<UPuzzleEmitterComponent>(Emitter));
	const TArray<FSignalRouteDestination>* Destinations = Routes ? Routes->DestinationsBySignal.Find(SignalTag) : nullptr;
	if (!Destinations)
	{
		return;
	}

	SignalState.bIsValid = true;
	TSet<int32> AffectedPrimaryBindings;
	for (const FSignalRouteDestination& Destination : *Destinations)
	{
		if (!ResolvedInputBindings.IsValidIndex(Destination.PrimaryBindingIndex))
		{
			continue;
		}

		FResolvedInputBinding& PrimaryBinding = ResolvedInputBindings[Destination.PrimaryBindingIndex];
		if (Destination.IsPrimary())
		{
			PrimaryBinding.RawState = SignalState;
		}
		else if (PrimaryBinding.GateInputs.IsValidIndex(Destination.GateBindingIndex))
		{
			PrimaryBinding.GateInputs[Destination.GateBindingIndex].State = SignalState;
		}
		AffectedPrimaryBindings.Add(Destination.PrimaryBindingIndex);
	}

	for (const int32 BindingIndex : AffectedPrimaryBindings)
	{
		RebuildEffectiveInput(BindingIndex);
	}
	if (!AffectedPrimaryBindings.IsEmpty())
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
	else
	{
		FString ConditionError;
		if (!RootCondition->ValidateCondition(ConditionError))
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' has invalid RootCondition: %s", *GetNameSafe(this), *ConditionError);
			bIsValidConfiguration = false;
		}
	}

	TSet<FName> SeenInputIds;
	for (int32 BindingIndex = 0; BindingIndex < InputBindings.Num(); ++BindingIndex)
	{
		const FPuzzleInputBinding& Binding = InputBindings[BindingIndex];
		const FString PrimaryContext = BuildPrimaryBindingContext(BindingIndex, Binding.InputId);
		if (Binding.InputId.IsNone())
		{
			PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' %s has no InputId.", *GetNameSafe(this), *PrimaryContext);
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
			PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' %s has an invalid signal tag.", *GetNameSafe(this), *PrimaryContext);
			bIsValidConfiguration = false;
			continue;
		}

		UPuzzleEmitterComponent* Emitter = nullptr;
		if (!ResolveEmitterComponent(
			Binding.EmitterActor,
			Binding.bSpecifyEmitterComponent,
			Binding.EmitterComponentName,
			PrimaryContext,
			Emitter))
		{
			bIsValidConfiguration = false;
			continue;
		}

		const int32 ResolvedBindingIndex = ResolvedInputBindings.Num();
		FResolvedInputBinding& ResolvedBinding = ResolvedInputBindings.AddDefaulted_GetRef();
		ResolvedBinding.ConfigurationIndex = BindingIndex;
		ResolvedBinding.InputId = Binding.InputId;
		ResolvedBinding.Emitter = Emitter;
		ResolvedBinding.SignalTag = Binding.SignalTag;
		ResolvedBinding.bGateEnabled = !Binding.EmitterGates.IsEmpty() && !Binding.GateConditions.IsEmpty();
		ResolvedInputIndices.Add(Binding.InputId, ResolvedBindingIndex);

		if (!ResolvedBinding.bGateEnabled)
		{
			continue;
		}

		TSet<FName> SeenGateInputIds;
		for (int32 GateIndex = 0; GateIndex < Binding.EmitterGates.Num(); ++GateIndex)
		{
			const FPuzzleEmitterGateBinding& GateBinding = Binding.EmitterGates[GateIndex];
			const FString GateContext = BuildGateBindingContext(BindingIndex, Binding.InputId, GateIndex, GateBinding.InputId);
			if (GateBinding.InputId.IsNone())
			{
				PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' %s has no gate InputId.", *GetNameSafe(this), *GateContext);
				bIsValidConfiguration = false;
				continue;
			}
			if (SeenGateInputIds.Contains(GateBinding.InputId))
			{
				PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' %s duplicates a gate-local InputId.", *GetNameSafe(this), *GateContext);
				bIsValidConfiguration = false;
				continue;
			}

			SeenGateInputIds.Add(GateBinding.InputId);
			ResolvedBinding.ConfiguredGateInputIds.Add(GateBinding.InputId);
			if (!GateBinding.SignalTag.IsValid())
			{
				PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' %s has an invalid signal tag.", *GetNameSafe(this), *GateContext);
				bIsValidConfiguration = false;
				continue;
			}

			UPuzzleEmitterComponent* GateEmitter = nullptr;
			if (!ResolveEmitterComponent(
				GateBinding.EmitterActor,
				GateBinding.bSpecifyEmitterComponent,
				GateBinding.EmitterComponentName,
				GateContext,
				GateEmitter))
			{
				bIsValidConfiguration = false;
				continue;
			}

			const bool bDuplicateGateSource = ResolvedBinding.GateInputs.ContainsByPredicate(
				[GateEmitter, &GateBinding](const FResolvedGateInputBinding& Existing)
				{
					return Existing.Emitter.Get() == GateEmitter && Existing.SignalTag == GateBinding.SignalTag;
				});
			if (bDuplicateGateSource)
			{
				PUZZLESYSTEM_LOG_ERROR(
					"Puzzle Controller '%s' %s duplicates an Emitter/signal gate source already used by this primary input.",
					*GetNameSafe(this),
					*GateContext);
				bIsValidConfiguration = false;
				continue;
			}

			FResolvedGateInputBinding& ResolvedGate = ResolvedBinding.GateInputs.AddDefaulted_GetRef();
			ResolvedGate.InputId = GateBinding.InputId;
			ResolvedGate.Emitter = GateEmitter;
			ResolvedGate.SignalTag = GateBinding.SignalTag;
		}

		for (int32 ConditionIndex = 0; ConditionIndex < Binding.GateConditions.Num(); ++ConditionIndex)
		{
			const UPuzzleCondition* Condition = Binding.GateConditions[ConditionIndex];
			if (!Condition)
			{
				PUZZLESYSTEM_LOG_ERROR(
					"Puzzle Controller '%s' %s has a null Gate Condition at index %d.",
					*GetNameSafe(this),
					*PrimaryContext,
					ConditionIndex);
				bIsValidConfiguration = false;
				continue;
			}
			if (!Condition->IsIn(this))
			{
				PUZZLESYSTEM_LOG_ERROR(
					"Puzzle Controller '%s' %s Gate Condition %d is not an instanced object owned by this Controller.",
					*GetNameSafe(this),
					*PrimaryContext,
					ConditionIndex);
				bIsValidConfiguration = false;
			}

			FString ConditionError;
			if (!Condition->ValidateCondition(ConditionError))
			{
				PUZZLESYSTEM_LOG_ERROR(
					"Puzzle Controller '%s' %s Gate Condition %d is invalid: %s",
					*GetNameSafe(this),
					*PrimaryContext,
					ConditionIndex,
					*ConditionError);
				bIsValidConfiguration = false;
			}

			TSet<FName> ReferencedGateInputIds;
			Condition->GetReferencedInputIds(ReferencedGateInputIds);
			for (const FName ReferencedGateInputId : ReferencedGateInputIds)
			{
				if (!ResolvedBinding.ConfiguredGateInputIds.Contains(ReferencedGateInputId))
				{
					PUZZLESYSTEM_LOG_ERROR(
						"Puzzle Controller '%s' %s Gate Condition %d references unknown gate-local InputId '%s'.",
						*GetNameSafe(this),
						*PrimaryContext,
						ConditionIndex,
						*ReferencedGateInputId.ToString());
					bIsValidConfiguration = false;
				}
			}
		}
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

bool APuzzleController::ResolveEmitterComponent(
	AActor* EmitterActor,
	bool bSpecifyEmitterComponent,
	FName EmitterComponentName,
	const FString& BindingContext,
	UPuzzleEmitterComponent*& OutEmitter) const
{
	OutEmitter = nullptr;
	if (!IsValid(EmitterActor))
	{
		PUZZLESYSTEM_LOG_ERROR("Puzzle Controller '%s' %s has no EmitterActor.", *GetNameSafe(this), *BindingContext);
		return false;
	}

	TArray<UPuzzleEmitterComponent*> Components;
	EmitterActor->GetComponents<UPuzzleEmitterComponent>(Components);
	Components.RemoveAll([](const UPuzzleEmitterComponent* Component)
	{
		return !IsValid(Component);
	});
	if (Components.IsEmpty())
	{
		PUZZLESYSTEM_LOG_ERROR(
			"Puzzle Controller '%s' %s found no Emitter components on Actor '%s'.",
			*GetNameSafe(this),
			*BindingContext,
			*GetNameSafe(EmitterActor));
		return false;
	}

	if (!bSpecifyEmitterComponent)
	{
		OutEmitter = Components[0];
		return true;
	}
	if (EmitterComponentName.IsNone())
	{
		PUZZLESYSTEM_LOG_ERROR(
			"Puzzle Controller '%s' %s explicitly selects an Emitter component on Actor '%s' but has no component name.",
			*GetNameSafe(this),
			*BindingContext,
			*GetNameSafe(EmitterActor));
		return false;
	}

	for (UPuzzleEmitterComponent* Component : Components)
	{
		if (Component->GetFName() == EmitterComponentName)
		{
			OutEmitter = Component;
			return true;
		}
	}

	PUZZLESYSTEM_LOG_ERROR(
		"Puzzle Controller '%s' %s could not find Emitter component '%s' on Actor '%s'.",
		*GetNameSafe(this),
		*BindingContext,
		*EmitterComponentName.ToString(),
		*GetNameSafe(EmitterActor));
	return false;
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
	Components.RemoveAll([](const UPuzzleReceiverComponent* Component)
	{
		return !IsValid(Component);
	});
	if (Components.IsEmpty())
	{
		PUZZLESYSTEM_LOG_ERROR(
			"Puzzle Controller '%s' found no Receiver components on Actor '%s'.",
			*GetNameSafe(this),
			*GetNameSafe(Binding.ReceiverActor));
		return false;
	}

	if (!Binding.bSpecifyReceiverComponent)
	{
		OutReceiver = Components[0];
		return true;
	}
	if (Binding.ReceiverComponentName.IsNone())
	{
		PUZZLESYSTEM_LOG_ERROR(
			"Puzzle Controller '%s' explicitly selects a Receiver component on Actor '%s' but has no component name.",
			*GetNameSafe(this),
			*GetNameSafe(Binding.ReceiverActor));
		return false;
	}

	for (UPuzzleReceiverComponent* Component : Components)
	{
		if (Component->GetFName() == Binding.ReceiverComponentName)
		{
			OutReceiver = Component;
			return true;
		}
	}

	PUZZLESYSTEM_LOG_ERROR(
		"Puzzle Controller '%s' could not find Receiver component '%s' on Actor '%s'.",
		*GetNameSafe(this),
		*Binding.ReceiverComponentName.ToString(),
		*GetNameSafe(Binding.ReceiverActor));
	return false;
}

void APuzzleController::BindEmitters()
{
	for (int32 PrimaryIndex = 0; PrimaryIndex < ResolvedInputBindings.Num(); ++PrimaryIndex)
	{
		const FResolvedInputBinding& PrimaryBinding = ResolvedInputBindings[PrimaryIndex];
		if (UPuzzleEmitterComponent* Emitter = PrimaryBinding.Emitter.Get())
		{
			FSignalRouteDestination Destination;
			Destination.PrimaryBindingIndex = PrimaryIndex;
			const TWeakObjectPtr<UPuzzleEmitterComponent> EmitterKey(Emitter);
			SignalRoutes.FindOrAdd(EmitterKey).DestinationsBySignal.FindOrAdd(PrimaryBinding.SignalTag).Add(Destination);
		}

		for (int32 GateIndex = 0; GateIndex < PrimaryBinding.GateInputs.Num(); ++GateIndex)
		{
			const FResolvedGateInputBinding& GateBinding = PrimaryBinding.GateInputs[GateIndex];
			if (UPuzzleEmitterComponent* GateEmitter = GateBinding.Emitter.Get())
			{
				FSignalRouteDestination Destination;
				Destination.PrimaryBindingIndex = PrimaryIndex;
				Destination.GateBindingIndex = GateIndex;
				const TWeakObjectPtr<UPuzzleEmitterComponent> GateEmitterKey(GateEmitter);
				SignalRoutes.FindOrAdd(GateEmitterKey).DestinationsBySignal.FindOrAdd(GateBinding.SignalTag).Add(Destination);
			}
		}
	}

	for (const TPair<TWeakObjectPtr<UPuzzleEmitterComponent>, FEmitterSignalRoutes>& RoutePair : SignalRoutes)
	{
		if (UPuzzleEmitterComponent* Emitter = RoutePair.Key.Get())
		{
			Emitter->OnSignalChangedNative.AddUObject(this, &APuzzleController::HandleEmitterSignalChanged);
			Emitter->OnEmitterInvalidatedNative.AddUObject(this, &APuzzleController::HandleEmitterInvalidated);
			BoundEmitters.Add(Emitter);
		}
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
	for (FResolvedInputBinding& PrimaryBinding : ResolvedInputBindings)
	{
		PrimaryBinding.RawState = FPuzzleSignalState();
		if (UPuzzleEmitterComponent* Emitter = PrimaryBinding.Emitter.Get())
		{
			Emitter->TryGetSignalState(PrimaryBinding.SignalTag, PrimaryBinding.RawState);
		}

		for (FResolvedGateInputBinding& GateBinding : PrimaryBinding.GateInputs)
		{
			GateBinding.State = FPuzzleSignalState();
			if (UPuzzleEmitterComponent* GateEmitter = GateBinding.Emitter.Get())
			{
				GateEmitter->TryGetSignalState(GateBinding.SignalTag, GateBinding.State);
			}
		}
	}

	for (int32 PrimaryIndex = 0; PrimaryIndex < ResolvedInputBindings.Num(); ++PrimaryIndex)
	{
		RebuildEffectiveInput(PrimaryIndex);
	}
}

void APuzzleController::RebuildEffectiveInput(const int32 PrimaryBindingIndex)
{
	if (!ResolvedInputBindings.IsValidIndex(PrimaryBindingIndex))
	{
		return;
	}

	FResolvedInputBinding& Binding = ResolvedInputBindings[PrimaryBindingIndex];
	if (!Binding.bGateEnabled)
	{
		Binding.bGateValid = true;
		Binding.bGateAllowsSignal = true;
		Binding.LastGateConditionResults.Reset();
		RuntimeInputCache.Add(Binding.InputId, Binding.RawState);
		return;
	}

	Binding.bGateValid = !Binding.GateInputs.IsEmpty();
	TArray<int64> CurrentGateRevisions;
	CurrentGateRevisions.Reserve(Binding.GateInputs.Num());
	for (const FResolvedGateInputBinding& GateInput : Binding.GateInputs)
	{
		Binding.bGateValid &= GateInput.State.bIsValid;
		CurrentGateRevisions.Add(GateInput.State.bIsValid ? GateInput.State.Revision : 0);
	}

	if (Binding.bGateValid)
	{
		Binding.bGateAllowsSignal = EvaluateGateConditions(PrimaryBindingIndex);
	}
	else
	{
		Binding.bGateAllowsSignal = false;
		const int32 ConditionCount = InputBindings.IsValidIndex(Binding.ConfigurationIndex)
			? InputBindings[Binding.ConfigurationIndex].GateConditions.Num()
			: 0;
		Binding.LastGateConditionResults.Init(false, ConditionCount);
	}

	FPuzzleSignalState NewEffectiveState;
	if (Binding.RawState.bIsValid && Binding.bGateValid)
	{
		NewEffectiveState.bIsValid = true;
		NewEffectiveState.bIsActive = Binding.RawState.bIsActive && Binding.bGateAllowsSignal;
		NewEffectiveState.Payload = Binding.bGateAllowsSignal ? Binding.RawState.Payload : nullptr;
	}

	const int64 AdmittedRawRevision = NewEffectiveState.bIsValid && Binding.bGateAllowsSignal
		? Binding.RawState.Revision
		: 0;
	const FPuzzleSignalState* PreviousState = RuntimeInputCache.Find(Binding.InputId);
	const bool bSignatureChanged = !Binding.bHasEffectiveSignature
		|| !PreviousState
		|| PreviousState->bIsValid != NewEffectiveState.bIsValid
		|| PreviousState->bIsActive != NewEffectiveState.bIsActive
		|| PreviousState->Payload != NewEffectiveState.Payload
		|| Binding.LastGateRevisions != CurrentGateRevisions
		|| Binding.LastAdmittedRawRevision != AdmittedRawRevision;
	if (bSignatureChanged)
	{
		++Binding.EffectiveRevision;
	}

	Binding.bHasEffectiveSignature = true;
	Binding.LastGateRevisions = MoveTemp(CurrentGateRevisions);
	Binding.LastAdmittedRawRevision = AdmittedRawRevision;
	NewEffectiveState.Revision = Binding.EffectiveRevision;
	RuntimeInputCache.Add(Binding.InputId, NewEffectiveState);
}

bool APuzzleController::EvaluateGateConditions(const int32 PrimaryBindingIndex)
{
	if (!ResolvedInputBindings.IsValidIndex(PrimaryBindingIndex))
	{
		return false;
	}

	FResolvedInputBinding& Binding = ResolvedInputBindings[PrimaryBindingIndex];
	if (!InputBindings.IsValidIndex(Binding.ConfigurationIndex))
	{
		return false;
	}

	const FPuzzleInputBinding& Configuration = InputBindings[Binding.ConfigurationIndex];
	Binding.LastGateConditionResults.Reset(Configuration.GateConditions.Num());
	TGuardValue<int32> GateScope(ActiveGateEvaluationBindingIndex, PrimaryBindingIndex);
	bool bAllConditionsPass = !Configuration.GateConditions.IsEmpty();
	for (const TObjectPtr<UPuzzleCondition>& Condition : Configuration.GateConditions)
	{
		const bool bConditionResult = IsValid(Condition) && Condition->EvaluateCondition(this);
		Binding.LastGateConditionResults.Add(bConditionResult);
		bAllConditionsPass &= bConditionResult;
	}
	return bAllConditionsPass;
}

const APuzzleController::FResolvedInputBinding* APuzzleController::FindResolvedInput(FName PrimaryInputId) const
{
	const int32* BindingIndex = ResolvedInputIndices.Find(PrimaryInputId);
	return BindingIndex && ResolvedInputBindings.IsValidIndex(*BindingIndex)
		? &ResolvedInputBindings[*BindingIndex]
		: nullptr;
}

const FPuzzleSignalState* APuzzleController::FindConditionInputState(FName InputId) const
{
	if (ResolvedInputBindings.IsValidIndex(ActiveGateEvaluationBindingIndex))
	{
		const FResolvedInputBinding& Binding = ResolvedInputBindings[ActiveGateEvaluationBindingIndex];
		for (const FResolvedGateInputBinding& GateInput : Binding.GateInputs)
		{
			if (GateInput.InputId == InputId)
			{
				return &GateInput.State;
			}
		}
		return nullptr;
	}
	return RuntimeInputCache.Find(InputId);
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
	const FEmitterSignalRoutes* Routes = SignalRoutes.Find(TWeakObjectPtr<UPuzzleEmitterComponent>(Emitter));
	if (!Routes)
	{
		return;
	}

	TSet<int32> AffectedPrimaryBindings;
	for (const TPair<FGameplayTag, TArray<FSignalRouteDestination>>& SignalRoute : Routes->DestinationsBySignal)
	{
		for (const FSignalRouteDestination& Destination : SignalRoute.Value)
		{
			if (!ResolvedInputBindings.IsValidIndex(Destination.PrimaryBindingIndex))
			{
				continue;
			}

			FResolvedInputBinding& PrimaryBinding = ResolvedInputBindings[Destination.PrimaryBindingIndex];
			if (Destination.IsPrimary())
			{
				PrimaryBinding.RawState = FPuzzleSignalState();
			}
			else if (PrimaryBinding.GateInputs.IsValidIndex(Destination.GateBindingIndex))
			{
				PrimaryBinding.GateInputs[Destination.GateBindingIndex].State = FPuzzleSignalState();
			}
			AffectedPrimaryBindings.Add(Destination.PrimaryBindingIndex);
		}
	}

	for (const int32 BindingIndex : AffectedPrimaryBindings)
	{
		RebuildEffectiveInput(BindingIndex);
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
	const FVector RelationshipOffset = GetActorRightVector() * 5.0f;
	FString Label = FString::Printf(TEXT("%s\nResult: %s"), *GetNameSafe(this), IsControllerActive() ? TEXT("Active") : TEXT("Inactive"));

	auto DrawDebugTarget = [World, ControllerLocation](const AActor* TargetActor, const FColor& Color, float Radius, float Thickness, const FVector& Offset)
	{
		if (!TargetActor)
		{
			return;
		}
		const FVector TargetLocation = TargetActor->GetActorLocation() + Offset;
		DrawDebugLine(World, ControllerLocation + Offset, TargetLocation, Color, false, 0.0f, 0, Thickness);
		DrawDebugSphere(World, TargetLocation, Radius, 16, Color, false, 0.0f, 0, Thickness);
	};

	DrawDebugBox(World, ControllerLocation, FVector(24.0f), FColor::Yellow, false, 0.0f, 0, 2.0f);
	for (const FResolvedInputBinding& Binding : ResolvedInputBindings)
	{
		const UPuzzleEmitterComponent* Emitter = Binding.Emitter.Get();
		const AActor* EmitterOwner = Emitter ? Emitter->GetOwner() : nullptr;
		DrawDebugTarget(EmitterOwner, FColor::Cyan, 32.0f, 2.0f, -RelationshipOffset);

		const FPuzzleSignalState* EffectiveState = RuntimeInputCache.Find(Binding.InputId);
		const TCHAR* GateMode = !Binding.bGateEnabled
			? TEXT("Bypassed")
			: (!Binding.bGateValid ? TEXT("Invalid") : (Binding.bGateAllowsSignal ? TEXT("Open") : TEXT("Closed")));
		Label += FString::Printf(
			TEXT("\nIn %s/%s Raw=%s:%s r%lld Gate=%s Effective=%s:%s r%lld"),
			*Binding.InputId.ToString(),
			*Binding.SignalTag.ToString(),
			Binding.RawState.bIsValid ? TEXT("Valid") : TEXT("Invalid"),
			Binding.RawState.bIsActive ? TEXT("Active") : TEXT("Inactive"),
			Binding.RawState.Revision,
			GateMode,
			EffectiveState && EffectiveState->bIsValid ? TEXT("Valid") : TEXT("Invalid"),
			EffectiveState && EffectiveState->bIsActive ? TEXT("Active") : TEXT("Inactive"),
			EffectiveState ? EffectiveState->Revision : 0);

		for (const FResolvedGateInputBinding& GateInput : Binding.GateInputs)
		{
			const UPuzzleEmitterComponent* GateEmitter = GateInput.Emitter.Get();
			const AActor* GateOwner = GateEmitter ? GateEmitter->GetOwner() : nullptr;
			DrawDebugTarget(GateOwner, FColor::Red, 30.0f, 2.0f, RelationshipOffset);
			Label += FString::Printf(
				TEXT("\n  Gate %s/%s=%s:%s r%lld Payload=%s"),
				*GateInput.InputId.ToString(),
				*GateInput.SignalTag.ToString(),
				GateInput.State.bIsValid ? TEXT("Valid") : TEXT("Invalid"),
				GateInput.State.bIsActive ? TEXT("Active") : TEXT("Inactive"),
				GateInput.State.Revision,
				*GetNameSafe(GateInput.State.Payload ? GateInput.State.Payload->GetClass() : nullptr));
		}
		if (Binding.bGateEnabled && !Binding.LastGateConditionResults.IsEmpty())
		{
			Label += TEXT("\n  Conditions:");
			for (int32 ConditionIndex = 0; ConditionIndex < Binding.LastGateConditionResults.Num(); ++ConditionIndex)
			{
				Label += FString::Printf(TEXT(" %d=%s"), ConditionIndex, Binding.LastGateConditionResults[ConditionIndex] ? TEXT("True") : TEXT("False"));
			}
		}
	}

	if (ResolvedInputBindings.IsEmpty())
	{
		for (const FPuzzleInputBinding& Binding : InputBindings)
		{
			DrawDebugTarget(Binding.EmitterActor.Get(), FColor::Cyan, 32.0f, 2.0f, -RelationshipOffset);
			for (const FPuzzleEmitterGateBinding& GateBinding : Binding.EmitterGates)
			{
				DrawDebugTarget(GateBinding.EmitterActor.Get(), FColor::Red, 30.0f, 2.0f, RelationshipOffset);
			}
		}
	}

	if (!ResolvedReceivers.IsEmpty())
	{
		for (const TWeakObjectPtr<UPuzzleReceiverComponent>& ReceiverPtr : ResolvedReceivers)
		{
			const UPuzzleReceiverComponent* Receiver = ReceiverPtr.Get();
			const AActor* ReceiverOwner = Receiver ? Receiver->GetOwner() : nullptr;
			DrawDebugTarget(ReceiverOwner, FColor::Green, 40.0f, 3.0f, FVector::ZeroVector);
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
			DrawDebugTarget(Binding.ReceiverActor.Get(), FColor::Green, 40.0f, 3.0f, FVector::ZeroVector);
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
