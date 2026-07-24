#include "Types/GameplayActionExecutionSpec.h"

#include "AIController.h"
#include "Actions/GameplayActionDefinition.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Struct.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Blueprint/GameplayActionBlueprintLibrary.h"
#include "Components/GameplayActionComponent.h"
#include "GameFramework/Pawn.h"

namespace
{
	bool CopyBagValues(const FInstancedPropertyBag& Source, FInstancedPropertyBag& Destination)
	{
		if (!Source.IsValid() || !Destination.IsValid())
		{
			return false;
		}

		const FConstStructView SourceValue = Source.GetValue();
		if (!SourceValue.IsValid())
		{
			return false;
		}

		for (const FPropertyBagPropertyDesc& SourceDesc : Source.GetPropertyBagStruct()->GetPropertyDescs())
		{
			if (!SourceDesc.CachedProperty)
			{
				return false;
			}

			const FPropertyBagPropertyDesc* DestinationDesc = Destination.FindPropertyDescByName(SourceDesc.Name);
			if (!DestinationDesc || !DestinationDesc->CachedProperty
				|| !DestinationDesc->CachedProperty->SameType(SourceDesc.CachedProperty))
			{
				continue;
			}

			const void* SourceAddress =
				SourceDesc.CachedProperty->ContainerPtrToValuePtr<void>(SourceValue.GetMemory());
			if (UGameplayActionBlueprintLibrary::SetBagValueFromProperty(
					Destination,
					SourceDesc.Name,
					SourceDesc.CachedProperty,
					SourceAddress) != EGameplayActionParameterAccessResult::Success)
			{
				return false;
			}
		}
		return true;
	}

	bool CopyBagToRequest(
		const FInstancedPropertyBag& Source,
		FGameplayActionRequest& Request,
		FString& OutDiagnostic)
	{
		const FConstStructView SourceValue = Source.GetValue();
		if (!Source.IsValid() || !SourceValue.IsValid())
		{
			OutDiagnostic = TEXT("The execution spec parameter bag is invalid.");
			return false;
		}

		for (const FPropertyBagPropertyDesc& Desc : Source.GetPropertyBagStruct()->GetPropertyDescs())
		{
			if (!Desc.CachedProperty)
			{
				OutDiagnostic = FString::Printf(
					TEXT("Parameter '%s' has no reflected property."),
					*Desc.Name.ToString());
				return false;
			}

			const void* ValueAddress = Desc.CachedProperty->ContainerPtrToValuePtr<void>(SourceValue.GetMemory());
			const EGameplayActionParameterAccessResult Access =
				UGameplayActionBlueprintLibrary::SetRequestParameterFromProperty(
					Request,
					Desc.Name,
					Desc.CachedProperty,
					ValueAddress);
			if (Access != EGameplayActionParameterAccessResult::Success)
			{
				OutDiagnostic = FString::Printf(
					TEXT("Parameter '%s' could not be copied into the request (%s)."),
					*Desc.Name.ToString(),
					*UEnum::GetValueAsString(Access));
				return false;
			}
		}
		return true;
	}

	const FBlackboardEntry* ResolveBlackboardEntry(
		const UBlackboardComponent& Blackboard,
		const FBlackboardKeySelector& Selector,
		FBlackboard::FKey& OutKeyId,
		FString& OutDiagnostic)
	{
		if (Selector.SelectedKeyName.IsNone())
		{
			OutDiagnostic = TEXT("A Blackboard binding has no selected key.");
			return nullptr;
		}

		const UBlackboardData* Asset = Blackboard.GetBlackboardAsset();
		OutKeyId = Blackboard.GetKeyID(Selector.SelectedKeyName);
		const FBlackboardEntry* Entry =
			Asset && Asset->IsValidKey(OutKeyId) ? Asset->GetKey(OutKeyId) : nullptr;
		if (!Entry || !Entry->KeyType)
		{
			OutDiagnostic = FString::Printf(
				TEXT("Blackboard key '%s' is missing or has no type."),
				*Selector.SelectedKeyName.ToString());
		}
		return Entry;
	}

