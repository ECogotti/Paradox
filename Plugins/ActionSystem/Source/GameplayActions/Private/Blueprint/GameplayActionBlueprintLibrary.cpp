#include "Blueprint/GameplayActionBlueprintLibrary.h"

#include "Actions/GameplayActionDefinition.h"
#include "Actions/GameplayActionInstance.h"
#include "UObject/Stack.h"
#include "UObject/UnrealType.h"

FGameplayActionRequestCreationResult UGameplayActionBlueprintLibrary::CreateActionRequest(UGameplayActionDefinition* Definition)
{
	FGameplayActionRequestCreationResult Result;
	if (!IsValid(Definition)
		|| !Definition->InstanceClass
		|| Definition->InstanceClass->HasAnyClassFlags(CLASS_Abstract)
		|| !Definition->ActionTag.IsValid()
		|| !Definition->DefaultParameters.IsValid()
		|| (Definition->OptionalTimeout.bEnabled
			&& (!FMath::IsFinite(Definition->OptionalTimeout.DurationSeconds)
				|| Definition->OptionalTimeout.DurationSeconds < 0.0))
		|| !FMath::IsFinite(Definition->MaxQueueTimeSeconds)
		|| Definition->MaxQueueTimeSeconds < 0.0)
	{
		Result.DiagnosticMessage =
			TEXT("The Definition is null, structurally invalid, or contains a negative/non-finite timeout.");
		return Result;
	}

	Result.Status = EGameplayActionRequestCreationStatus::Created;
	Result.Request.Definition = Definition;
	Result.Request.Parameters = Definition->DefaultParameters;
	Result.Request.bInitialized = true;
	return Result;
}

void UGameplayActionBlueprintLibrary::SetRequestPriority(FGameplayActionRequest& Request, const int32 Priority)
{
	if (Request.bInitialized)
	{
		Request.bOverridePriority = true;
		Request.PriorityOverride = Priority;
	}
}

void UGameplayActionBlueprintLibrary::ClearRequestPriorityOverride(FGameplayActionRequest& Request)
{
	if (Request.bInitialized)
	{
		Request.bOverridePriority = false;
	}
}

void UGameplayActionBlueprintLibrary::SetRequestBlockedPolicy(FGameplayActionRequest& Request, const EGameplayActionBlockedPolicy BlockedPolicy)
{
	if (Request.bInitialized)
	{
		Request.bOverrideBlockedPolicy = true;
		Request.BlockedPolicyOverride = BlockedPolicy;
	}
}

void UGameplayActionBlueprintLibrary::ClearRequestBlockedPolicyOverride(FGameplayActionRequest& Request)
{
	if (Request.bInitialized)
	{
		Request.bOverrideBlockedPolicy = false;
	}
}

void UGameplayActionBlueprintLibrary::SetRequestContext(
	FGameplayActionRequest& Request,
	const FGameplayTag OriginTag,
	UObject* Requester,
	const FGameplayActionCorrelationData Correlation)
{
	if (Request.bInitialized)
	{
		Request.OriginTag = OriginTag;
		Request.Requester = Requester;
		Request.Correlation = Correlation;
	}
}

void UGameplayActionBlueprintLibrary::SetRequestParameter(
	FGameplayActionRequest& Request,
	FName ParameterName,
	const int32& Value,
	EGameplayActionParameterAccessResult& AccessResult)
{
	checkNoEntry();
}

void UGameplayActionBlueprintLibrary::GetRequestParameter(
	const FGameplayActionRequest& Request,
	FName ParameterName,
	int32& Value,
	EGameplayActionParameterAccessResult& AccessResult)
{
	checkNoEntry();
}

void UGameplayActionBlueprintLibrary::GetActionParameter(
	const UGameplayActionInstance* Action,
	FName ParameterName,
	int32& Value,
	EGameplayActionParameterAccessResult& AccessResult)
{
	checkNoEntry();
}

void UGameplayActionBlueprintLibrary::GetEventParameter(
	const FGameplayActionEvent& Event,
	FName ParameterName,
	int32& Value,
	EGameplayActionParameterAccessResult& AccessResult)
{
	checkNoEntry();
}

EGameplayActionParameterAccessResult UGameplayActionBlueprintLibrary::SetBagValueFromProperty(
	FInstancedPropertyBag& Bag,
	const FName ParameterName,
	const FProperty* ValueProperty,
	const void* ValueAddress)
{
	if (!Bag.IsValid())
	{
		return EGameplayActionParameterAccessResult::InvalidValue;
	}

	const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(ParameterName);
	if (!Desc || !Desc->CachedProperty)
	{
		return EGameplayActionParameterAccessResult::ParameterNotFound;
	}
	if (!ValueProperty || !ValueAddress)
	{
		return EGameplayActionParameterAccessResult::InvalidValue;
	}

	if (!Desc->CachedProperty->SameType(ValueProperty))
	{
		return EGameplayActionParameterAccessResult::TypeMismatch;
	}

	const uint8* SourceContainer = static_cast<const uint8*>(ValueAddress) - ValueProperty->GetOffset_ForInternal();
	return Bag.SetValue(ParameterName, ValueProperty, SourceContainer) == EPropertyBagResult::Success
		? EGameplayActionParameterAccessResult::Success
		: EGameplayActionParameterAccessResult::InvalidValue;
}

