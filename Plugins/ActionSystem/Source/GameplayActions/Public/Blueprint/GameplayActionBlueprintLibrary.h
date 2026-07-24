#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/GameplayActionTypes.h"
#include "GameplayActionBlueprintLibrary.generated.h"

class FProperty;
class UGameplayActionDefinition;
class UGameplayActionInstance;

UCLASS()
class GAMEPLAYACTIONS_API UGameplayActionBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	static bool IsActionHandleValid(FGameplayActionHandle Handle) { return Handle.IsValid(); }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	static bool WasActionRequestCreated(const FGameplayActionRequestCreationResult& Result) { return Result.WasCreated(); }

	UFUNCTION(BlueprintPure, Category = "Gameplay Actions")
	static bool WasActionSubmissionAccepted(const FGameplayActionSubmissionResult& Result) { return Result.IsAccepted(); }

	/**
	 * Authoritative request factory.
	 *
	 * It deep-copies the Definition Property Bag schema and values. Manually constructed request
	 * structs remain uninitialized and are intentionally rejected by Submit Action.
	 */
	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions", meta = (DisplayName = "Create Gameplay Action Request"))
	static FGameplayActionRequestCreationResult CreateActionRequest(UGameplayActionDefinition* Definition);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions|Request")
	static void SetRequestPriority(UPARAM(ref) FGameplayActionRequest& Request, int32 Priority);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions|Request")
	static void ClearRequestPriorityOverride(UPARAM(ref) FGameplayActionRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions|Request")
	static void SetRequestBlockedPolicy(UPARAM(ref) FGameplayActionRequest& Request, EGameplayActionBlockedPolicy BlockedPolicy);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions|Request")
	static void ClearRequestBlockedPolicyOverride(UPARAM(ref) FGameplayActionRequest& Request);

	UFUNCTION(BlueprintCallable, Category = "Gameplay Actions|Request")
	static void SetRequestContext(UPARAM(ref) FGameplayActionRequest& Request, FGameplayTag OriginTag, UObject* Requester, FGameplayActionCorrelationData Correlation);

	/**
	 * Writes an existing Property Bag field using the exact reflected wildcard type.
	 * Missing fields and type mismatches are reported and never mutate the request schema.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Gameplay Actions|Parameters", meta = (CustomStructureParam = "Value", DisplayName = "Set Request Parameter"))
	static void SetRequestParameter(UPARAM(ref) FGameplayActionRequest& Request, FName ParameterName, const int32& Value, EGameplayActionParameterAccessResult& AccessResult);
	DECLARE_FUNCTION(execSetRequestParameter);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Gameplay Actions|Parameters", meta = (CustomStructureParam = "Value", DisplayName = "Get Request Parameter"))
	static void GetRequestParameter(const FGameplayActionRequest& Request, FName ParameterName, UPARAM(ref) int32& Value, EGameplayActionParameterAccessResult& AccessResult);
	DECLARE_FUNCTION(execGetRequestParameter);

	/** Reads an immutable accepted-instance parameter without exposing mutable Property Bag storage. */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Gameplay Actions|Parameters", meta = (CustomStructureParam = "Value", DisplayName = "Get Action Parameter"))
	static void GetActionParameter(const UGameplayActionInstance* Action, FName ParameterName, UPARAM(ref) int32& Value, EGameplayActionParameterAccessResult& AccessResult);
	DECLARE_FUNCTION(execGetActionParameter);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Gameplay Actions|Parameters", meta = (CustomStructureParam = "Value", DisplayName = "Get Action Event Parameter"))
	static void GetEventParameter(const FGameplayActionEvent& Event, FName ParameterName, UPARAM(ref) int32& Value, EGameplayActionParameterAccessResult& AccessResult);
	DECLARE_FUNCTION(execGetEventParameter);

	static EGameplayActionParameterAccessResult SetBagValueFromProperty(
		FInstancedPropertyBag& Bag, FName ParameterName, const FProperty* ValueProperty, const void* ValueAddress);

	static EGameplayActionParameterAccessResult GetBagValueToProperty(
		const FInstancedPropertyBag& Bag, FName ParameterName, const FProperty* ValueProperty, void* ValueAddress);

	/** C++ counterpart of the wildcard setter. ValueAddress points directly to the reflected property value. */
	static EGameplayActionParameterAccessResult SetRequestParameterFromProperty(
		FGameplayActionRequest& Request, FName ParameterName, const FProperty* ValueProperty, const void* ValueAddress);

	/** C++ counterpart of the wildcard request getter. ValueAddress points directly to the reflected output value. */
	static EGameplayActionParameterAccessResult GetRequestParameterToProperty(
		const FGameplayActionRequest& Request, FName ParameterName, const FProperty* ValueProperty, void* ValueAddress);
};
