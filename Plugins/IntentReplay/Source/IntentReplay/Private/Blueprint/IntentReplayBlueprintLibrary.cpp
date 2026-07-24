#include "Blueprint/IntentReplayBlueprintLibrary.h"

#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "UObject/Stack.h"

void UIntentReplayBlueprintLibrary::GetRecordedIntentParameter(
	const FRecordedIntent& RecordedIntent,
	FName ParameterName,
	int32& Value,
	EGameplayActionParameterAccessResult& AccessResult)
{
	// CustomThunk functions must never execute their placeholder native body. UHT routes Blueprint
	// bytecode to execGetRecordedIntentParameter, where the wildcard pin's actual FProperty is known.
	checkNoEntry();
}

DEFINE_FUNCTION(UIntentReplayBlueprintLibrary::execGetRecordedIntentParameter)
{
	P_GET_STRUCT_REF(FRecordedIntent, RecordedIntent);
	P_GET_PROPERTY(FNameProperty, ParameterName);

	Stack.MostRecentPropertyAddress = nullptr;
	Stack.MostRecentPropertyContainer = nullptr;
	// Step the wildcard manually so GameplayActions can perform the same reflected, type-safe access
	// used by its request parameter nodes without exposing mutable Property Bag memory.
	Stack.Step(Stack.Object, nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	void* ValueAddress = Stack.MostRecentPropertyAddress;

	P_GET_ENUM_REF(EGameplayActionParameterAccessResult, AccessResult);
	P_FINISH;

	P_NATIVE_BEGIN;
	AccessResult = UGameplayActionBlueprintLibrary::GetBagValueToProperty(
		RecordedIntent.GetParameters(),
		ParameterName,
		ValueProperty,
		ValueAddress);
	P_NATIVE_END;
}
