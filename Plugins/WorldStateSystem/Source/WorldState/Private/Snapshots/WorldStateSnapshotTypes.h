#pragma once

#include "CoreMinimal.h"
#include "Spawning/WorldStateSpawnStrategy.h"
#include "Types/WorldStateTypes.h"

/** Value-owned serialized form of one selected reflected root property. */
struct FWorldStateCapturedProperty
{
	/** Durable owner identity independent of live UObject pointers. */
	FWorldStateCaptureSourceId CaptureSourceId;
	/** Root reflected property name. */
	FName PropertyName;
	/** Expected source class recorded for restore preflight. */
	FSoftClassPath SourceClass;
	/** Canonical reflected type identity recorded at capture. */
	FString TypeSignature;
	/** Explicit property payload format version for future migrations. */
	int32 SerializationFormatVersion = 1;
	/** Independent archive bytes; snapshots never retain an address into live world state. */
	TArray<uint8> Payload;
	/** Resolution policy applied to all soft paths recursively contained by Payload. */
	EWorldStateReferenceRequirement ReferenceRequirement = EWorldStateReferenceRequirement::Optional;
	/** Effective property restore phase after any selection override. */
	EWorldStateRestorePhase RestorePhase = EWorldStateRestorePhase::Default;
};

/** Value-owned structural state for one selected authored Scene Component. */
struct FWorldStateSceneComponentSnapshot
{
	/** Stable identity used to find the current authored Component by name and class. */
	FWorldStateCaptureSourceId CaptureSourceId;
	/** Complete relative transform restored after Actor structure. */
	FTransform RelativeTransform = FTransform::Identity;
	/** Distinguishes a deliberately unattached Component from a missing parent. */
	bool bHadParent = false;
	/** Selects stable local source identity rather than an external object path for the parent. */
	bool bParentOwnedByActor = false;
	/** Local parent Component identity when bParentOwnedByActor is true. */
	FWorldStateCaptureSourceId ParentSourceId;
	/** External parent path retained only for validation; restore never synchronously loads it. */
	FSoftObjectPath ExternalParentPath;
	/** Captured attachment socket. */
	FName SocketName;
	/** Converts a changed/missing parent from a warning into a restore failure. */
	bool bStrictParentValidation = false;
};

/** Complete private snapshot record for one participant, with no live UObject references. */
struct FWorldStateParticipantSnapshot
{
	/** Stable participant identity used as the owning snapshot-map key. */
	FWorldStateParticipantId ParticipantId;
	/** Structural capture switches copied from the participant at capture time. */
	bool bCaptureExistence = true;
	bool bCaptureActorTransform = true;
	bool bCaptureAttachment = false;
	/** Captured Actor transform when enabled. */
	FTransform ActorTransform = FTransform::Identity;
	/** Actor attachment identity, preferring participant identity over external object paths. */
	bool bHadActorAttachment = false;
	FWorldStateParticipantId AttachmentParentParticipantId;
	FSoftObjectPath AttachmentParentActorPath;
	FName AttachmentParentComponentName;
	FName AttachmentSocketName;
	/** Data supplied to the selected spawn strategy if the Actor is missing. */
	FWorldStateSpawnDescriptor SpawnDescriptor;
	/** Captured lifetime and deterministic ordering policy. */
	EWorldStateExistencePolicy ExistencePolicy = EWorldStateExistencePolicy::ExistingOnly;
	EWorldStateRestorePhase RestorePhase = EWorldStateRestorePhase::Default;
	TArray<FWorldStateParticipantId> RestoreAfter;
	TArray<FWorldStateParticipantId> RestoreBefore;
	/** Captured scope membership and value-owned scene/property records. */
	TArray<FName> Groups;
	TArray<FWorldStateSceneComponentSnapshot> SceneComponents;
	TArray<FWorldStateCapturedProperty> Properties;
};

/** Immutable in-memory snapshot published atomically by the owning world subsystem. */
struct FWorldStateSnapshot
{
	/** Public opaque handle; snapshot contents remain private. */
	FWorldStateSnapshotId SnapshotId;
	/** Explicit complete-snapshot format version. */
	int32 FormatVersion = 1;
	/** Monotonic per-world capture sequence used for diagnostics. */
	uint64 CaptureSequence = 0;
	/** Captured world identity and caller label. */
	FName WorldPackageName;
	FName Label;
	/** Value-owned participant records keyed by stable identity. */
	TMap<FWorldStateParticipantId, FWorldStateParticipantSnapshot> Participants;

	/** Returns the total owned property archive size for diagnostics and summaries. */
	int64 GetPayloadBytes() const
	{
		int64 Total = 0;
		for (const TPair<FWorldStateParticipantId, FWorldStateParticipantSnapshot>& Pair : Participants)
		{
			for (const FWorldStateCapturedProperty& Property : Pair.Value.Properties)
			{
				Total += Property.Payload.Num();
			}
		}
		return Total;
	}
};
