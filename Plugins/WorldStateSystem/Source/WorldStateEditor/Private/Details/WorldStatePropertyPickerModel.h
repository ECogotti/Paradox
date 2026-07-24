#pragma once

#include "Serialization/WorldStatePropertySerializer.h"
#include "Types/WorldStateTypes.h"

class UWorldStateParticipantComponent;

/** One Actor or authored Component source displayed by the property picker. */
struct FWorldStatePropertyPickerSource
{
	/** Stable identity persisted by selections. */
	FWorldStateCaptureSourceId Id;
	/** Editor object used only to inspect current reflection metadata. */
	TWeakObjectPtr<UObject> Object;
	/** Designer-facing source label. */
	FText Label;
};

/** Descriptive picker row for one reflected root property. */
struct FWorldStatePropertyPickerCandidate
{
	/** Source identity and transient editor object used to build the row. */
	FWorldStateCaptureSourceId SourceId;
	TWeakObjectPtr<UObject> Source;
	/** Reflected root name and concise display type. */
	FName PropertyName;
	FString DisplayType;
	/** Shared runtime/editor recursive validation result. */
	FWorldStatePropertyValidationResult Validation;
	/** Prevents duplicate authored selections for the same source and root name. */
	bool bAlreadySelected = false;
};

/** Pure picker model shared by the Details customization and editor automation tests. */
class FWorldStatePropertyPickerModel
{
public:
	/** Enumerates the owner Actor and reconstructible authored Components, including Blueprint templates. */
	static TArray<FWorldStatePropertyPickerSource> BuildSources(const UWorldStateParticipantComponent& Participant);
	/** Builds sorted reflected-property rows and optionally retains unsupported rows for diagnostics/tests. */
	static TArray<FWorldStatePropertyPickerCandidate> BuildCandidates(
		const UWorldStateParticipantComponent& Participant,
		const FWorldStatePropertyPickerSource& Source,
		bool bIncludeUnsupported);
	/** Revalidates an existing selection while preserving missing source/property identities visibly. */
	static FWorldStatePropertyPickerCandidate DescribeSelection(
		const UWorldStateParticipantComponent& Participant,
		const FWorldStatePropertySelection& Selection);
	/** Returns false for multi-object customization because different participants need not share source identity. */
	static bool CanEditUniqueSources(TConstArrayView<TWeakObjectPtr<UObject>> CustomizedObjects);
};
