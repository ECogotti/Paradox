# GameplayActionsAI runtime module

The runtime module owns the execution spec, request builder, component resolver, Behavior Tree
nodes, StateTree nodes, and automation tests.

Use `GameplayActionsAI::BuildRequest` in native integrations instead of manually constructing
`FGameplayActionRequest`. A successful build has passed through the core factory and copied the
complete fixed schema. This module never modifies Definitions at runtime.

BT and StateTree Execute nodes bind the core native `Ended` observer before submission. Their
delegate ownership is per node execution, and cancellation always targets only the handle and
component used by that execution.

The runtime automation coverage includes synchronous terminal events, queued Behavior Tree
execution and abort, rejected submissions, and the StateTree observer's exact-component and
late-callback lifetime guarantees.
