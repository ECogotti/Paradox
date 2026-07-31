# GameplayActionsGridWorld runtime module

AI and non-AI paths make intentional different-start recalculation observable through their module
log while preserving strict validation for every other mismatch.

The module owns the native Grid Move Definition, action instance, tags, logging category, and tests.
AIControllers call the exported GridWorld task directly instead of duplicating navigation or goal
contention. Other Controllers project the requested goal through GridWorld and execute the resulting
path through their existing `UPathFollowingComponent`.

The bridge deliberately works against the base path-following API. A player controller can therefore
install `UGridWorldPathFollowingComponent` and receive GridWorld's Center-Constrained or Cell-by-Cell
behavior without introducing a Paradox-specific dependency into the bridge.

The current `UGridMoveToCellTask` is retained by a reflected transient property until completion or
cleanup. Late callbacks are rejected after the delegate is removed.

Pause and resume use the narrow public wrappers exported by GridWorld. Cleanup removes this action's
delegate before calling `ExternalCancel`, preventing cancellation callbacks from re-entering a
terminal Gameplay Action.

The non-AI Controller path also owns its exact path-following request, delegate and optional endpoint
claim. Cleanup aborts only that request, releases its claim and rejects late completion callbacks.
It supports player movement to a location or to the current position of a goal actor. Both
destination and exact requests implement the default `StopBeforeOccupied`: the final cell is checked
against published occupancy owners and traffic claims while ordinary A* still computes the route to
the requested cell. A foreign-owned final cell is removed from that route, and its immediate
predecessor is atomically claimed. Injected paths distinguish requested and effective goals so a
preview-to-commit occupancy change can shorten an unadjusted path exactly once. `RejectOccupied`
remains available for strict failure. A successful non-partial result converts the effective claim
to Pawn parking; every other terminal path releases it. Moving-goal tracking and the redirect/wait
alternative-goal policies remain owned by `UGridMoveToCellTask`.