	bool ApplyBlackboardValue(
		const UBlackboardComponent& Blackboard,
		const FGameplayActionBlackboardParameterBinding& Binding,
		FInstancedPropertyBag& Parameters,
		FString& OutDiagnostic)
	{
		const FPropertyBagPropertyDesc* Desc = Parameters.FindPropertyDescByName(Binding.ParameterName);
		if (!Desc)
		{
			OutDiagnostic = FString::Printf(
				TEXT("Definition has no parameter named '%s'; bindings cannot mutate its schema."),
				*Binding.ParameterName.ToString());
			return false;
		}
		if (!Desc->ContainerTypes.IsEmpty())
		{
			OutDiagnostic = FString::Printf(
				TEXT("Blackboard binding for container parameter '%s' is not supported."),
				*Binding.ParameterName.ToString());
			return false;
		}

		FBlackboard::FKey KeyId = FBlackboard::InvalidKey;
		const FBlackboardEntry* Entry =
			ResolveBlackboardEntry(Blackboard, Binding.BlackboardKey, KeyId, OutDiagnostic);
		if (!Entry)
		{
			return false;
		}

		const FName ParameterName = Binding.ParameterName;
		EPropertyBagResult Result = EPropertyBagResult::TypeMismatch;

		if (Entry->KeyType->IsA<UBlackboardKeyType_Bool>() && Desc->ValueType == EPropertyBagPropertyType::Bool)
		{
			Result = Parameters.SetValueBool(ParameterName, Blackboard.GetValue<UBlackboardKeyType_Bool>(KeyId));
		}
		else if (Entry->KeyType->IsA<UBlackboardKeyType_Int>() && Desc->ValueType == EPropertyBagPropertyType::Int32)
		{
			Result = Parameters.SetValueInt32(ParameterName, Blackboard.GetValue<UBlackboardKeyType_Int>(KeyId));
		}
		else if (Entry->KeyType->IsA<UBlackboardKeyType_Float>() && Desc->ValueType == EPropertyBagPropertyType::Float)
		{
			Result = Parameters.SetValueFloat(ParameterName, Blackboard.GetValue<UBlackboardKeyType_Float>(KeyId));
		}
		else if (const UBlackboardKeyType_Enum* EnumKey = Cast<UBlackboardKeyType_Enum>(Entry->KeyType))
		{
			const UEnum* ExpectedEnum = Cast<UEnum>(Desc->ValueTypeObject);
			if (Desc->ValueType == EPropertyBagPropertyType::Enum && EnumKey->EnumType == ExpectedEnum)
			{
				Result = Parameters.SetValueEnum(
					ParameterName,
					Blackboard.GetValue<UBlackboardKeyType_Enum>(KeyId),
					ExpectedEnum);
			}
			else if (Desc->ValueType == EPropertyBagPropertyType::Byte && EnumKey->EnumType == nullptr)
			{
				Result = Parameters.SetValueByte(
					ParameterName,
					Blackboard.GetValue<UBlackboardKeyType_Enum>(KeyId));
			}
		}
		else if (Entry->KeyType->IsA<UBlackboardKeyType_Name>() && Desc->ValueType == EPropertyBagPropertyType::Name)
		{
			Result = Parameters.SetValueName(ParameterName, Blackboard.GetValue<UBlackboardKeyType_Name>(KeyId));
		}
		else if (Entry->KeyType->IsA<UBlackboardKeyType_String>() && Desc->ValueType == EPropertyBagPropertyType::String)
		{
			Result = Parameters.SetValueString(ParameterName, Blackboard.GetValue<UBlackboardKeyType_String>(KeyId));
		}
		else if (Entry->KeyType->IsA<UBlackboardKeyType_Vector>()
			&& Desc->ValueType == EPropertyBagPropertyType::Struct
			&& Desc->ValueTypeObject == TBaseStructure<FVector>::Get())
		{
			Result = Parameters.SetValueStruct(ParameterName, Blackboard.GetValue<UBlackboardKeyType_Vector>(KeyId));
		}
		else if (Entry->KeyType->IsA<UBlackboardKeyType_Rotator>()
			&& Desc->ValueType == EPropertyBagPropertyType::Struct
			&& Desc->ValueTypeObject == TBaseStructure<FRotator>::Get())
		{
			Result = Parameters.SetValueStruct(ParameterName, Blackboard.GetValue<UBlackboardKeyType_Rotator>(KeyId));
		}
		else if (Entry->KeyType->IsA<UBlackboardKeyType_Object>()
			&& (Desc->ValueType == EPropertyBagPropertyType::Object
				|| Desc->ValueType == EPropertyBagPropertyType::SoftObject))
		{
			UObject* Value = Blackboard.GetValue<UBlackboardKeyType_Object>(KeyId);
			Result = Desc->ValueType == EPropertyBagPropertyType::Object
				? Parameters.SetValueObject(ParameterName, Value)
				: Parameters.SetValueSoftPath(ParameterName, Value);
		}
		else if (Entry->KeyType->IsA<UBlackboardKeyType_Class>()
			&& (Desc->ValueType == EPropertyBagPropertyType::Class
				|| Desc->ValueType == EPropertyBagPropertyType::SoftClass))
		{
			UClass* Value = Blackboard.GetValue<UBlackboardKeyType_Class>(KeyId);
			Result = Desc->ValueType == EPropertyBagPropertyType::Class
				? Parameters.SetValueClass(ParameterName, Value)
				: Parameters.SetValueSoftPath(ParameterName, Value);
		}
		else if (const UBlackboardKeyType_Struct* StructKey = Cast<UBlackboardKeyType_Struct>(Entry->KeyType))
		{
			const FConstStructView StructValue = Blackboard.GetValue<UBlackboardKeyType_Struct>(KeyId);
			if (Desc->ValueType == EPropertyBagPropertyType::Struct
				&& StructValue.IsValid()
				&& StructValue.GetScriptStruct() == Desc->ValueTypeObject)
			{
				Result = Parameters.SetValueStruct(ParameterName, StructValue);
			}
			else if ((Desc->ValueType == EPropertyBagPropertyType::SoftObject
					|| Desc->ValueType == EPropertyBagPropertyType::SoftClass)
				&& StructKey->DefaultValue.GetScriptStruct() == TBaseStructure<FSoftObjectPath>::Get()
				&& StructValue.IsValid())
			{
				Result = Parameters.SetValueSoftPath(ParameterName, StructValue.Get<FSoftObjectPath>());
			}
		}

		if (Result != EPropertyBagResult::Success)
		{
			OutDiagnostic = FString::Printf(
				TEXT("Blackboard key '%s' is not type-compatible with parameter '%s'."),
				*Binding.BlackboardKey.SelectedKeyName.ToString(),
				*ParameterName.ToString());
			return false;
		}
		return true;
	}

