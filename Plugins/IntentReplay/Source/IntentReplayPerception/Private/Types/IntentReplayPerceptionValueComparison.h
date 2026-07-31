#pragma once

#include "Types/PerceptionKnowledgeTypes.h"

namespace IntentReplayPerception
{
	inline bool AreValuesExactlyEqual(
		const FPerceptionKnowledgeValue& Left,
		const FPerceptionKnowledgeValue& Right)
	{
		if (Left.GetType() != Right.GetType())
		{
			return false;
		}

		switch (Left.GetType())
		{
		case EPerceptionKnowledgeValueType::Bool:
		{
			bool LeftValue = false;
			bool RightValue = false;
			return Left.GetBool(LeftValue)
				&& Right.GetBool(RightValue)
				&& LeftValue == RightValue;
		}
		case EPerceptionKnowledgeValueType::Integer:
		{
			int64 LeftValue = 0;
			int64 RightValue = 0;
			return Left.GetInteger(LeftValue)
				&& Right.GetInteger(RightValue)
				&& LeftValue == RightValue;
		}
		case EPerceptionKnowledgeValueType::Float:
		{
			double LeftValue = 0.0;
			double RightValue = 0.0;
			return Left.GetFloat(LeftValue)
				&& Right.GetFloat(RightValue)
				&& LeftValue == RightValue;
		}
		case EPerceptionKnowledgeValueType::Name:
		{
			FName LeftValue;
			FName RightValue;
			return Left.GetName(LeftValue)
				&& Right.GetName(RightValue)
				&& LeftValue == RightValue;
		}
		case EPerceptionKnowledgeValueType::GameplayTag:
		{
			FGameplayTag LeftValue;
			FGameplayTag RightValue;
			return Left.GetGameplayTag(LeftValue)
				&& Right.GetGameplayTag(RightValue)
				&& LeftValue == RightValue;
		}
		case EPerceptionKnowledgeValueType::EntityId:
		{
			FPerceptionKnowledgeEntityId LeftValue;
			FPerceptionKnowledgeEntityId RightValue;
			return Left.GetEntityId(LeftValue)
				&& Right.GetEntityId(RightValue)
				&& LeftValue == RightValue;
		}
		case EPerceptionKnowledgeValueType::Vector:
		{
			FVector LeftValue = FVector::ZeroVector;
			FVector RightValue = FVector::ZeroVector;
			return Left.GetVector(LeftValue)
				&& Right.GetVector(RightValue)
				&& LeftValue == RightValue;
		}
		case EPerceptionKnowledgeValueType::None:
		default:
			return true;
		}
	}
}
