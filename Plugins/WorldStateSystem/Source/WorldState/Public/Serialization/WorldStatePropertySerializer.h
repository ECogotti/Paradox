#pragma once

#include "CoreMinimal.h"
#include "Types/WorldStateTypes.h"

class FProperty;

/** One soft path found while recursively inspecting a restored property value. */
struct WORLDSTATE_API FWorldStateDiscoveredSoftReference
{
	/** Path from the selected root to the nested value, including container entries. */
	FString NestedValuePath;
	/** Serialized object/class path; collection does not load or resolve it. */
	FSoftObjectPath Path;
};

/** Shared runtime/editor reflection validation and one-root-property serialization service. */
class WORLDSTATE_API FWorldStatePropertySerializer
{
public:
	/** Recursively validates a root property and returns its canonical type signature on success. */
	static FWorldStatePropertyValidationResult Validate(const FProperty* Property);
	/** Builds the UE 5.8 FPropertyTypeName signature used as the persistent compatibility identity. */
	static FString BuildTypeSignature(const FProperty* Property);
	/**
	 * Serializes every ArrayDim element into an independently owned byte payload.
	 * @return False on invalid input or any archive error; OutPayload is empty on failure.
	 */
	static bool Serialize(const FProperty* Property, const void* Container, TArray<uint8>& OutPayload, FString& OutError);
	/**
	 * Deserializes through temporary initialized storage and copies into Container only after full success.
	 * @return False for invalid input, archive errors or trailing bytes; Container remains unchanged.
	 */
	static bool Deserialize(const FProperty* Property, void* Container, TConstArrayView<uint8> Payload, FString& OutError);
	/** Recursively enumerates soft paths already stored in a reflected value without resolving or loading them. */
	static void CollectSoftReferences(const FProperty* Property, const void* Container, TArray<FWorldStateDiscoveredSoftReference>& OutReferences);
};
