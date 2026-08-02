#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/IntentReplayTypes.h"
#include "IntentReplayTrack.generated.h"

/**
 * Finalized, immutable sequence of semantic action snapshots.
 *
 * Tracks live in the transient package rather than under the source Actor. A coordinator that
 * needs a track across world reset must still retain it through a reflected UPROPERTY so GC can
 * see it. Multiple playback sessions may read the same track concurrently.
 */
UCLASS(BlueprintType, Transient)
class INTENTREPLAY_API UIntentReplayTrack : public UObject
{
	GENERATED_BODY()

public:
	/** Stable identity created with the recording session and preserved at finalization. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Track")
	FIntentReplayTrackId GetTrackId() const { return TrackId; }

	/** Recording attempt that produced this track; valid for format 2 and later. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Track")
	FIntentRecordingSessionId GetSourceRecordingSessionId() const { return SourceRecordingSessionId; }

	/** Serialized format contract used by ValidateTrack; version 2 is current, version 1 is legacy. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Track")
	int32 GetFormatVersion() const { return FormatVersion; }

	/** Number of Accepted requests committed before the recording stopped. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Track")
	int32 GetEntryCount() const { return Entries.Num(); }

	/** Copies one read-only entry snapshot out by contiguous track index. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Track")
	bool GetEntryByIndex(int32 Index, FRecordedIntent& OutEntry) const;

	/** Copies the entry with the requested stable Recorded Intent ID, if present. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Track")
	bool FindEntryById(FRecordedIntentId RecordedIntentId, FRecordedIntent& OutEntry) const;

	/** Recording-clock duration captured at stop, excluding time spent paused. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Track")
	double GetRecordedDurationSeconds() const { return RecordedDurationSeconds; }

	/** Optional source/iteration label supplied in FIntentRecordingOptions. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Track")
	FString GetSourceLabel() const { return SourceLabel; }

	/** User metadata copied from FIntentRecordingOptions. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Track")
	FGameplayTagContainer GetMetadataTags() const { return MetadataTags; }

	/** True only after the mutable session has transferred its storage into this object. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Track")
	bool IsFinalized() const { return bFinalized; }

	/** Rechecks version, identity, entry shape, uniqueness, timestamps, and deterministic order. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay|Track")
	FIntentReplayTrackValidationResult ValidateTrack() const;

	/** Read-only C++ view; no mutable entry array is exposed to Blueprint or external modules. */
	const TArray<FRecordedIntent>& GetEntries() const { return Entries; }

private:
	/** One-way publication step called only by UIntentReplayComponent. */
	void InitializeFinalized(
		FIntentReplayTrackId InTrackId,
		FIntentRecordingSessionId InSourceRecordingSessionId,
		TArray<FRecordedIntent>&& InEntries,
		double InRecordedDurationSeconds,
		FString InSourceLabel,
		FGameplayTagContainer InMetadataTags);

	/** Stable track identity. */
	UPROPERTY()
	FIntentReplayTrackId TrackId;

	UPROPERTY()
	FIntentRecordingSessionId SourceRecordingSessionId;

	/** Format used to reject unsupported future/legacy layouts explicitly. */
	UPROPERTY()
	int32 FormatVersion = 2;

	/** Immutable after InitializeFinalized returns. */
	UPROPERTY()
	TArray<FRecordedIntent> Entries;

	/** Recording-clock duration, not wall-clock lifetime of the source Actor. */
	UPROPERTY()
	double RecordedDurationSeconds = 0.0;

	/** Diagnostic source label. */
	UPROPERTY()
	FString SourceLabel;

	/** User metadata retained with the track. */
	UPROPERTY()
	FGameplayTagContainer MetadataTags;

	/** Publication guard validated before playback preparation. */
	UPROPERTY()
	bool bFinalized = false;

	friend class UIntentReplayComponent;
	friend struct FIntentReplayCoreTestAccessor;
};
