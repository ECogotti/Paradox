# PerceptionKnowledge runtime module

The module also provides the generic event-driven Hearing Range renderer and read-only listener
profile/range/body views. They contain no clone, replay, investigation, priority, or paradox rule.
Profile runtime setters, editor property changes, and explicit C++ bulk notifications propagate
event-driven to every bound Listener. Native Sight/Hearing configs are refreshed before listener
configuration observers, including the Hearing renderer, are notified.

This module contains the entire Milestone 1 implementation.

Public API is organized by responsibility:

- `Blueprint`: safe value construction and identity/value formatting.
- `Components`: observable Source and observer-owned Listener.
- `Data`: reusable perception Profile.
- `Interfaces`: explicit computed-state providers.
- `Settings`: bounded Hearing correlation and debug timer settings.
- `Subsystems`: weak world registry and event routing.
- `Types`: value-only identity, observation, state, snapshot, result, debug, and statistics structures.

Private implementation mirrors the public responsibilities and keeps all Automation Tests under `Private/Tests`. The module owns `LogPerceptionKnowledge`, the `PerceptionKnowledge.Debug` CVar, and native Sight/Hearing Gameplay Tags.

The runtime module must remain independent from project-specific plugins. Future IntentReplay and GOAP integrations should consume delegates and snapshots from outside this module rather than adding reverse dependencies.
