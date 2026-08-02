#include "Types/PerceptionKnowledgeTypes.h"

FPerceptionKnowledgeValue FPerceptionKnowledgeValue::MakeBool(const bool Value)
{
	FPerceptionKnowledgeValue Result;
	Result.Type = EPerceptionKnowledgeValueType::Bool;
	Result.BoolValue = Value;
	return Result;
}

FPerceptionKnowledgeValue FPerceptionKnowledgeValue::MakeInteger(const int64 Value)
{
	FPerceptionKnowledgeValue Result;
	Result.Type = EPerceptionKnowledgeValueType::Integer;
	Result.IntegerValue = Value;
	return Result;
}

FPerceptionKnowledgeValue FPerceptionKnowledgeValue::MakeFloat(const double Value)
{
	FPerceptionKnowledgeValue Result;
	Result.Type = EPerceptionKnowledgeValueType::Float;
	Result.FloatValue = Value;
	return Result;
}

FPerceptionKnowledgeValue FPerceptionKnowledgeValue::MakeName(const FName Value)
{
	FPerceptionKnowledgeValue Result;
	Result.Type = EPerceptionKnowledgeValueType::Name;
	Result.NameValue = Value;
	return Result;
}

FPerceptionKnowledgeValue FPerceptionKnowledgeValue::MakeGameplayTag(const FGameplayTag Value)
{
	FPerceptionKnowledgeValue Result;
	Result.Type = EPerceptionKnowledgeValueType::GameplayTag;
	Result.GameplayTagValue = Value;
	return Result;
}

FPerceptionKnowledgeValue FPerceptionKnowledgeValue::MakeEntityId(const FPerceptionKnowledgeEntityId Value)
{
	FPerceptionKnowledgeValue Result;
	Result.Type = EPerceptionKnowledgeValueType::EntityId;
	Result.EntityIdValue = Value;
	return Result;
}

FPerceptionKnowledgeValue FPerceptionKnowledgeValue::MakeVector(const FVector Value)
{
	FPerceptionKnowledgeValue Result;
	Result.Type = EPerceptionKnowledgeValueType::Vector;
	Result.VectorValue = Value;
	return Result;
}

bool FPerceptionKnowledgeValue::IsValid() const
{
	switch (Type)
	{
	case EPerceptionKnowledgeValueType::Bool:
	case EPerceptionKnowledgeValueType::Integer:
	case EPerceptionKnowledgeValueType::Name:
		return true;
	case EPerceptionKnowledgeValueType::Float:
		return FMath::IsFinite(FloatValue);
	case EPerceptionKnowledgeValueType::GameplayTag:
		return GameplayTagValue.IsValid();
	case EPerceptionKnowledgeValueType::EntityId:
		return EntityIdValue.IsValid();
	case EPerceptionKnowledgeValueType::Vector:
		return !VectorValue.ContainsNaN();
	default:
		return false;
	}
}

FString FPerceptionKnowledgeValue::ToString() const
{
	switch (Type)
	{
	case EPerceptionKnowledgeValueType::Bool:
		return BoolValue ? TEXT("true") : TEXT("false");
	case EPerceptionKnowledgeValueType::Integer:
		return LexToString(IntegerValue);
	case EPerceptionKnowledgeValueType::Float:
		return LexToString(FloatValue);
	case EPerceptionKnowledgeValueType::Name:
		return NameValue.ToString();
	case EPerceptionKnowledgeValueType::GameplayTag:
		return GameplayTagValue.ToString();
	case EPerceptionKnowledgeValueType::EntityId:
		return EntityIdValue.ToString();
	case EPerceptionKnowledgeValueType::Vector:
		return VectorValue.ToCompactString();
	default:
		return TEXT("<invalid>");
	}
}

bool FPerceptionKnowledgeValue::GetBool(bool& OutValue) const
{
	if (Type != EPerceptionKnowledgeValueType::Bool) return false;
	OutValue = BoolValue;
	return true;
}

bool FPerceptionKnowledgeValue::GetInteger(int64& OutValue) const
{
	if (Type != EPerceptionKnowledgeValueType::Integer) return false;
	OutValue = IntegerValue;
	return true;
}

bool FPerceptionKnowledgeValue::GetFloat(double& OutValue) const
{
	if (Type != EPerceptionKnowledgeValueType::Float) return false;
	OutValue = FloatValue;
	return true;
}

bool FPerceptionKnowledgeValue::GetName(FName& OutValue) const
{
	if (Type != EPerceptionKnowledgeValueType::Name) return false;
	OutValue = NameValue;
	return true;
}

bool FPerceptionKnowledgeValue::GetGameplayTag(FGameplayTag& OutValue) const
{
	if (Type != EPerceptionKnowledgeValueType::GameplayTag) return false;
	OutValue = GameplayTagValue;
	return true;
}

bool FPerceptionKnowledgeValue::GetEntityId(FPerceptionKnowledgeEntityId& OutValue) const
{
	if (Type != EPerceptionKnowledgeValueType::EntityId) return false;
	OutValue = EntityIdValue;
	return true;
}

bool FPerceptionKnowledgeValue::GetVector(FVector& OutValue) const
{
	if (Type != EPerceptionKnowledgeValueType::Vector) return false;
	OutValue = VectorValue;
	return true;
}

bool operator==(const FPerceptionKnowledgeValue& Left, const FPerceptionKnowledgeValue& Right)
{
	if (Left.Type != Right.Type)
	{
		return false;
	}
	switch (Left.Type)
	{
	case EPerceptionKnowledgeValueType::Bool:
		return Left.BoolValue == Right.BoolValue;
	case EPerceptionKnowledgeValueType::Integer:
		return Left.IntegerValue == Right.IntegerValue;
	case EPerceptionKnowledgeValueType::Float:
		return Left.FloatValue == Right.FloatValue;
	case EPerceptionKnowledgeValueType::Name:
		return Left.NameValue == Right.NameValue;
	case EPerceptionKnowledgeValueType::GameplayTag:
		return Left.GameplayTagValue == Right.GameplayTagValue;
	case EPerceptionKnowledgeValueType::EntityId:
		return Left.EntityIdValue == Right.EntityIdValue;
	case EPerceptionKnowledgeValueType::Vector:
		return Left.VectorValue == Right.VectorValue;
	default:
		return true;
	}
}

bool FPerceptionKnowledgeExposedState::IsValid() const
{
	return StateTag.IsValid()
		&& !ObservableThroughSenses.IsEmpty()
		&& (Status != EPerceptionKnowledgeFactStatus::Known || Value.IsValid());
}

bool FPerceptionKnowledgeExposedState::IsObservableThrough(const FGameplayTag SenseTag) const
{
	return SenseTag.IsValid() && ObservableThroughSenses.HasTagExact(SenseTag);
}

FPerceptionKnowledgeObservation FPerceptionKnowledgeObservation::FromState(
	const FPerceptionKnowledgeStateObservation& InState)
{
	FPerceptionKnowledgeObservation Result;
	Result.Type = EPerceptionKnowledgeObservationType::State;
	Result.State = InState;
	return Result;
}

FPerceptionKnowledgeObservation FPerceptionKnowledgeObservation::FromEvent(
	const FPerceptionKnowledgeEventObservation& InEvent)
{
	FPerceptionKnowledgeObservation Result;
	Result.Type = EPerceptionKnowledgeObservationType::Event;
	Result.Event = InEvent;
	return Result;
}
