# GameplayActionsAIEditor module

This editor-only module customizes `FGameplayActionExecutionSpec`.

Changing `Definition` replaces the Property Bag layout with the Definition layout and migrates values
that retain the same name and reflected type. Loaded stale schemas remain visibly warned until the
Definition is reselected. The module is never loaded in runtime targets.
