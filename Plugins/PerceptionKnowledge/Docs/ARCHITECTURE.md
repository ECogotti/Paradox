# Architecture

## Ownership

The Source owns identity and exposed state. The Listener owns current knowledge and Recent Event Memory. The World Subsystem owns only world-scoped routing data:

```text
Source state changes
    -> World Subsystem current-perception relationship
    -> interested Listener
    -> value observation
    -> Listener-owned Knowledge Store
```

All registries use weak UObject references. Source and Listener registration, delegate binding, timers, perception relationships, component unregistration, EndPlay, and world teardown have symmetric cleanup.

## Identity

`FPerceptionKnowledgeEntityId` wraps a plugin-local `FGuid`. CDOs and templates remain invalid so a template ID cannot be copied to every instance. Map-authored and loaded instances retain their ID; runtime instances receive one during component creation/registration. Ordinary duplication regenerates the ID while PIE duplication preserves it. The world registry rejects a duplicate live ID and never replaces the first registered Source.

An external authority may reconstruct that same semantic Source through
`UPerceptionKnowledgeSourceComponent::AssignEntityId`. Reassignment is legal only on the Game
Thread while the runtime component is disabled, semantically unregistered, and natively
unregistered. The World Subsystem validates that no other live Source owns the requested ID.
Invalid input, collisions, missing world authority, and stale registration return a structured
failure without changing the Source's current ID. Re-enabling follows the normal atomic
registration path and may still fail observably if world state changed between validation and
registration.

Identity is intentionally local to this plugin. Perception Knowledge has no dependency on EntityRelations or another custom project plugin.

## Typed values and state

`FPerceptionKnowledgeValue` is a closed discriminated value with Bool, Integer, Float, Name, Gameplay Tag, Entity ID, and Vector alternatives. It has deterministic type-aware equality and contains no live UObject reference. Once a State Tag has a typed value, a mutation to another value type is rejected.

State is indexed by `FPerceptionKnowledgeStateKey`, which contains Entity ID and State Tag. Unknown and Invalidated are explicit statuses; they are not encoded as a Boolean or missing entry.

`RemoveObservableState` removes exposure and sends an invalidation to listeners currently interested in the Source. A future observer cannot acquire the removed entry. `InvalidateObservableState` retains the invalidated entry, so a future observer can acquire that status.

## Providers

The Source gathers its local state map first and then calls `IPerceptionKnowledgeStateProvider::GatherObservableStates` on the owning Actor and implementing Components. Invalid, wrong-sense, and duplicate provider entries are rejected with diagnostics. Providers must call `NotifyProviderStatesChanged` when computed state changes; no arbitrary UPROPERTY scanning occurs.

## Sight

Sight acquisition resolves the native stimulus Actor to a registered Source, records the current Sight relationship, gathers states, and stores observations. Sight loss clears only the current relationship and preserves knowledge.

State changes while Sight remains active take the primary event-driven route through the World Subsystem. No loss/reacquisition is needed. `VisibleStateValidationInterval` is a disabled-by-default fallback and, when positive, refreshes only Sources already visible to that Listener.

## Hearing

`EmitSemanticNoise` first creates a semantic event and stores it in a bounded, TTL-based world registry. It allocates a unique numeric `FName` instance using the fixed base name `PerceptionKnowledgeNoise`, places that token in `FAIStimulus::Tag`, and always calls `UAISense_Hearing::ReportNoiseEvent`.

Native Hearing therefore decides range and affiliation. A listener resolves the token back to semantic data, validates native instigator and location, then adds an event to Recent Event Memory. Correlation entries are not consumed by the first listener, so multiple listeners can resolve the same sound. Closely timed sounds use different numeric tokens. Missing, expired, or mismatched correlations log a warning and appear magenta in debug.

## Knowledge and revisions

For every accepted state observation:

1. metadata is refreshed;
2. the Listener-wide knowledge revision increments;
3. the fact revision increments only on first learning or semantic change;
4. `OnKnownStateChanged` fires only on first learning or semantic change;
5. `OnObservationProduced` follows the Profile anti-spam policy.

Events are stored separately and never affect state revisions. Recent Event Memory is capacity-bounded and batch-cleaned by timer.

## Player Controller lifetime

UE 5.8 `UAIPerceptionComponent::GetBodyActor` uses a Controller's Pawn, but listener direction still
comes from the owning Controller's gameplay-eyes contract. `APlayerController` normally bases that
rotation on `ControlRotation`; `bAttachToPawn` synchronizes location only and explicitly leaves
rotation on `ControlRotation`. A top-down project whose Pawn faces independently must therefore
override `GetActorEyesViewPoint` on its Player Controller, or otherwise keep its gameplay-eyes
rotation aligned with the Pawn. Perception Knowledge deliberately uses that native contract for
both actual Sight and debug, so the debug cone reports the same direction that AI Perception uses.
`OnPossessedPawnChanged` clears native/current perception, requests a listener update for the new
Pawn, and preserves the Knowledge Store. With no Pawn, observations are suspended and the missing
body/viewpoint remains visible through result, log, and debug state.

## Runtime Profile propagation

The Profile is the single shared source of listener tuning, but native AI Perception owns runtime
copies inside each Listener's Sight and Hearing config objects. The Profile therefore publishes an
event whenever validated runtime setters, editor property changes, editor undo, or explicit C++
bulk notification changes it.

Each bound Listener synchronously:

1. validates and copies the Profile into its native sense configs;
2. applies runtime enablement through `SetSenseEnabled`;
3. requests `UAIPerceptionSystem` listener refresh;
4. updates timers and registration;
5. broadcasts `OnListenerConfigurationChangedNative`.

The final broadcast is deliberately last. Renderer and other presentation consumers consequently
observe the values already applied to native perception. Invalid Profile changes disable native
senses and semantic observation until a valid change is published; they never leave an old range
active behind a newer invalid asset value. Listener binding and unbinding follow component
registration, unregistration, Profile replacement, and EndPlay symmetrically.

## Extension boundaries

IntentReplay can subscribe to native or Blueprint observation delegates and attach its own recording correlation outside the core event. GOAP can request filtered value snapshots. Neither extension should mutate the Listener's internal maps. Comparative timeline colors and paradox classification belong to IntentReplay, not this plugin.
