#pragma once

#include "IDetailCustomization.h"
#include "Details/WorldStatePropertyPickerModel.h"
#include "Types/WorldStateTypes.h"

class IPropertyUtilities;
class UActorComponent;
class UWorldStateParticipantComponent;

/** Transaction-aware details UI for property, Scene Component and participant-identity authoring. */
class FWorldStateParticipantComponentCustomization final : public IDetailCustomization
{
public:
	/** PropertyEditor factory used by the editor module registration. */
	static TSharedRef<IDetailCustomization> MakeInstance();
	/** Replaces raw identity/selection arrays with validated, source-aware controls. */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	/** Returns the currently resolvable unique participant's picker sources. */
	TArray<FWorldStatePropertyPickerSource> BuildCaptureSources() const;
	/** Builds source-grouped menu entries for valid, unselected root properties. */
	TSharedRef<SWidget> BuildPropertyMenu();
	/** Formats one persisted selection while keeping missing/incompatible entries visible. */
	FText DescribePropertySelection(int32 SelectionIndex) const;
	/** Highlights selection validation failures without discarding authored data. */
	FSlateColor GetPropertySelectionColor(int32 SelectionIndex) const;
	/** Transactionally removes one persisted property selection. */
	FReply RemovePropertySelection(int32 SelectionIndex);
	/** Transactionally appends a selection with its source class and canonical type signature. */
	void AddPropertySelection(FWorldStateCaptureSourceId SourceId, FName PropertyName, FSoftClassPath SourceClass, FString TypeSignature);
	/** Returns whether relative-transform capture is enabled for SourceId. */
	ECheckBoxState IsSceneComponentSelected(FWorldStateCaptureSourceId SourceId) const;
	/** Transactionally adds, enables or removes a Scene Component selection. */
	void SetSceneComponentSelected(ECheckBoxState NewState, FWorldStateCaptureSourceId SourceId);
	/** Transactionally replaces the current instance identity. */
	FReply RegenerateId();
	/** Scans the current world for another participant with the same valid identity. */
	bool HasDuplicateId() const;
	/** Requests a details rebuild after an authored mutation. */
	void Refresh() const;

	/** Weak because details panels do not own the customized UObject. */
	TWeakObjectPtr<UWorldStateParticipantComponent> Participant;
	/** PropertyEditor service retained only for explicit refresh requests. */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
