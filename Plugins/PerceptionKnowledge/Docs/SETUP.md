# Setup and usage

## Optional Hearing Range renderer

Add `UPerceptionKnowledgeHearingRangeRendererComponent` beside the listener. It resolves a
same-owner listener automatically, or accepts explicit `SetListener` wiring. Assign its mesh and
material, set `AuthoredMeshRadius`, use `VerticalScaleMultiplier` for a flat disc, and configure
gameplay visibility separately from local debug visibility. It reanchors on possession and
rescales on profile changes without ticking.

Controller ownership needs no Blueprint workaround: because Unreal Controller Actors are hidden,
the component treats its inherited mesh/material properties as authoring data and creates the
visible, transient primitive on the possessed Pawn. Do not add a duplicate sphere to the Pawn.
Use `GetRendererDiagnostic` if it remains hidden. A normal gameplay display requires
`bVisibleInGameplay`; diagnostic display instead requires both `bEnableDebug` and
`PerceptionKnowledge.Debug 1`. The component remains disabled by default.

## Enable the plugin

The project descriptor enables `PerceptionKnowledge`. For another UE 5.8 project, copy the plugin directory, enable it in the Plugins window or project descriptor, and restart the editor.

The runtime module depends only on Engine modules: Core, CoreUObject, Engine, AIModule, GameplayTags, and DeveloperSettings.

## Create a Profile

Create a Data Asset of class `PerceptionKnowledgeProfile`. Configure:

- Sight enablement, Sight Radius, Lose Sight Radius, peripheral half angle, Max Age, and affiliation.
- Hearing enablement, Hearing Range, Max Age, and affiliation.
- Recent Event lifetime and maximum buffer entries.
- repeated-observation policy: Always, Acquisitions and Changes, or Changes Only.
- optional visible-state validation interval. Zero is the recommended default; a positive value must be at least 0.1 seconds.

A Listener without a valid Profile registers in suspended mode and produces no observations.

### Player Controller viewpoint

The Listener uses UE's native gameplay-eyes contract. A Controller-owned Listener resolves the
possessed Pawn as its Body Actor, but `APlayerController` direction normally remains
`ControlRotation`. The inherited Controller option `Attach to Pawn` follows the Pawn's location,
not its rotation. In a top-down game where the physical Pawn turns independently of the camera,
override `APlayerController::GetActorEyesViewPoint` so its location comes from the Pawn's eye
location and its rotation comes from the Pawn's physical facing. Listener debug uses this same
contract, so a correctly rotating debug cone also verifies the direction used by native Sight.

### Change a Profile at runtime

A Profile is shared configuration, so one published mutation updates every Listener currently
using that asset:

- editing a Profile Data Asset while PIE is running publishes automatically;
- Blueprint uses `SetSightRanges` and `SetHearingRange`;
- C++ may use the same setters, or mutate several fields and call
  `NotifyRuntimeConfigurationChanged` once.

`SetSightRanges` changes Sight Radius and Lose Sight Radius atomically and rejects inconsistent
values. `NotifyRuntimeConfigurationChanged` validates the complete Profile and still publishes an
invalid result: active listeners then suspend both native senses instead of continuing with stale
settings. Correct the values and notify again to resume.

After a valid publication, each registered Listener copies the Profile into its native
`UAISenseConfig_Sight` and `UAISenseConfig_Hearing`, updates the runtime sense allow-list, and calls
`RequestStimuliListenerUpdate`. `GetEffectiveSightRadius`, `GetEffectiveLoseSightRadius`, and
`GetEffectiveHearingRange` report these applied native values. The Hearing renderer refreshes from
the same applied value without Tick.

```cpp
FString Error;
Profile->SetSightRanges(2200.0f, 2500.0f, Error);
Profile->SetHearingRange(3200.0f, Error);

// For one C++ bulk edit:
Profile->SightRadius = 2600.0f;
Profile->LoseSightRadius = 2900.0f;
Profile->HearingRange = 3600.0f;
Profile->NotifyRuntimeConfigurationChanged(Error);
```

## Configure an observable Actor

