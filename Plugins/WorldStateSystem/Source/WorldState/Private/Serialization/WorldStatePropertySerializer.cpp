#include "Serialization/WorldStatePropertySerializer.h"

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Serialization/StructuredArchiveAdapters.h"
#include "UObject/FieldPathProperty.h"
#include "UObject/Interface.h"
#include "UObject/PropertyOptional.h"
#include "UObject/PropertyTypeName.h"
#include "UObject/UnrealType.h"

namespace UE::WorldState::Private
{
	/** Creates the first-failure result returned by recursive validation. */
	FWorldStatePropertyValidationResult MakeInvalid(
		EWorldStatePropertyValidationStatus Status,
		FString Path,
		FString Message)
	{
		FWorldStatePropertyValidationResult Result;
		Result.Status = Status;
		Result.NestedFailurePath = MoveTemp(Path);
		Result.Message = MoveTemp(Message);
		return Result;
	}

	/** Validates the complete reflected value graph and retains the precise nested failure path. */
	FWorldStatePropertyValidationResult ValidateRecursive(const FProperty* Property, const FString& Path)
	{
		if (!Property)
		{
			return MakeInvalid(EWorldStatePropertyValidationStatus::MissingProperty, Path, TEXT("The reflected property no longer exists."));
		}

		if (Property->HasAnyPropertyFlags(CPF_EditorOnly | CPF_Deprecated))
		{
			return MakeInvalid(
				EWorldStatePropertyValidationStatus::EditorOnlyPropertyRejected,
				Path,
				TEXT("Editor-only and deprecated properties cannot be captured in runtime snapshots."));
		}

		// Soft properties derive from object-property machinery, so accept them before rejecting hard references.
		if (CastField<FSoftObjectProperty>(Property))
		{
			return FWorldStatePropertyValidationResult();
		}

		if (CastField<FWeakObjectProperty>(Property) || CastField<FLazyObjectProperty>(Property))
		{
			return MakeInvalid(
				EWorldStatePropertyValidationStatus::WeakObjectReferenceRejected,
				Path,
				TEXT("Weak and lazy UObject references are rejected by the default World State policy."));
		}

		if (CastField<FObjectPropertyBase>(Property) || CastField<FInterfaceProperty>(Property))
		{
			return MakeInvalid(
				EWorldStatePropertyValidationStatus::HardObjectReferenceRejected,
				Path,
				TEXT("Hard UObject references are rejected; use a soft object or soft class reference."));
		}

		if (CastField<FDelegateProperty>(Property) || CastField<FMulticastDelegateProperty>(Property) || CastField<FFieldPathProperty>(Property))
		{
			return MakeInvalid(
				EWorldStatePropertyValidationStatus::UnsupportedPropertyType,
				Path,
				TEXT("Delegates and reflected field references are not snapshot values."));
		}

		// Container support is determined recursively; a supported outer container cannot hide an unsafe element.
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			return ValidateRecursive(ArrayProperty->Inner, Path + TEXT("[]"));
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			return ValidateRecursive(SetProperty->ElementProp, Path + TEXT("{}"));
		}

		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			FWorldStatePropertyValidationResult KeyResult = ValidateRecursive(MapProperty->KeyProp, Path + TEXT(".Key"));
			return KeyResult.IsValid() ? ValidateRecursive(MapProperty->ValueProp, Path + TEXT(".Value")) : KeyResult;
		}

		if (const FOptionalProperty* OptionalProperty = CastField<FOptionalProperty>(Property))
		{
			return ValidateRecursive(OptionalProperty->GetValueProperty(), Path + TEXT(".Value"));
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			// Soft-path structs are terminal values; native and User Defined Structs are traversed identically.
			if (StructProperty->Struct == FSoftObjectPath::StaticStruct() || StructProperty->Struct == FSoftClassPath::StaticStruct())
			{
				return FWorldStatePropertyValidationResult();
			}

			for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
			{
				const FProperty* Member = *It;
				FWorldStatePropertyValidationResult MemberResult = ValidateRecursive(Member, Path + TEXT(".") + Member->GetName());
				if (!MemberResult.IsValid())
				{
					if (MemberResult.Status == EWorldStatePropertyValidationStatus::UnsupportedPropertyType)
					{
						MemberResult.Status = EWorldStatePropertyValidationStatus::UnsupportedNestedType;
					}
					return MemberResult;
				}
			}
		}

		return FWorldStatePropertyValidationResult();
	}

	/** Recursively reads soft paths from a validated live value without resolving or loading objects. */
	void CollectDirect(
		const FProperty* Property,
		const void* Value,
		const FString& Path,
		TArray<FWorldStateDiscoveredSoftReference>& OutReferences)
	{
		if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr SoftValue = SoftProperty->GetPropertyValue(Value);
			FWorldStateDiscoveredSoftReference& Found = OutReferences.AddDefaulted_GetRef();
			Found.NestedValuePath = Path;
			Found.Path = SoftValue.ToSoftObjectPath();
			return;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == FSoftObjectPath::StaticStruct())
			{
				FWorldStateDiscoveredSoftReference& Found = OutReferences.AddDefaulted_GetRef();
				Found.NestedValuePath = Path;
				Found.Path = *static_cast<const FSoftObjectPath*>(Value);
				return;
			}
			if (StructProperty->Struct == FSoftClassPath::StaticStruct())
			{
				FWorldStateDiscoveredSoftReference& Found = OutReferences.AddDefaulted_GetRef();
				Found.NestedValuePath = Path;
				Found.Path = *static_cast<const FSoftClassPath*>(Value);
				return;
			}

			for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
			{
				const FProperty* Member = *It;
				for (int32 Index = 0; Index < Member->ArrayDim; ++Index)
				{
					CollectDirect(
						Member,
						Member->ContainerPtrToValuePtr<void>(Value, Index),
						Path + TEXT(".") + Member->GetName() + (Member->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), Index) : FString()),
						OutReferences);
				}
			}
			return;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, Value);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				CollectDirect(ArrayProperty->Inner, Helper.GetRawPtr(Index), FString::Printf(TEXT("%s[%d]"), *Path, Index), OutReferences);
			}
			return;
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			// Script sets/maps are sparse; diagnostic indices count only valid entries for stable readable paths.
			FScriptSetHelper Helper(SetProperty, Value);
			int32 LogicalIndex = 0;
			for (int32 InternalIndex = 0; InternalIndex < Helper.GetMaxIndex(); ++InternalIndex)
			{
				if (Helper.IsValidIndex(InternalIndex))
				{
					CollectDirect(SetProperty->ElementProp, Helper.GetElementPtr(InternalIndex), FString::Printf(TEXT("%s{%d}"), *Path, LogicalIndex++), OutReferences);
				}
			}
			return;
		}

		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(MapProperty, Value);
			int32 LogicalIndex = 0;
			for (int32 InternalIndex = 0; InternalIndex < Helper.GetMaxIndex(); ++InternalIndex)
			{
				if (Helper.IsValidIndex(InternalIndex))
				{
					const FString PairPath = FString::Printf(TEXT("%s{%d}"), *Path, LogicalIndex++);
					CollectDirect(MapProperty->KeyProp, Helper.GetKeyPtr(InternalIndex), PairPath + TEXT(".Key"), OutReferences);
					CollectDirect(MapProperty->ValueProp, Helper.GetValuePtr(InternalIndex), PairPath + TEXT(".Value"), OutReferences);
				}
			}
		}
	}
}