	UGameplayActionComponent* ResolveUniqueComponent(AActor* Actor, FString& OutDiagnostic)
	{
		if (!IsValid(Actor))
		{
			return nullptr;
		}

		TArray<UGameplayActionComponent*> Components;
		Actor->GetComponents(Components);
		if (Components.Num() == 1)
		{
			return Components[0];
		}
		if (Components.Num() > 1)
		{
			OutDiagnostic = FString::Printf(
				TEXT("Actor '%s' owns %d GameplayActionComponents; component resolution is ambiguous."),
				*GetNameSafe(Actor),
				Components.Num());
		}
		return nullptr;
	}
}

bool FGameplayActionExecutionSpec::SynchronizeParameters()
{
	if (!IsValid(Definition) || !Definition->GetDefaultParameters().IsValid())
	{
		Parameters.Reset();
		return false;
	}

	const FInstancedPropertyBag Previous = Parameters;
	Parameters = Definition->GetDefaultParameters();
	if (Previous.IsValid())
	{
		return CopyBagValues(Previous, Parameters);
	}
	return true;
}

bool FGameplayActionExecutionSpec::IsSchemaSynchronized() const
{
	return IsValid(Definition)
		&& Parameters.IsValid()
		&& Definition->GetDefaultParameters().IsValid()
		&& Parameters.GetPropertyBagStruct() == Definition->GetDefaultParameters().GetPropertyBagStruct();
}

UGameplayActionComponent* GameplayActionsAI::ResolveActionComponent(
	AActor* ExplicitActor,
	AAIController* AIController,
	FString& OutDiagnostic)
{
	OutDiagnostic.Reset();
	if (ExplicitActor)
	{
		UGameplayActionComponent* Component = ResolveUniqueComponent(ExplicitActor, OutDiagnostic);
		if (!Component && OutDiagnostic.IsEmpty())
		{
			OutDiagnostic = FString::Printf(
				TEXT("Explicit Actor '%s' has no GameplayActionComponent."),
				*GetNameSafe(ExplicitActor));
		}
		return Component;
	}

	if (!IsValid(AIController))
	{
		OutDiagnostic = TEXT("No explicit Actor or valid AIController was provided.");
		return nullptr;
	}

	if (APawn* Pawn = AIController->GetPawn())
	{
		if (UGameplayActionComponent* Component = ResolveUniqueComponent(Pawn, OutDiagnostic))
		{
			return Component;
		}
		if (!OutDiagnostic.IsEmpty())
		{
			return nullptr;
		}
	}

	if (UGameplayActionComponent* Component = ResolveUniqueComponent(AIController, OutDiagnostic))
	{
		return Component;
	}
	if (OutDiagnostic.IsEmpty())
	{
		OutDiagnostic = FString::Printf(
			TEXT("Neither controlled Pawn nor AIController '%s' has a GameplayActionComponent."),
			*GetNameSafe(AIController));
	}
	return nullptr;
}

