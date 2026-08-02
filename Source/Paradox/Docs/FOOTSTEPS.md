# Footsteps, semantic Hearing, and crouch

## Runtime flow

The generic `FootstepSystem` owns animation notifies, foot socket selection, floor traces, Physical
Material resolution, audio, Niagara, decals, and immutable `FFootstepEvent` snapshots.

The Paradox runtime module adds `UParadoxFootstepNoiseComponent`. It observes the generic native
delegate and converts valid contacts to `FPerceptionKnowledgeNoiseRequest` values:

```text
UAnimNotify_Footstep
    -> UFootstepComponent
        -> FFootstepEvent
            -> UParadoxFootstepNoiseComponent
                -> UPerceptionKnowledgeSourceComponent::EmitSemanticNoise
                    -> PerceptionKnowledge listener
                        -> UIntentReplayObservationComponent
```

The adapter never calls Intent Replay or IntentReplayPerception. Observation recording, matching,
and clone response therefore contain only events that native Hearing actually delivered.

Correlation is added later by `IntentReplayPerception`: when the Character Source is executing one
unambiguous replay-owned movement action, its footstep observation receives that action's verified
`RecordedIntentId`. Paradox treats this ID as the one-to-one occurrence identity, so a path recovery
that delays or spatially shifts an already-heard historical footstep does not create an
investigation. A second footstep without another expected record, or the same tag from a different
Source Entity ID, remains unexpected.

A verified replay correlation never suppresses a genuinely new external Source. For example, T0
has no T1 footsteps in its original track; once T1 becomes a clone, its footstep remains
`UnexpectedObservation` for T0 and the Paradox Hearing policy starts an investigation. Only a
verified footstep caused by the observing clone itself is ignored.

## Character setup

`AParadoxCharacter` creates these inherited components for both player and clone roles:

- `FootstepComponent`;
- `PerceptionKnowledgeSourceComponent`;
- `ParadoxFootstepNoiseComponent`.

`BP_PlayerAstronaut` and `BP_CloneAstronaut` use:

- `DA_AstronautFootsteps` as the generic cosmetic profile;
- `LeftFoot` and `RightFoot` as the foot socket/bone names;
- a Hearing-only PerceptionKnowledge Source;
- `DA_AstronautFootstepNoise` as the project AI-noise profile.

`DA_AstronautFootsteps` contains one explicit fallback response. It plays
`SC_MetalFootsteps` and `FX_Footstep` for every Physical Surface and leaves decals disabled.
No sound, Niagara, or material references are duplicated in the AI profile.

The walk and run animation sequences contain one left and one right `UAnimNotify_Footstep` at their
foot-contact frames. Notify intensity is `1.0` and socket override is empty, so the component's
`LeftFoot`/`RightFoot` configuration remains authoritative.

## AI-noise profile

`UParadoxFootstepNoiseProfile` maps `EPhysicalSurface` to
`FParadoxFootstepNoiseResponse` and supports one explicit fallback. Each response contains:

- `EventTag`;
- `CauseTag`;
- `BaseLoudness`;
- `MaxRange`;
- `bEmitNoise`.

The astronaut profile uses one fallback:

```text
EventTag    = PerceptionKnowledge.Event.Noise.Character.Footstep
CauseTag    = PerceptionKnowledge.Cause.CharacterMovement.Footstep
BaseLoudness = 1.0
MaxRange     = 0.0
bEmitNoise   = true
```

Effective Loudness and Strength are `BaseLoudness * FFootstepEvent.NormalizedIntensity`.
`MaxRange=0` means the existing listener profile controls range; its native default Hearing range
is 3000 cm. Set a positive Max Range only when the source needs a stricter limit.

An unmapped surface without fallback is observable as `MissingSurfaceResponse`. A response with
`bEmitNoise=false` produces `DisabledBySurface` without disabling generic audio or VFX.

## Crouch

`AParadoxCharacter` enables native CharacterMovement crouch support. `IA_Crouch` is mapped to
Left Control in `IMC_Default`; the player controller handles its `Started` event as a toggle and
calls `RequestSetCrouched` with an absolute value. The ready-to-use definition is:

```text
/Game/Data/GameplayActions/DA_ParadoxSetCrouched
```

It creates `UParadoxSetCrouchedAction`, uses
`GameplayAction.Character.SetCrouched`, and writes `DesiredCrouched` to the recorded request. The
action calls `Crouch()` or `UnCrouch()` and completes as soon as Character Movement accepts the
persistent request.

The definition owns only `GameplayAction.Lock.Stance`, uses neutral priority and
`BlockedPolicy=Queue`. Grid movement owns only `GameplayAction.Lock.Movement`; because Gameplay
Actions compares locks exactly, stance starts immediately during a long movement and the movement
remains `Running`. Queueing applies only if another action still owns the Stance lock. Input
toggle state considers both `IsCrouched()` and `bWantsToCrouch`, so rapid crouch/uncrouch requests
do not wait for the next Character Movement update.

