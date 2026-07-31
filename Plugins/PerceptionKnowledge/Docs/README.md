# Perception Knowledge

## Hearing Range renderer and read-only configuration

`UPerceptionKnowledgeHearingRangeRendererComponent` is an optional event-driven, non-colliding
mesh renderer anchored to the listener's current Body Actor. Consumers can read
`GetListenerProfile`, `GetEffectiveHearingRange`, and `GetResolvedBodyActor`, or subscribe in C++ to
`OnListenerConfigurationChangedNative`. Gameplay visibility is independent from debug visibility;
debug requires the local flag and `PerceptionKnowledge.Debug`. The mesh never affects collision,
overlaps, shadows, or navigation.

When the renderer is owned by a Controller, its visible primitive is created transiently on the
possessed Pawn. Unreal keeps Controller Actors hidden, so attaching a Controller-owned primitive to
the Pawn would still prevent it from rendering. `GetActiveRenderComponent`,
`IsHearingRangeVisible`, and `GetRendererDiagnostic` expose the resolved runtime state without
requiring Tick.

`UPerceptionKnowledgeProfile` changes propagate to every bound runtime Listener. Editing the Data
Asset while PIE is active publishes automatically. Blueprint runtime code uses `SetSightRanges` or
`SetHearingRange`; C++ code that directly mutates several public fields finishes with
`NotifyRuntimeConfigurationChanged`. Each Listener reapplies the native AI Perception configs,
updates enabled senses, requests a listener refresh, and only then notifies presentation
consumers. The Hearing renderer therefore follows the range actually applied to native Hearing.

Perception Knowledge is a standalone Unreal Engine 5.8 runtime plugin that translates native AI Perception Sight and Hearing stimuli into semantic observations. Each listener owns its current knowledge; the plugin does not record a timeline and does not contain Paradox, GOAP, IntentReplay, Smart Object, replication, or save-game rules.

## Core model

A Source exposes two deliberately separate forms of information:

- **State** is the latest observable fact for `(EntityId, StateTag)`. A state has a typed value and an explicit `Known`, `Unknown`, or `Invalidated` status. `Known + Bool(false)` is not `Unknown`.
- **Event** is a transient occurrence with its own Observation ID, source, instigator, location, time, sense, strength, loudness, confidence, and optional cause. Events enter bounded Recent Event Memory and never overwrite state.

`UPerceptionKnowledgeListenerComponent` stores one authoritative value per state key. Every accepted state observation refreshes metadata and the listener-wide knowledge revision. A fact revision and `OnKnownStateChanged` advance only when the semantic value or status changes.

## Controlled runtime identity assignment

Sources normally create and preserve their own `FPerceptionKnowledgeEntityId`. An authoritative
runtime owner that reconstructs the same semantic entity can call `AssignEntityId`, but only while
the Source is disabled and fully absent from both native and semantic registries. The API rejects
invalid IDs, templates, wrong-thread calls, stale registration, and collisions with another live
Source. Every rejection preserves the previous ID.

Call `SetSourceEnabled(false)`, check the result and registration getters, assign the validated ID,
then call `SetSourceEnabled(true)` and verify registration. This is an explicit lifecycle operation,
not an alternative to the default automatic identity.

## Main runtime types

- `UPerceptionKnowledgeSourceComponent` derives from `UAIPerceptionStimuliSourceComponent`.
- `UPerceptionKnowledgeListenerComponent` derives from `UAIPerceptionComponent`.
- `UPerceptionKnowledgeWorldSubsystem` owns weak registries, current perception relationships, and short-lived Hearing correlations. It does not own listener knowledge.
- `UPerceptionKnowledgeProfile` is the required shared Sight, Hearing, memory, and anti-spam configuration.
- `IPerceptionKnowledgeStateProvider` lets an Actor or Component provide computed state without runtime property reflection.
- `FPerceptionKnowledgeSnapshot` is a disconnected value copy intended for future consumers.

## Typical use

1. Add a Source Component to an observable Actor.
2. Configure its initial state entries or update them through `SetObservableState`.
3. Add a Listener Component to a Player Controller, AI Controller, or other Actor with a valid viewpoint.
4. Assign a `UPerceptionKnowledgeProfile`.
5. Subscribe to `OnObservationProduced`, query known states, or call `BuildKnowledgeSnapshot`.
6. Use `EmitSemanticNoise` for Hearing. Direct Hearing requests through `EmitObservableEvent` are rejected.

See [SETUP.md](SETUP.md), [ARCHITECTURE.md](ARCHITECTURE.md), and [DEBUGGING.md](DEBUGGING.md) for the complete contracts.

## Milestone 1 boundaries

The plugin only answers what an observer currently knows and which recent semantic events it heard or saw. It does not compare observations with a recording, produce paradoxes, plan actions, persist knowledge, replicate knowledge, or build an historical track. Future IntentReplay can subscribe to observation delegates; future GOAP can consume immutable snapshots without changing this core model.