FWorldStatePropertyValidationResult FWorldStatePropertySerializer::Validate(const FProperty* Property)
{
	FWorldStatePropertyValidationResult Result = UE::WorldState::Private::ValidateRecursive(Property, Property ? Property->GetName() : FString());
	if (Result.IsValid())
	{
		Result.TypeSignature = BuildTypeSignature(Property);
	}
	return Result;
}

FString FWorldStatePropertySerializer::BuildTypeSignature(const FProperty* Property)
{
	if (!Property)
	{
		return FString();
	}

	// FPropertyTypeName is the engine's canonical UE 5.8 identity and includes nested/container type arguments.
	const UE::FPropertyTypeName TypeName(Property);
	TStringBuilder<256> Builder;
	Builder << TypeName;
	return FString::Printf(TEXT("%s[ArrayDim=%d]"), *FString(Builder.ToString()), Property->ArrayDim);
}

bool FWorldStatePropertySerializer::Serialize(const FProperty* Property, const void* Container, TArray<uint8>& OutPayload, FString& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WorldState_SerializeProperty);
	OutPayload.Reset();
	if (!Property || !Container)
	{
		OutError = TEXT("Property serialization received a null property or container.");
		return false;
	}

	// Names and object-like soft values are serialized as strings; proxy loading is explicitly disabled.
	FMemoryWriter Writer(OutPayload, true);
	FObjectAndNameAsStringProxyArchive Archive(Writer, false);
	Archive.ArIsSaveGame = false;
	Archive.ArNoDelta = true;
	// ArrayDim belongs to the root declaration and is distinct from a dynamic FArrayProperty value.
	int32 ArrayDim = Property->ArrayDim;
	Archive << ArrayDim;
	for (int32 Index = 0; Index < ArrayDim; ++Index)
	{
		FStructuredArchiveFromArchive StructuredArchive(Archive);
		Property->SerializeItem(StructuredArchive.GetSlot(), const_cast<void*>(Property->ContainerPtrToValuePtr<void>(Container, Index)), nullptr);
		if (Archive.IsError())
		{
			OutError = FString::Printf(TEXT("Archive failed while serializing %s[%d]."), *Property->GetName(), Index);
			OutPayload.Reset();
			return false;
		}
	}
	Archive.Close();
	return !Archive.IsError();
}