At the start of every new time-loop recording, Paradox forces and validates the standing baseline.
Intent Replay then records and reproduces each absolute crouch and uncrouch transition without
fracturing or reissuing movement.

The adapter reads `ACharacter::IsCrouched()` at the exact footstep callback. It does not infer
stance from input or animation state.

When `bIgnoreNoiseDuringCrouch` is enabled, crouched footsteps still generate generic
FootstepSystem events, audio, and VFX, but no semantic AI noise is emitted. When disabled,
crouched footsteps use the normal surface response without a loudness or range multiplier.

`ABP_Astronaut` and `AS_astronaut_crouch_walk` are intentionally not modified. When the crouch
animation is integrated later, add left/right `UAnimNotify_Footstep` contacts to every crouched
locomotion sequence that should retain cosmetic footsteps.

## Blueprint and C++ access

Characters expose read-only getters for all three components. The adapter exposes:

- `HasProcessedFootstep`;
- `GetLastResult`;
- `GetLastDiagnosticMessage`;
- `GetNoiseProfile`;
- `IgnoresNoiseDuringCrouch`;
- `IsDebugEnabled`.

`EParadoxFootstepNoiseResult` distinguishes emitted, crouch-suppressed, surface-disabled,
configuration, invalid-event, invalid-owner, and PerceptionKnowledge emission failures.

In C++, observe the neutral event on the generic component when project AI policy is not required:

```cpp
Character->GetFootstepComponent()
    ->OnFootstepGeneratedNative()
    .AddUObject(this, &ThisClass::HandleFootstep);
```

Use the Paradox adapter's read-only result for diagnostics; do not emit a duplicate AI noise from
the same footstep.

## Networking

Neither the generic system nor the Paradox adapter adds replication or RPCs. Generic cosmetic
feedback is skipped on dedicated servers. Semantic noise remains server-capable because
PerceptionKnowledge and AI Perception are authoritative runtime systems.

## Debugging

Project drawing requires both:

```text
Paradox.Footsteps.Debug 1
ParadoxFootstepNoiseComponent.bEnableDebug = true
```

The adapter draws the contact, a line to the owner, a positive source Max Range when present, and a
label containing surface, tag, loudness, range policy, crouch state, suppression switch, and result.
With listener-controlled range, enable the listener's Hearing Range renderer instead.

Related global controls are:

```text
FootstepSystem.Debug 1
PerceptionKnowledge.Debug 1
IntentReplayPerception.Debug 1
```

The Paradox adapter deliberately does not redraw the generic socket or floor trace.

Common failures:

- `MissingNoiseProfile`: assign `DA_AstronautFootstepNoise` on the inherited adapter;
- `MissingSurfaceResponse`: enable a fallback or author the reported Physical Surface;
- `InvalidEvent`: verify the generic floor trace hit and the event belongs to the same owner;
- `EmissionFailed`: verify the Hearing-only Source is registered and the Event Tag is valid;
- no cosmetics on a configured surface: verify `DA_AstronautFootsteps` still uses its fallback;
- no crouched footsteps after a future animation change: add the two generic footstep notifies to
  the crouched locomotion sequence.

## PIE verification

Use `MA_Playground` and place or identify two floors with different Physical Materials.

1. Enable the three component-local debug flags needed for generic footsteps, the adapter, and the
   listener, then run `FootstepSystem.Debug 1`, `Paradox.Footsteps.Debug 1`, and
   `PerceptionKnowledge.Debug 1`.
2. Walk and run across both floors. Each contact must use the same sound and Niagara fallback while
   the adapter reports the actual Physical Surface.
3. Keep a clone/listener inside 3000 cm. A standing contact must produce `Emitted` and a Hearing
   observation. Move the listener outside its range and verify it receives none.
4. Press Left Control once and move. The Character must be crouched; audio and Niagara remain
   active, while the adapter reports `SuppressedByCrouch` and no Hearing observation is created.
5. Set `bIgnoreNoiseDuringCrouch=false` on the adapter and repeat. The result must be `Emitted`.
6. Record a run and replay it with `IntentReplayPerception.Debug 1`. Verify heard footsteps enter
   the observation track and comparison journal only through PerceptionKnowledge.
7. During a long movement, press Left Control twice in quick succession. Crouch and uncrouch must
   apply immediately, the movement action must remain `Running`, and both absolute stance actions
   must appear in the replay track.
8. Rewind and verify the clone reproduces both stance changes while continuing its recorded move.
9. Disable each global debug CVar and verify its drawing stops immediately.