EGameplayActionParameterAccessResult UGameplayActionBlueprintLibrary::GetBagValueToProperty(
	const FInstancedPropertyBag& Bag,
	const FName ParameterName,
	const FProperty* ValueProperty,
	void* ValueAddress)
{
	if (!Bag.IsValid())
	{
		return EGameplayActionParameterAccessResult::InvalidValue;
	}

	const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(ParameterName);
	if (!Desc || !Desc->CachedProperty)
	{
		return EGameplayActionParameterAccessResult::ParameterNotFound;
	}
	if (!ValueProperty || !ValueAddress)
	{
		return EGameplayActionParameterAccessResult::InvalidValue;
	}

	if (!Desc->CachedProperty->SameType(ValueProperty))
	{
		return EGameplayActionParameterAccessResult::TypeMismatch;
	}

	const FConstStructView BagValue = Bag.GetValue();
	const void* Source = BagValue.IsValid()
		? Desc->CachedProperty->ContainerPtrToValuePtr<void>(BagValue.GetMemory())
		: nullptr;
	if (!Source)
	{
		return EGameplayActionParameterAccessResult::InvalidValue;
	}

	ValueProperty->CopyCompleteValue(ValueAddress, Source);
	return EGameplayActionParameterAccessResult::Success;
}

EGameplayActionParameterAccessResult UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
	FGameplayActionRequest& Request,
	const FName ParameterName,
	const FProperty* ValueProperty,
	const void* ValueAddress)
{
	return Request.bInitialized
		? SetBagValueFromProperty(Request.Parameters, ParameterName, ValueProperty, ValueAddress)
		: EGameplayActionParameterAccessResult::RequestNotInitialized;
}

EGameplayActionParameterAccessResult UGameplayActionBlueprintLibrary::GetRequestParameterToProperty(
	const FGameplayActionRequest& Request,
	const FName ParameterName,
	const FProperty* ValueProperty,
	void* ValueAddress)
{
	return Request.bInitialized
		? GetBagValueToProperty(Request.Parameters, ParameterName, ValueProperty, ValueAddress)
		: EGameplayActionParameterAccessResult::RequestNotInitialized;
}

DEFINE_FUNCTION(UGameplayActionBlueprintLibrary::execSetRequestParameter)
{
	P_GET_STRUCT_REF(FGameplayActionRequest, Request);
	P_GET_PROPERTY(FNameProperty, ParameterName);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.Step(Stack.Object, nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	const void* ValueAddress = Stack.MostRecentPropertyAddress;

	P_GET_ENUM_REF(EGameplayActionParameterAccessResult, AccessResult);
	P_FINISH;

	P_NATIVE_BEGIN;
	AccessResult = SetRequestParameterFromProperty(Request, ParameterName, ValueProperty, ValueAddress);
	P_NATIVE_END;
}

DEFINE_FUNCTION(UGameplayActionBlueprintLibrary::execGetRequestParameter)
{
	P_GET_STRUCT_REF(FGameplayActionRequest, Request);
	P_GET_PROPERTY(FNameProperty, ParameterName);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.Step(Stack.Object, nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	void* ValueAddress = Stack.MostRecentPropertyAddress;

	P_GET_ENUM_REF(EGameplayActionParameterAccessResult, AccessResult);
	P_FINISH;

	P_NATIVE_BEGIN;
	AccessResult = GetRequestParameterToProperty(Request, ParameterName, ValueProperty, ValueAddress);
	P_NATIVE_END;
}

DEFINE_FUNCTION(UGameplayActionBlueprintLibrary::execGetActionParameter)
{
	P_GET_OBJECT(UGameplayActionInstance, Action);
	P_GET_PROPERTY(FNameProperty, ParameterName);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.Step(Stack.Object, nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	void* ValueAddress = Stack.MostRecentPropertyAddress;

	P_GET_ENUM_REF(EGameplayActionParameterAccessResult, AccessResult);
	P_FINISH;

	P_NATIVE_BEGIN;
	AccessResult = IsValid(Action)
		? GetBagValueToProperty(Action->GetParameters(), ParameterName, ValueProperty, ValueAddress)
		: EGameplayActionParameterAccessResult::InvalidValue;
	P_NATIVE_END;
}

DEFINE_FUNCTION(UGameplayActionBlueprintLibrary::execGetEventParameter)
{
	P_GET_STRUCT_REF(FGameplayActionEvent, Event);
	P_GET_PROPERTY(FNameProperty, ParameterName);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	Stack.Step(Stack.Object, nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	void* ValueAddress = Stack.MostRecentPropertyAddress;

	P_GET_ENUM_REF(EGameplayActionParameterAccessResult, AccessResult);
	P_FINISH;

	P_NATIVE_BEGIN;
	AccessResult = GetBagValueToProperty(Event.GetParameters(), ParameterName, ValueProperty, ValueAddress);
	P_NATIVE_END;
}