Add `PerceptionKnowledgeSourceComponent` to the Actor. Enable at least Sight or Hearing registration. The component creates and preserves its Entity ID automatically.

For an authoritative reconstruction system that must preserve semantic identity across Actor
instances, use the controlled disabled-state sequence:

```cpp
const FPerceptionKnowledgeOperationResult Disabled =
    Source->SetSourceEnabled(false);
if (Disabled.IsSuccess()
    && !Source->IsSemanticallyRegistered()
    && !Source->IsNativeStimuliSourceRegistered())
{
    const FPerceptionKnowledgeOperationResult Assigned =
        Source->AssignEntityId(AuthoritativeEntityId);
    if (Assigned.IsSuccess())
    {
        const FPerceptionKnowledgeOperationResult Enabled =
            Source->SetSourceEnabled(true);
    }
}
```

Check every result. `AssignEntityId` rejects an invalid ID, a live collision, or any enabled or
registered Source and leaves the old value untouched. Do not use `RegenerateEntityId` when the
identity comes from an immutable external timeline or save record.

Initial state entries need:

- a semantic State Tag;
- a typed `FPerceptionKnowledgeValue`;
- `Known`, `Unknown`, or `Invalidated`;
- one or more observable sense tags, normally `PerceptionKnowledge.Sense.Sight`.

Blueprint value factory nodes are available in `Perception Knowledge | Value`.

At runtime:

```cpp
Source->SetObservableState(
    PoweredTag,
    FPerceptionKnowledgeValue::MakeBool(true));
```

Use `SetObservableStateUnknown`, `InvalidateObservableState`, or `RemoveObservableState` for their distinct contracts. Check the returned `FPerceptionKnowledgeOperationResult`; incompatible types, invalid tags, missing registration, and unsupported senses are explicit failures.

For computed state, implement `IPerceptionKnowledgeStateProvider` on the Actor or a Component and call `NotifyProviderStatesChanged` when its output changes.

## Configure a Player Controller Listener

Add `PerceptionKnowledgeListenerComponent` to the Player Controller and assign the Profile. The possessed Pawn is the Body Actor and supplies the viewpoint/direction. This is suitable for recording what the controlled character perceives even if the game uses a separate top-down camera.

Unpossessing suspends observations. Possessing another Pawn clears native perception, requests an AI Perception listener update, and preserves current knowledge.

If a project-specific Player Controller arrangement prevents the Controller component from receiving native perception, the supported fallback is placing the Listener on the Pawn. Use that only after verifying the Controller setup; it changes the Listener's ownership lifetime.

## Configure an AI Controller Listener

Add the same Listener Component and Profile to the AI Controller, then possess its Pawn normally. The Profile and behavior are identical to the Player Controller configuration.

## Emit events

`EmitObservableEvent` is for non-acoustic events and, in Milestone 1, accepts the Sight sense only. It routes only to listeners that already perceive the Source through Sight.

For sound:

```cpp
FPerceptionKnowledgeNoiseRequest Noise;
Noise.EventTag = DoorOpenedNoiseTag;
Noise.Instigator = Character;
Noise.bUseSourceLocation = true;
Noise.Loudness = 1.0f;
Noise.MaxRange = 1200.0f;
Noise.Strength = 1.0f;
Source->EmitSemanticNoise(Noise);
```

Do not call native `ReportNoiseEvent` directly when semantic data is required; it would have no correlation entry.

## Query knowledge

Use:

- `GetKnownState(EntityId, StateTag)`;
- `GetKnownStatesForEntity(EntityId)`;
- `GetRecentEvents()`;
- `IsEntityCurrentlyPerceived(EntityId, SenseTag)`;
- `ForgetEntity(EntityId)`;
- `InvalidateKnownState(EntityId, StateTag)`.

All collection-returning APIs return copies.

## Build a snapshot

Fill `FPerceptionKnowledgeSnapshotFilter` with optional Entity IDs, State Tags, Sense Tags, and maximum age, then call `BuildKnowledgeSnapshot`. Empty collections mean “all”; negative Max Age disables age filtering. The result contains copied known states, build time, and global revision, with no pointer back to a Source or internal map.
