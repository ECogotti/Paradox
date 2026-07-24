#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/IntentReplayTypes.h"
#include "IntentReplayBlueprintLibrary.generated.h"

/** Blueprint-only read helpers that preserve the immutability of tracks and Recorded Intents. */
UCLASS()
class INTENTREPLAY_API UIntentReplayBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True when the wrapped Track GUID was issued by a recording session. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay")
	static bool IsTrackIdValid(FIntentReplayTrackId Id) { return Id.IsValid(); }

	/** True when the wrapped Recorded Intent GUID identifies a committed entry. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay")
	static bool IsRecordedIntentIdValid(FRecordedIntentId Id) { return Id.IsValid(); }

	/** True when the wrapped GUID identifies a recipient-local playback session. */
	UFUNCTION(BlueprintPure, Category = "Intent Replay")
	static bool IsPlaybackSessionIdValid(FIntentReplayPlaybackSessionId Id) { return Id.IsValid(); }

	/**
	 * Reads a named Property Bag value using the type connected to Value as the expected type.
	 * This custom thunk never exposes mutable bag storage and reports missing/type mismatch explicitly.
	 */
	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Intent Replay|Parameters", meta = (CustomStructureParam = "Value", DisplayName = "Get Recorded Intent Parameter"))
	static void GetRecordedIntentParameter(
		const FRecordedIntent& RecordedIntent,
		FName ParameterName,
		UPARAM(ref) int32& Value,
		EGameplayActionParameterAccessResult& AccessResult);
	DECLARE_FUNCTION(execGetRecordedIntentParameter);
};
