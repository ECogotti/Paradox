# GameplayActionsAI

`GameplayActionsAI` is an optional bridge between the generic `GameplayActions` scheduler and Unreal
AI authoring. The plugin depends on `GameplayActions`, Behavior Trees, and StateTree; the core action
plugin has no dependency on AI.

## Execution spec

`FGameplayActionExecutionSpec` represents the Blueprint workflow: select a Definition, copy its
defaults, apply scheduling/context overrides, and submit the factory-created request.

The `Parameters` Property Bag has a fixed schema. Editor tooling synchronizes it when `Definition`
changes and preserves fields whose name and reflected type still match. A stale schema is warned in
the details panel and rejected at runtime. Bindings may overwrite existing values but cannot add or
replace fields.

## Behavior Tree

`Execute Gameplay Action` accepts the execution spec, optional Blackboard parameter bindings, an
optional explicit component-owning Actor, and optional Struct outputs for handle, submission result,
and terminal result. It remains `In Progress` while the accepted action is queued or running and
succeeds only for terminal state `Succeeded`. Synchronous completion inside `Action Start` is safe.

Blackboard bindings support bool, int32, float, enum, name, string, object, class, vector, rotator,
arbitrary Struct keys, and soft references represented by `FSoftObjectPath` Struct keys.

`Cancel Gameplay Action` cancels one `FGameplayActionHandle`. `Can Submit Gameplay Action` performs
side-effect-free preflight and can optionally require `AcceptedStarted` instead of `AcceptedQueued`.

## StateTree

The plugin provides matching Execute, Cancel, and Can Submit nodes. StateTree properties, including
the fixed-layout Property Bag fields, are bindable. Execute remains running through queue residence;
state exit cancels only the task's own handle by default.

## Component resolution

Resolution order is explicit Actor, controlled Pawn, then AIController. More than one component on a
candidate Actor is an error. An explicit Actor with no component does not silently fall back.

## Ownership and callbacks

Execute nodes register with `OnActionEndedNative()` before submission. Ended events emitted
synchronously during submission are buffered until the authoritative handle is known. Abort, exit,
completion, and teardown remove only their own delegate handle. StateTree exit retains the exact
component that accepted the handle, so a later possession or component-layout change cannot redirect
cancellation to a different Actor.

## Validation

The `GameplayActionsAI.*` automation suite covers fixed-schema synchronization and deep copy,
authoritative request construction, all supported primitive and structured Blackboard bindings,
ambiguous component resolution, synchronous completion, Behavior Tree queue/rejection/abort
behavior, and StateTree observer binding, cancellation, and late-event cleanup.
