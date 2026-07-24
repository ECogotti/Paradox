# WorldStateEditor guide

`WorldStateEditor` registers a Details customization for `UWorldStateParticipantComponent`. It is an editor-only module and is not linked by runtime targets.

## Capture-property picker

The **Captured Properties** section groups candidates by:

- Owner Actor;
- authored native Actor Components;
- Blueprint Simple Construction Script Components.

Each candidate shows its reflected C++ type. Only recursively supported properties can be added, and a source/property pair cannot be added twice. Expected source class and canonical property signature are stored with the selection.

Selections are not silently deleted when a source or property later disappears. They remain visible as **Missing or incompatible**, so a refactor cannot turn data loss into an invisible default.

Runtime-created `Instance` Components are excluded because a name alone is not a reconstruction contract. Multi-object selection disables capture-source editing when sources cannot be mapped unambiguously.

## Structural state

The customization lists authored Scene Components for relative-transform capture. When Actor transform capture is authoritative, the root Scene Component option is disabled. Non-root selections restore a complete relative transform.

If the current parent differs from the captured parent and attachment capture is disabled, restore emits a warning; a selection configured for strict parent validation fails instead. Attachment capture restores the relationship before the relative transform.

## Participant IDs

The Identity section displays the per-instance participant ID and highlights duplicates in the current world. **Regenerate** assigns a new ID through an editor transaction. Templates remain ID-less; level instances and normal duplicates receive their own identity, while PIE instances retain the editor identity.

## Undo and Redo

Adding or removing a property, changing a Scene Component selection, and regenerating an ID use `FScopedTransaction`, `Modify()` and a Details refresh. They participate in the standard editor Undo/Redo history.

## Blueprint fallback

For a Blueprint component template without a direct Actor owner, the picker resolves the owning Blueprint generated class, its CDO and its Simple Construction Script nodes. This keeps inherited/native and SCS-authored sources visible without creating an asset or relying on component array indices.
