#include "Policies/IntentRecordabilityPolicy.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/AnsiStrProperty.h"
#include "UObject/UnrealType.h"
#include "UObject/Utf8StrProperty.h"

namespace
{
	/** Builds one consistent first-failure result without losing nested property context. */
	FIntentRecordabilityResult MakeRecordabilityFailure(
		const EIntentRecordabilityStatus Status,
		const FString& Path,
		const FProperty* Property,
		const FString& Message)
	{
		FIntentRecordabilityResult Result;
		Result.Status = Status;
		Result.ParameterPath = Path;
		Result.PropertyType = Property ? Property->GetClass()->GetName() : TEXT("Unknown");
		Result.DiagnosticMessage = Message;
		return Result;
	}
}

FIntentRecordabilityResult UIntentRecordabilityPolicy::ValidatePropertyBag(
	const FInstancedPropertyBag& PropertyBag) const
{
	if (!PropertyBag.IsValid() || !PropertyBag.GetPropertyBagStruct() || !PropertyBag.GetValue().IsValid())
	{
		return MakeRecordabilityFailure(
			EIntentRecordabilityStatus::InvalidPropertyBag,
			TEXT("Parameters"),
			nullptr,
			TEXT("The Property Bag is not initialized."));
	}

	const void* BagMemory = PropertyBag.GetValue().GetMemory();
	// Property Bags generate a UStruct at runtime. Reflection is the only reliable way to validate
	// every nested field without assuming a schema owned by GameplayActions or by the game.
	for (TFieldIterator<FProperty> It(PropertyBag.GetPropertyBagStruct()); It; ++It)
	{
		const FProperty* Property = *It;
		const void* Value = Property->ContainerPtrToValuePtr<void>(BagMemory);
		FIntentRecordabilityResult Result = ValidateProperty(Property, Value, Property->GetName());
		if (!Result.IsRecordable())
		{
			return Result;
		}
	}

	return FIntentRecordabilityResult();
}

FIntentRecordabilityResult UIntentRecordabilityPolicy::ValidateProperty(
	const FProperty* Property,
	const void* ValueAddress,
	const FString& PropertyPath) const
{
	if (!Property || !ValueAddress)
	{
		return MakeRecordabilityFailure(
			EIntentRecordabilityStatus::UnsupportedProperty,
			PropertyPath,
			Property,
			TEXT("The reflected property or value address is invalid."));
	}

	if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
	{
		// Validate actual elements, not just the Inner descriptor: object safety depends on values.
		FScriptArrayHelper Helper(ArrayProperty, ValueAddress);
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			const FString ChildPath = FString::Printf(TEXT("%s[%d]"), *PropertyPath, Index);
			FIntentRecordabilityResult Result = ValidateProperty(
				ArrayProperty->Inner,
				Helper.GetRawPtr(Index),
				ChildPath);
			if (!Result.IsRecordable())
			{
				return Result;
			}
		}
		return FIntentRecordabilityResult();
	}

	if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
	{
		// Script-set indexes may contain holes; only live slots have initialized values.
		FScriptSetHelper Helper(SetProperty, ValueAddress);
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
		{
			if (!Helper.IsValidIndex(Index))
			{
				continue;
			}
			const FString ChildPath = FString::Printf(TEXT("%s{%d}"), *PropertyPath, Index);
			FIntentRecordabilityResult Result = ValidateProperty(
				SetProperty->ElementProp,
				Helper.GetElementPtr(Index),
				ChildPath);
			if (!Result.IsRecordable())
			{
				return Result;
			}
		}
		return FIntentRecordabilityResult();
	}

	if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
	{
		// Keys and values can independently contain unsafe references, so both paths are reported.
		FScriptMapHelper Helper(MapProperty, ValueAddress);
		for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
		{
			if (!Helper.IsValidIndex(Index))
			{
				continue;
			}
			FIntentRecordabilityResult KeyResult = ValidateProperty(
				MapProperty->KeyProp,
				Helper.GetKeyPtr(Index),
				FString::Printf(TEXT("%s{%d}.Key"), *PropertyPath, Index));
			if (!KeyResult.IsRecordable())
			{
				return KeyResult;
			}
			FIntentRecordabilityResult ValueResult = ValidateProperty(
				MapProperty->ValueProp,
				Helper.GetValuePtr(Index),
				FString::Printf(TEXT("%s{%d}.Value"), *PropertyPath, Index));
			if (!ValueResult.IsRecordable())
			{
				return ValueResult;
			}
		}
		return FIntentRecordabilityResult();
	}

	if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
	{
		// Recursive reflection also covers engine structs embedded in soft paths and user payloads.
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
		{
			const FProperty* ChildProperty = *It;
			const void* ChildValue = ChildProperty->ContainerPtrToValuePtr<void>(ValueAddress);
			FIntentRecordabilityResult Result = ValidateProperty(
				ChildProperty,
				ChildValue,
				PropertyPath + TEXT(".") + ChildProperty->GetName());
			if (!Result.IsRecordable())
			{
				return Result;
			}
		}
		return FIntentRecordabilityResult();
	}

	if (CastField<FSoftObjectProperty>(Property) || CastField<FSoftClassProperty>(Property))
	{
		// Soft references carry stable asset identity without retaining a runtime UObject instance.
		return FIntentRecordabilityResult();
	}

	if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
	{
		// UClass objects are stable package assets and are safe to deep-copy as class identity.
		return FIntentRecordabilityResult();
	}

	if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
	{
		UObject* Object = ObjectProperty->GetObjectPropertyValue(ValueAddress);
		if (!Object)
		{
			return FIntentRecordabilityResult();
		}
		if (Object->IsA<AActor>() || Object->IsA<UActorComponent>())
		{
			return MakeRecordabilityFailure(
				EIntentRecordabilityStatus::RuntimeObjectReference,
				PropertyPath,
				Property,
				FString::Printf(TEXT("Runtime world object '%s' is not replay-safe."), *GetNameSafe(Object)));
		}
		if (Object->HasAnyFlags(RF_Transient))
		{
			return MakeRecordabilityFailure(
				EIntentRecordabilityStatus::TransientObjectReference,
				PropertyPath,
				Property,
				FString::Printf(TEXT("Transient object '%s' is not replay-safe."), *GetNameSafe(Object)));
		}
		if (!Object->IsAsset())
		{
			return MakeRecordabilityFailure(
				EIntentRecordabilityStatus::InvalidObjectReference,
				PropertyPath,
				Property,
				FString::Printf(TEXT("Hard object reference '%s' is not a stable asset."), *GetNameSafe(Object)));
		}
		return FIntentRecordabilityResult();
	}

	// String variants are listed explicitly because UE 5.8 soft-path internals can expose ANSI/UTF-8
	// reflected fields even when the public parameter is an ordinary deterministic soft reference.
	if (CastField<FBoolProperty>(Property)
		|| CastField<FNumericProperty>(Property)
		|| CastField<FEnumProperty>(Property)
		|| CastField<FNameProperty>(Property)
		|| CastField<FStrProperty>(Property)
		|| CastField<FAnsiStrProperty>(Property)
		|| CastField<FUtf8StrProperty>(Property)
		|| CastField<FTextProperty>(Property))
	{
		return FIntentRecordabilityResult();
	}

	return MakeRecordabilityFailure(
		EIntentRecordabilityStatus::UnsupportedProperty,
		PropertyPath,
		Property,
		FString::Printf(TEXT("Property type '%s' is not supported by the default recordability policy."), *Property->GetClass()->GetName()));
}