bool FWorldStatePropertySerializer::Deserialize(const FProperty* Property, void* Container, TConstArrayView<uint8> Payload, FString& OutError)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WorldState_DeserializeProperty);
	if (!Property || !Container || Payload.IsEmpty())
	{
		OutError = TEXT("Property deserialization received invalid input.");
		return false;
	}

	// Deserialize into initialized temporary storage so archive failure never writes defaults or partial values.
	void* TemporaryValue = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
	Property->InitializeValue(TemporaryValue);
	bool bSucceeded = false;
	{
		FMemoryReaderView Reader(Payload, true);
		FObjectAndNameAsStringProxyArchive Archive(Reader, false);
		Archive.ArIsSaveGame = false;
		Archive.ArNoDelta = true;
		int32 CapturedArrayDim = 0;
		Archive << CapturedArrayDim;
		if (CapturedArrayDim != Property->ArrayDim)
		{
			OutError = FString::Printf(TEXT("ArrayDim changed from %d to %d for %s."), CapturedArrayDim, Property->ArrayDim, *Property->GetName());
		}
		else
		{
			for (int32 Index = 0; Index < CapturedArrayDim && !Archive.IsError(); ++Index)
			{
				FStructuredArchiveFromArchive StructuredArchive(Archive);
				void* TemporaryElement = static_cast<uint8*>(TemporaryValue) + Index * Property->GetElementSize();
				Property->SerializeItem(StructuredArchive.GetSlot(), TemporaryElement, nullptr);
			}
			bSucceeded = !Archive.IsError() && Reader.AtEnd();
			if (!bSucceeded && OutError.IsEmpty())
			{
				OutError = FString::Printf(TEXT("Archive failed or contained trailing data while deserializing %s."), *Property->GetName());
			}
		}
		Archive.Close();
	}

	if (bSucceeded)
	{
		// Commit is intentionally a single copy after ArrayDim, archive and trailing-data validation.
		Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Container), TemporaryValue);
	}
	Property->DestroyValue(TemporaryValue);
	FMemory::Free(TemporaryValue);
	return bSucceeded;
}

void FWorldStatePropertySerializer::CollectSoftReferences(
	const FProperty* Property,
	const void* Container,
	TArray<FWorldStateDiscoveredSoftReference>& OutReferences)
{
	if (!Property || !Container)
	{
		return;
	}

	// Root static arrays are walked element-by-element to match the serializer's ArrayDim contract.
	for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
	{
		UE::WorldState::Private::CollectDirect(
			Property,
			Property->ContainerPtrToValuePtr<void>(Container, Index),
			Property->GetName() + (Property->ArrayDim > 1 ? FString::Printf(TEXT("[%d]"), Index) : FString()),
			OutReferences);
	}
}