FGameplayActionRequestBuildResult GameplayActionsAI::BuildRequest(
	const FGameplayActionExecutionSpec& Spec,
	const UBlackboardComponent* Blackboard,
	const TArray<FGameplayActionBlackboardParameterBinding>& BlackboardBindings)
{
	FGameplayActionRequestBuildResult Result;
	if (!IsValid(Spec.Definition))
	{
		Result.DiagnosticMessage = TEXT("Execution spec has no valid Definition.");
		return Result;
	}
	if (!Spec.IsSchemaSynchronized())
	{
		Result.DiagnosticMessage =
			TEXT("Execution spec parameter schema is stale. Resynchronize it with the selected Definition.");
		return Result;
	}
	if (!Blackboard && !BlackboardBindings.IsEmpty())
	{
		Result.DiagnosticMessage = TEXT("Blackboard parameter bindings were supplied without a BlackboardComponent.");
		return Result;
	}

	FInstancedPropertyBag FinalParameters = Spec.Parameters;
	for (const FGameplayActionBlackboardParameterBinding& Binding : BlackboardBindings)
	{
		if (!ApplyBlackboardValue(*Blackboard, Binding, FinalParameters, Result.DiagnosticMessage))
		{
			return Result;
		}
	}

	FGameplayActionRequestCreationResult Creation =
		UGameplayActionBlueprintLibrary::CreateActionRequest(Spec.Definition);
	if (!Creation.WasCreated())
	{
		Result.DiagnosticMessage = Creation.DiagnosticMessage;
		return Result;
	}

	Result.Request = MoveTemp(Creation.Request);
	if (!CopyBagToRequest(FinalParameters, Result.Request, Result.DiagnosticMessage))
	{
		Result.Request = FGameplayActionRequest();
		return Result;
	}

	if (Spec.bOverridePriority)
	{
		UGameplayActionBlueprintLibrary::SetRequestPriority(Result.Request, Spec.Priority);
	}
	if (Spec.bOverrideBlockedPolicy)
	{
		UGameplayActionBlueprintLibrary::SetRequestBlockedPolicy(Result.Request, Spec.BlockedPolicy);
	}
	UGameplayActionBlueprintLibrary::SetRequestContext(
		Result.Request,
		Spec.OriginTag,
		Spec.Requester,
		Spec.Correlation);

	Result.bSucceeded = true;
	return Result;
}

bool GameplayActionsAI::WriteBlackboardStruct(
	UBlackboardComponent* Blackboard,
	const FBlackboardKeySelector& Key,
	const FConstStructView Value,
	FString& OutDiagnostic)
{
	if (Key.SelectedKeyName.IsNone())
	{
		return true;
	}
	if (!Blackboard)
	{
		OutDiagnostic = FString::Printf(
			TEXT("Blackboard output key '%s' was configured, but no BlackboardComponent is available."),
			*Key.SelectedKeyName.ToString());
		return false;
	}

	FBlackboard::FKey KeyId = FBlackboard::InvalidKey;
	const FBlackboardEntry* Entry = ResolveBlackboardEntry(*Blackboard, Key, KeyId, OutDiagnostic);
	UBlackboardKeyType_Struct* StructKey = Entry ? Cast<UBlackboardKeyType_Struct>(Entry->KeyType) : nullptr;
	if (!StructKey || !Value.IsValid()
		|| StructKey->DefaultValue.GetScriptStruct() != Value.GetScriptStruct())
	{
		OutDiagnostic = FString::Printf(
			TEXT("Blackboard output key '%s' must be a Struct key of type '%s'."),
			*Key.SelectedKeyName.ToString(),
			*GetNameSafe(Value.GetScriptStruct()));
		return false;
	}
	if (!Blackboard->SetValue<UBlackboardKeyType_Struct>(KeyId, Value))
	{
		OutDiagnostic = FString::Printf(
			TEXT("Could not write Blackboard output key '%s'."),
			*Key.SelectedKeyName.ToString());
		return false;
	}
	return true;
}
