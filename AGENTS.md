# AGENTS.md

This file defines the global rules for AI-assisted development in this Unreal Engine project.

These rules apply to the entire repository unless a more specific local instruction explicitly overrides them.

---

# GOLDEN RULES

These rules are mandatory.

## 1. Always read local `CODEX` instructions before working

Before modifying, adding, deleting, debugging, or refactoring code inside any module or plugin:

1. Identify the module or plugin that owns the code.
2. Search its directory and relevant subdirectories for a folder named:

   `CODEX`

3. If a `CODEX` folder exists, you MUST enter it.
4. Read all relevant Markdown files inside it before making any change.
5. Follow those instructions for the entire task.

A module or plugin may contain multiple `CODEX` folders at different levels.

The closer a `CODEX` folder is to the code being modified, the more specific its instructions are.

Instruction priority is:

1. Root `AGENTS.md` Golden Rules.
2. Closest relevant `CODEX` instructions.
3. Parent module/plugin `CODEX` instructions.
4. General rules in this file.

Local `CODEX` instructions may extend or specialize these rules, but they must never silently violate the Golden Rules.

Do not touch code inside a module or plugin before checking for its `CODEX` instructions.

---

## 2. If it does not compile, the task is not finished

Compilation is part of implementation.

A task is not complete until all code affected by the change compiles successfully.

Never consider a task finished because:

- the code looks correct;
- the implementation is theoretically correct;
- the changed file has no obvious syntax errors;
- only an unrelated file appears to fail;
- the editor has not yet been restarted;
- compilation was skipped to save time.

After every meaningful code change:

1. Compile the appropriate target.
2. Read the complete relevant error output.
3. Fix errors caused by the change.
4. Recompile.
5. Repeat until the affected target builds successfully.

Never hide a compilation problem by:

- commenting out broken code;
- disabling warnings;
- removing functionality;
- weakening type safety;
- bypassing an API;
- suppressing an error without understanding it.

If compilation cannot be completed because of an external limitation, explicitly report:

- what was changed;
- what was not validated;
- why validation was impossible;
- the exact remaining risk.

---

## 3. Understand before modifying

Never start by editing code.

Before making changes:

1. Identify the owning module or plugin.
2. Read its `CODEX` instructions.
3. Inspect the relevant public API.
4. Inspect the implementation.
5. Find the main callers and dependencies.
6. Understand object ownership and lifetime.
7. Understand how the system is exposed to Blueprint.
8. Understand how the system is initialized and shut down.
9. Identify existing patterns already used by the module.

Do not guess architecture from a single file.

Do not invent a new pattern before checking whether the project already has one.

---

## 4. Make the smallest correct change

Prefer the smallest change that fully solves the actual problem.

Do not:

- refactor unrelated systems;
- rename unrelated types;
- reformat entire files;
- move files unnecessarily;
- redesign working architecture while fixing a localized bug;
- change public APIs without need;
- introduce abstractions that are not required by the task.

Every changed line increases risk.

A change should be easy to understand, review, test, and revert.

---

## 5. Never invent Unreal Engine APIs

Do not write code based on vague memory of an Unreal Engine API.

Before using an unfamiliar engine type, function, macro, subsystem, delegate, or lifecycle event:

1. Search the existing project.
2. Inspect the relevant engine or plugin headers when available.
3. Verify the actual signature.
4. Verify the required module dependency.
5. Verify the API is valid for the Unreal Engine version used by this project.

Never fabricate:

- function names;
- delegate signatures;
- override signatures;
- module names;
- include paths;
- reflection specifiers;
- engine behavior.

When uncertain, investigate before writing code.

---

## 6. Preserve existing behavior unless change is intentional

A bug fix must not silently become a behavioral redesign.

Before changing existing behavior, identify:

- what currently happens;
- what should change;
- what must remain unchanged.

Preserve existing:

- public contracts;
- Blueprint APIs;
- serialized data;
- save compatibility;
- asset references;
- default behavior;

unless the task explicitly requires changing them.

Breaking changes must be intentional and clearly identified.

---

## 7. Respect Unreal ownership, lifetime, reflection, and garbage collection

Never treat UObject-based types like normal C++ objects.

Always verify:

- who owns the object;
- who creates it;
- who destroys it;
- whether the reference must be tracked by the garbage collector;
- whether the object can become invalid asynchronously;
- whether the object exists on the CDO;
- whether the code may run in the editor;
- whether the code may run during shutdown.

Use Unreal-aware pointer and ownership patterns.

Do not introduce raw UObject pointers as persistent state without understanding lifetime implications.

Do not use `NewObject`, `CreateDefaultSubobject`, `SpawnActor`, `MakeShared`, or raw allocation interchangeably.

Use the correct ownership model for the type and lifecycle.

---

## 8. Public APIs must remain safe

Never expose an API that requires callers to know hidden implementation assumptions.

Public functions must:

- validate their inputs when failure is possible;
- fail predictably;
- clearly define ownership;
- avoid unexpected side effects;
- preserve invariants;
- return useful failure information where appropriate.

Do not rely on call order unless the API explicitly enforces or documents it.

Do not use `check()` for recoverable runtime conditions.

Do not silently continue after corrupted state.

---

## 9. Never hide failures

Failures must remain observable.

Do not:

- swallow errors;
- ignore invalid states;
- silently return from unexpected failures;
- convert errors into fake successes;
- catch a problem only to hide it;
- remove diagnostics because they are noisy.

Use the appropriate mechanism:

- return value;
- result enum;
- assertion;
- ensure;
- warning;
- error log.

The severity must match the actual problem.

---

## 10. Fix causes, not symptoms

Do not patch the visible symptom until the state transition that produced it is understood.

When debugging:

1. Identify where the incorrect state first appears.
2. Trace how it propagates.
3. Find the earliest incorrect assumption or transition.
4. Fix the root cause.
5. Validate that downstream symptoms disappear.

Avoid timing hacks, arbitrary delays, repeated retries, and state resets unless they are part of the intended design.

---

## 11. Existing project conventions beat personal preference

Consistency is more valuable than introducing a theoretically better style in one isolated area.

When a module already has a clear convention:

- follow it;
- extend it;
- improve it only when the task explicitly requires architectural work.

Do not mix multiple architectural patterns inside the same system without a strong reason.

---

## 12. Do not modify Unreal Engine installation source; keep project binaries updated

This project uses an installed binary build of Unreal Engine. The Engine C++ source is not part of
the project and is not an available modification surface.

Do not attempt to solve project or plugin problems by editing files inside the Unreal Engine
installation. Implement changes inside the project-owned modules and plugins by using the APIs
exposed by the installed Engine. If a correct solution would genuinely require an Engine source
change, stop and report the required change to the user instead of attempting it.

Project build outputs are different: compiling is expected to update files such as the project
DLLs in `Binaries` and build state in `Intermediate`. After a successful compilation, leave the
project DLLs aligned with the latest source code.

Never restore, check out, replace, or otherwise roll back a freshly built project DLL merely to
clean the Git diff. A task is not complete while the project source and its compiled DLLs are out
of sync. If a project DLL is missing or stale, rebuild the affected target and keep the resulting
DLL current.

---

## 13. Documentation is part of implementation

Every module or plugin you work on must contain user-facing documentation in a `Docs` folder.

Before finishing work on a module or plugin:

1. Locate its `Docs` folder.
2. If it does not exist, create it.
3. Read the existing documentation relevant to the change.
4. Update the documentation when behavior, usage, configuration, extension points, debugging, setup, or public APIs change.
5. Ensure the documentation explains both how the system works and how a user is expected to use it.

Documentation is written for developers and designers using the module or plugin.

The `Docs` folder is NOT a replacement for `CODEX` instructions.

- `CODEX` contains instructions for Codex and AI-assisted work.
- `Docs` contains documentation for human users of the module or plugin.

Do not put AI-specific rules inside `Docs`.

Do not consider a user-facing feature complete when the relevant documentation is missing or outdated.

---

# REQUIRED WORKFLOW

Follow this workflow for every non-trivial task.

## Step 1 — Establish scope

Determine:

- what behavior is requested;
- which module or plugin owns it;
- which files are probably involved;
- what must not change.

---

## Step 2 — Load project-specific instructions

Search for all relevant `CODEX` folders.

Read their Markdown instructions before modifying code.

---

## Step 3 — Read relevant user documentation

Locate the relevant module/plugin `Docs` folder.

Read the documentation related to the system being changed so that implementation and documented behavior remain aligned.

If the `Docs` folder does not exist, plan to create it before finishing the task.

---

## Step 4 — Investigate the current implementation

Inspect:

- public declarations;
- private implementation;
- initialization;
- shutdown;
- call sites;
- ownership;
- state transitions;
- Blueprint exposure;
- relevant tests or debug tools.

---

## Step 5 — Define the minimum solution

Before coding, determine:

- the root cause or requested behavior;
- the smallest safe change;
- possible side effects;
- how the result will be verified.

---

## Step 6 — Implement

Follow:

- existing architecture;
- Unreal Engine conventions;
- module boundaries;
- local `CODEX` rules.

---

## Step 7 — Compile

Compile the affected target.

Fix every error caused by the change.

---

## Step 8 — Validate behavior

Verify:

- expected path;
- relevant failure paths;
- invalid inputs;
- repeated execution;
- initialization and shutdown when relevant;
- Blueprint behavior when relevant.

---

## Step 9 — Update documentation

Update the relevant Markdown files inside `Docs`.

Document at least the parts affected by the change:

- purpose;
- behavior;
- setup;
- usage;
- configuration;
- Blueprint or C++ API usage;
- extension points;
- debugging and troubleshooting where relevant.

Do not copy implementation details into documentation unless they help users understand or extend the system.

---

## Step 10 — Review the diff

Before considering the task complete, check:

- every changed file;
- every changed public API;
- every new dependency;
- every new include;
- every new Blueprint-exposed property;
- every new log;
- every new debug visualization;
- every changed documentation file.

Remove accidental or unrelated changes.

---

# DEBUGGING

Debugging support is part of the architecture, not temporary garbage to leave scattered throughout the codebase.

Every non-trivial system should be designed so that its important runtime state can be inspected.

---

## Visual debugging

Use visual debugging when spatial information is important.

Examples:

- positions;
- directions;
- traces;
- collision shapes;
- paths;
- target selection;
- ranges;
- bounds;
- navigation data;
- orientation;
- state transitions tied to world-space objects.

Appropriate tools include:

- lines;
- points;
- arrows;
- spheres;
- capsules;
- boxes;
- cones;
- text labels;
- trace visualization.

Visual debugging must never be permanently forced on.

---

## Local visual debug control

Every Actor, Actor Component, UObject, or system instance that produces visual debug output must provide a local way to disable that debug output.

Example concept:

`bEnableDebug`

The exact implementation may differ depending on the system.

Local debug controls should normally be:

- disabled by default;
- easy to enable for one specific instance;
- available to designers when useful.

Enabling debug on one Actor must not require enabling it on every Actor of the same type.

---

## Global visual debug control

Every module or plugin that produces runtime visual debug output must also provide a global way to disable all visual debug output owned by that module.

The global mechanism may be implemented using the pattern most appropriate for the module, such as:

- console variable;
- developer settings;
- module-level debug settings;
- dedicated debug subsystem.

The effective debug state should follow this principle:

`Global Debug Enabled AND Local Debug Enabled`

Disabling the global debug option must immediately prevent visual debug output from every object owned by that module or plugin.

---

## Debug drawing rules

Visual debug code must:

- be clearly identifiable;
- have negligible cost while disabled;
- avoid unnecessary allocations;
- avoid expensive calculations when debug is disabled;
- avoid shipping-only dependencies on editor code.

Where appropriate, guard debug drawing using Unreal debug compilation support.

Do not perform an expensive calculation merely to decide not to draw its result.

Check debug state first.

---

## Debug lifetime

Choose draw lifetime intentionally.

Use:

- one-frame drawing for continuously updated data;
- short-duration drawing for transient events;
- persistent drawing only when explicitly useful.

Do not create permanent debug clutter by default.

---

## Trace debugging

When debugging traces, make it possible to understand:

- trace start;
- trace end;
- trace shape;
- trace direction;
- hit location;
- hit normal;
- selected hit;
- rejected hits when relevant.

When trace filtering is complex, expose enough information to explain why a candidate was accepted or rejected.

A trace result that is visually correct but logically rejected must be diagnosable.

---

# LOGGING

Each module must own exactly one primary log category.

Example naming pattern:

`Log<ModuleName>`

Examples:

- `LogCombat`
- `LogInteraction`
- `LogTraversal`

Do not use `LogTemp` for committed module code.

`LogTemp` is acceptable only for extremely temporary local investigation and must be removed before the task is finished.

---

## Module log category

A module should declare and define its own log category using the standard Unreal pattern.

Conceptually:

```cpp
DECLARE_LOG_CATEGORY_EXTERN(LogMyModule, Log, All);
```

and:

```cpp
DEFINE_LOG_CATEGORY(LogMyModule);
```

The exact location should follow the module's architecture.

---

## Required logging macros

Every module must provide shortcut macros for at least:

- Info;
- Warning;
- Error.

Conceptual naming:

```cpp
MYMODULE_LOG_INFO(...)
MYMODULE_LOG_WARNING(...)
MYMODULE_LOG_ERROR(...)
```

The macros must use the module's own log category.

Keep macro names clearly scoped to the owning module to avoid collisions.

---

## Logging severity

Use severity intentionally.

### Info

Use for meaningful lifecycle and high-level state information.

Do not log every frame.

Do not use Info for expected high-frequency operations.

### Warning

Use when:

- execution can continue;
- the state is unexpected;
- configuration is probably wrong;
- functionality is degraded.

### Error

Use when:

- an operation failed;
- requested functionality could not be completed;
- the system entered a state requiring developer attention.

Do not report ordinary user input or expected branch failures as errors.

---

## Useful log context

Logs should contain enough context to identify the problem.

Where relevant include:

- object name;
- world;
- owning Actor;
- component;
- requested operation;
- relevant identifier;
- current state;
- expected state.

Avoid vague logs such as:

`Something failed`

Prefer logs that answer:

- what failed;
- where;
- for which object;
- under which state.

---

## Logging performance

Never spam logs from:

- Tick;
- animation updates;
- physics callbacks;
- repeated traces;
- high-frequency loops;

unless explicitly protected by debug configuration.

Repeated identical warnings should be rate-limited, state-triggered, or emitted once where appropriate.

---

# NAMING CONVENTIONS

Follow Unreal Engine naming conventions unless the existing module has a stronger established convention.

---

## Unreal type prefixes

Use standard Unreal prefixes:

- `U` for UObject-derived types.
- `A` for Actor-derived types.
- `F` for structs and non-UObject value types.
- `E` for enums.
- `I` for interface implementation types.
- `T` for templates.
- `S` for Slate widgets where appropriate.

Do not invent new Hungarian-style prefixes.

---

## Classes and structs

Use PascalCase.

Examples:

- `UInteractionComponent`
- `ATraversalVolume`
- `FInteractionRequest`
- `ETraversalState`

Names must communicate responsibility.

Avoid generic names such as:

- `Manager`
- `Helper`
- `Utility`
- `Handler`

unless the responsibility is genuinely clear from the full type name.

---

## Functions

Use PascalCase.

Functions should normally begin with a verb.

Examples:

- `StartInteraction`
- `CancelTraversal`
- `FindBestTarget`
- `UpdateCachedState`

Boolean queries should normally use:

- `Is`
- `Has`
- `Can`
- `Should`

Examples:

- `IsActive`
- `HasValidTarget`
- `CanStartInteraction`
- `ShouldUpdate`

---

## Variables

Use PascalCase following Unreal conventions.

Boolean variables must use the `b` prefix.

Examples:

- `CurrentTarget`
- `InteractionRadius`
- `bIsActive`
- `bEnableDebug`

Do not use:

- `m_`
- `_member`
- arbitrary type prefixes.

---

## Function parameters

Names must make intent clear.

Use semantic prefixes when useful:

- `In` for input where ambiguity exists;
- `Out` for output parameters;
- `InOut` only when truly necessary.

Examples:

- `InTarget`
- `OutHitResult`

Do not add prefixes mechanically when the parameter is already unambiguous.

---

## Files

A file should normally match its primary type.

Examples:

- `InteractionComponent.h`
- `InteractionComponent.cpp`

Avoid placing unrelated systems inside the same file.

---

## Interfaces

Use the standard Unreal interface pair.

Example:

- `UInteractable`
- `IInteractable`

Interface functions should describe capabilities, not implementation details.

---

## Components

Component names should describe the capability they add.

Examples:

- `InteractionComponent`
- `TargetingComponent`
- `TraversalComponent`

Avoid components that become containers for unrelated behavior.

---

## Subsystems

Subsystem names must identify:

- scope;
- responsibility.

Examples:

- `UInteractionWorldSubsystem`
- `UTraversalGameInstanceSubsystem`

Do not use a subsystem simply as a global singleton.

Choose its lifetime because that lifetime is correct for the responsibility.

---

## Delegates and events

Delegate names should clearly describe the event.

Examples:

- `OnInteractionStarted`
- `OnTargetChanged`
- `OnTraversalCompleted`

Use past tense for notifications representing something that already occurred.

Avoid ambiguous names such as:

- `OnChanged`
- `OnUpdated`

when the owner or changed state is unclear.

---

## Enums

Use strongly typed enums where appropriate.

Example:

```cpp
enum class EInteractionState : uint8
```

Enumerator names should be concise because the enum type already provides context.

Prefer:

- `Inactive`
- `Searching`
- `Interacting`

over:

- `InteractionStateInactive`
- `InteractionStateSearching`

---

## Log categories

Use:

`Log<ModuleName>`

Do not create a new log category for every class.

---

## Abbreviations

Avoid unnecessary abbreviations.

Accept common and established terms such as:

- AI
- UI
- ID
- HTTP
- UE

Prefer clear full names for project-specific concepts.

---

# C++ FOLDER STRUCTURE

Every module and plugin must use a predictable folder structure.

Prefer consistency over personal preference.

Do not reorganize an established module only to match this structure unless the task explicitly requires structural cleanup.

For new modules and plugins, follow these rules by default.

---

## Standard module structure

A runtime module should normally follow this structure:

```text
Source/
└── MyModule/
    ├── MyModule.Build.cs
    ├── Public/
    ├── Private/
    ├── CODEX/
    └── Docs/
```

The module implementation entry point should normally live directly in the appropriate root source folder:

```text
Public/MyModule.h
Private/MyModule.cpp
```

Only expose the module header publicly when external modules actually need it.

---

## Public and Private boundaries

Use `Public` only for headers that form part of the module's external C++ API.

Use `Private` for:

- implementation-only classes;
- private headers;
- internal helpers;
- algorithms not intended for external modules;
- implementation details.

Do not place a header in `Public` merely because another class inside the same module needs it.

Files inside the same module can include private headers where appropriate.

Minimize the module's public surface.

---

## Organize by feature or responsibility

Inside `Public` and `Private`, prefer folders based on meaningful responsibilities.

Examples:

```text
Public/
├── Components/
├── Data/
├── Interfaces/
├── Settings/
├── Subsystems/
└── Types/

Private/
├── Components/
├── Data/
├── Settings/
├── Subsystems/
└── Types/
```

These are examples, not mandatory folders.

Create a folder only when the module actually contains that responsibility.

Do not create empty architectural folders in anticipation of hypothetical future code.

---

## Mirror Public and Private where useful

When a public type has a corresponding implementation, use matching folder paths where practical.

Example:

```text
Public/Components/InteractionComponent.h
Private/Components/InteractionComponent.cpp
```

This makes implementation files predictable to locate.

Do not force mirroring for private-only systems.

---

## Prefer domain folders over generic dumping grounds

Avoid generic folders such as:

- `Misc`
- `Common`
- `Helpers`
- `Utilities`
- `Managers`

unless the contained code genuinely forms a coherent, named responsibility.

When a folder becomes a dumping ground for unrelated code, split it by domain.

---

## Avoid excessive folder depth

Folder structure should help navigation, not simulate class namespaces through many nested levels.

Prefer:

```text
Private/Targeting/TargetSelector.cpp
```

over unnecessarily deep structures such as:

```text
Private/Systems/Gameplay/Runtime/Targeting/Selection/TargetSelector.cpp
```

Add nesting only when each level communicates a real architectural boundary.

---

## Keep closely related types together

Group files by system responsibility.

For example, a targeting system may contain:

```text
Targeting/
├── TargetingComponent.h
├── TargetingRequest.h
├── TargetingResult.h
└── TargetFilter.h
```

Do not scatter one feature across unrelated folders solely based on C++ type category.

Choose the structure that makes the system easiest to understand as a whole.

---

## Runtime and Editor code must be separated

Editor-only dependencies must not leak into runtime modules.

When a plugin requires substantial editor tooling, prefer a separate editor module.

Example:

```text
Plugins/MyPlugin/
├── MyPlugin.uplugin
├── Docs/
├── CODEX/
└── Source/
    ├── MyPlugin/
    │   ├── Public/
    │   ├── Private/
    │   ├── CODEX/
    │   └── Docs/
    └── MyPluginEditor/
        ├── Public/
        ├── Private/
        ├── CODEX/
        └── Docs/
```

Runtime code must not depend on the editor module.

Editor-only classes, Slate tools, asset actions, factories, details customizations, and editor subsystems belong in an editor module when appropriate.

---

## Plugin-level structure

A plugin should normally follow this structure:

```text
Plugins/
└── MyPlugin/
    ├── MyPlugin.uplugin
    ├── Source/
    ├── Content/        # only when needed
    ├── Config/         # only when needed
    ├── Resources/      # only when needed
    ├── CODEX/
    └── Docs/
```

Do not create optional folders unless the plugin actually uses them.

Plugin-level `Docs` should explain:

- plugin purpose;
- installation and enablement;
- high-level architecture;
- module overview;
- main workflows;
- integration with the project.

Module-level `Docs` should explain the behavior and usage of that specific module.

Avoid unnecessary duplication between plugin and module documentation.

---

## Tests

When a module contains automated tests, keep them clearly separated from production code.

A common pattern is:

```text
Private/Tests/
```

Use a dedicated test module when the scale, dependencies, or project architecture make that more appropriate.

Test-only code must not leak into the public runtime API.

---

## Third-party code

External source code must be clearly separated from project-owned implementation.

Do not mix third-party source files into ordinary module folders.

Use a dedicated `ThirdParty` structure when required by the dependency and the Unreal build setup.

Do not modify vendored third-party code unless the task explicitly requires it.

Document local patches when they are unavoidable.

---

## Folder changes

Moving C++ files can affect:

- includes;
- generated project files;
- module dependencies;
- source control history;
- external references.

Do not move files solely for aesthetic reasons during unrelated tasks.

When a structural change is required:

1. plan the target structure;
2. move only the relevant files;
3. update includes;
4. verify Build.cs dependencies;
5. compile;
6. update documentation if paths or usage changed.

---

# MODULARITY

Code should be organized around responsibilities and explicit boundaries.

The goal is not maximum abstraction.

The goal is to make systems:

- understandable;
- replaceable;
- testable;
- reusable;
- difficult to misuse.

---

## Single responsibility

A class should have one clear reason to change.

Do not allow one class to become responsible for:

- state;
- input;
- UI;
- persistence;
- visualization;
- networking;
- configuration;

unless those responsibilities genuinely belong to the same abstraction.

Split responsibilities by behavior, not by arbitrary file size.

---

## Explicit dependencies

Dependencies must be visible.

Prefer:

- constructor/setup dependencies;
- explicit references;
- interfaces;
- component lookup during controlled initialization;
- subsystem access where lifetime truly matches.

Avoid hidden dependencies through:

- unrelated global variables;
- arbitrary singleton access;
- repeated world searches;
- hard-coded object names.

---

## Module boundaries

Respect Unreal module boundaries.

A module must not access another module's private implementation.

Public headers must contain only the API that external modules actually need.

Keep implementation details inside `Private`.

Before adding a module dependency:

1. confirm it is actually required;
2. determine whether it belongs in Public or Private dependencies;
3. avoid creating circular dependencies.

---

## Avoid circular dependencies

Prefer architectural decoupling through:

- interfaces;
- delegates;
- events;
- data structures;
- mediator subsystems where appropriate.

Do not solve circular dependencies by blindly adding includes.

---

## Prefer composition

Prefer composing behavior through:

- Actor Components;
- UObjects;
- strategies;
- data;
- interfaces;

instead of building deep inheritance trees.

Inheritance should represent a real "is-a" relationship.

Composition should represent capabilities and optional behavior.

---

## Data-driven configuration

Do not hard-code content decisions into reusable systems.

Where appropriate, move configurable behavior into:

- properties;
- structs;
- data assets;
- curves;
- tables;
- configuration objects.

Code should define rules.

Data should define content and tuning.

---

## Avoid god objects

Do not create objects that know everything and coordinate every system.

Large orchestration logic should be split into explicit responsibilities.

A class with many unrelated dependencies is an architectural warning.

---

# REWRITABILITY AND EXTENSIBILITY

Systems must be designed so that designers and programmers can replace behavior without rewriting the entire module.

The default implementation should work out of the box.

Extension points should be intentional.

---

## Provide useful default behavior

An extensible system must still have a complete native implementation.

Do not create empty architecture where every useful behavior must be implemented in Blueprint.

C++ should provide:

- safe defaults;
- core invariants;
- critical state management;
- performance-sensitive behavior.

Blueprint should be able to specialize behavior where appropriate.

---

## Blueprint-overridable behavior

Use `BlueprintNativeEvent` when:

- C++ provides useful default behavior;
- designers may need to replace that behavior.

Use `BlueprintImplementableEvent` primarily when:

- no native behavior is required;
- the event is optional;
- the event is intended as a presentation or content hook.

Do not expose every internal function to Blueprint.

Expose intentional extension points.

---

## C++ overridable behavior

When programmers are expected to specialize internal behavior, provide protected virtual functions where appropriate.

Separate:

- stable public API;
- replaceable protected implementation.

A public function may enforce invariants and delegate customizable behavior to a protected virtual implementation.

Do not force subclasses to reimplement safety checks.

---

## Prefer hooks over copied systems

Provide extension points for meaningful decisions.

Examples:

- target validation;
- score calculation;
- filtering;
- state transition approval;
- result transformation;
- presentation;
- effect execution.

Do not require a designer or programmer to duplicate an entire system just to change one decision.

---

## Use strategy objects for large replaceable behaviors

When a behavior has substantial internal logic and multiple implementations are expected, consider a dedicated strategy object.

Appropriate forms may include:

- instanced UObjects;
- abstract UObject classes;
- interfaces;
- dedicated components.

Do not turn every small function into a strategy object.

Use this pattern when the behavior represents a genuine replaceable policy.

---

## Use delegates for observation

Use delegates when external systems need to react without controlling the internal implementation.

Examples:

- state changed;
- target acquired;
- action started;
- action completed;
- request failed.

Observers should not need to modify the source system.

---

## Designer-facing APIs

Blueprint-exposed properties and functions must be usable without reading C++ source code.

Use appropriate:

- categories;
- tooltips;
- edit conditions;
- clamp metadata;
- display names;
- advanced display options.

Avoid exposing internal state purely because it might someday be useful.

---

## Do not expose mutable internals

Prefer controlled operations over direct mutable access.

Avoid exposing internal arrays, maps, state structures, or pointers when callers could break system invariants.

Prefer:

- getters;
- queries;
- commands;
- validated setters;
- read-only views where possible.

---

# UNREAL OBJECT LIFECYCLE

Always reason about lifecycle explicitly.

Relevant phases may include:

- construction;
- CDO creation;
- component creation;
- registration;
- initialization;
- BeginPlay;
- gameplay;
- EndPlay;
- destruction;
- world teardown;
- module shutdown.

Never assume an object is fully initialized merely because the pointer is valid.

---

## Constructors

Do not perform world-dependent gameplay work in UObject or Actor constructors.

Constructors may execute for:

- the CDO;
- editor previews;
- asset loading;
- Blueprint compilation.

Use the appropriate lifecycle callback for runtime work.

---

## Initialization and shutdown must be symmetrical

If a system:

- binds a delegate;
- registers a callback;
- starts a timer;
- creates a resource;
- subscribes to an event;

it must have a clear corresponding cleanup path.

Avoid dangling bindings and callbacks during world teardown.

---

## Timers and delegates

Always consider whether the bound object may be destroyed before invocation.

Avoid unnecessary lambda captures of UObject pointers.

Remove or invalidate long-lived registrations when ownership ends.

---

# BLUEPRINT API DESIGN

Blueprint exposure is a public API.

Treat changes to Blueprint-facing properties and functions with the same care as changes to C++ public APIs.

---

## Blueprint function rules

Blueprint-callable functions should:

- have clear names;
- have clear categories;
- use understandable types;
- return useful success information;
- avoid hidden side effects.

Do not expose low-level implementation details when a higher-level operation would be safer.

---

## Pure functions

Mark functions pure only when they are conceptually queries.

A pure function should not:

- mutate meaningful state;
- perform expensive work unexpectedly;
- create objects;
- trigger gameplay behavior.

Blueprint may evaluate pure nodes more often than expected.

---

## Blueprint properties

Choose property specifiers intentionally.

Do not automatically make fields:

`EditAnywhere, BlueprintReadWrite`

Prefer the narrowest correct access.

Consider separately:

- who can edit the property;
- where it can be edited;
- who can read it;
- who can write it.

---

## Blueprint compatibility

Before renaming or removing reflected elements, consider existing assets.

Changes to:

- `UPROPERTY`;
- `UFUNCTION`;
- classes;
- structs;
- enums;

may affect serialized Blueprint assets.

Do not make reflected API changes casually.

---

# HEADER AND DEPENDENCY HYGIENE

Keep compile dependencies minimal and explicit.

---

## Forward declarations

Use forward declarations in headers where possible.

Include full headers when the complete type is actually required.

Do not forward declare types where Unreal reflection or template requirements need a complete definition.

---

## Public and private includes

Public headers must not unnecessarily leak implementation dependencies.

Keep private implementation dependencies in `.cpp` files whenever possible.

---

## Build.cs dependencies

When adding a dependency, determine whether it belongs in:

- `PublicDependencyModuleNames`;
- `PrivateDependencyModuleNames`.

Default to private when external consumers of the module do not need the dependency through the public API.

---

# PERFORMANCE AND PROFILING

Do not optimize blindly.

Do not ignore obvious hot-path costs.

Performance-sensitive code should be measurable using Unreal's profiling tools when doing so provides useful diagnostic value.

---

## Hot paths

Treat these as performance-sensitive unless proven otherwise:

- Tick;
- animation updates;
- physics callbacks;
- AI loops;
- repeated traces;
- rendering callbacks;
- per-frame Blueprint calls;
- large collection iteration.

Avoid unnecessary:

- allocations;
- world searches;
- casts;
- string construction;
- logging;
- temporary containers;

inside hot paths.

---

## Unreal Insights instrumentation

When a function, algorithm, or execution scope may have meaningful performance impact, make it measurable in Unreal Insights.

Use Unreal's built-in trace instrumentation before creating custom tracing systems.

For meaningful CPU scopes, consider instrumentation such as:

```cpp
TRACE_CPUPROFILER_EVENT_SCOPE(MyModule_MyOperation);
```

Use the appropriate Unreal profiling or tracing mechanism for the information being measured.

Relevant candidates include:

- expensive Tick functions;
- batch processing;
- large or nested loops;
- AI queries;
- complex target selection;
- expensive trace pipelines;
- async task execution;
- loading and initialization steps;
- serialization work;
- costly state updates;
- functions being actively optimized.

Do not instrument every function.

Do not add profiling scopes to trivial:

- getters;
- setters;
- simple wrappers;
- tiny functions whose isolated cost has no diagnostic value.

Too many low-value events make performance captures harder to read.

---

## Profiling scope naming

Profiling event names must be:

- stable;
- specific;
- easy to search;
- associated with the responsible system.

Prefer names that identify the module or system and operation.

Examples:

```cpp
TRACE_CPUPROFILER_EVENT_SCOPE(Interaction_FindBestTarget);
TRACE_CPUPROFILER_EVENT_SCOPE(Traversal_UpdateCandidates);
TRACE_CPUPROFILER_EVENT_SCOPE(MyModule_ProcessBatch);
```

Avoid vague names such as:

- `Update`;
- `Process`;
- `Work`.

Avoid creating high-cardinality event names dynamically from object names or runtime values.

Use counters, metadata, bookmarks, or other appropriate trace mechanisms when the diagnostic question is about quantities or state rather than elapsed scope time.

---

## Measure before and after optimization

When making a performance optimization:

1. identify the suspected cost;
2. make the relevant execution path measurable;
3. capture a representative workload;
4. record the baseline;
5. make the smallest optimization;
6. capture the same workload again;
7. compare results;
8. verify behavior did not change.

Do not claim a performance improvement based only on theoretical complexity or intuition when the code can reasonably be measured.

---

## Profiling overhead

Profiling code must not introduce meaningful unnecessary work of its own.

Do not:

- construct expensive strings every frame solely for profiling labels;
- gather detailed metadata when the relevant tracing path is disabled;
- add redundant nested scopes that provide no diagnostic value.

Instrumentation should help identify cost without becoming a significant source of cost or noise.

---

## Tick

Do not add Tick by default.

Before using Tick, consider:

- events;
- timers;
- delegates;
- state changes;
- latent actions;
- subsystem updates.

When Tick is required:

- keep it disabled when inactive;
- avoid expensive work every frame;
- consider update frequency;
- instrument expensive Tick paths when Unreal Insights visibility would help measure them.

---

## Caching

Cache expensive or repeated lookups when lifetime and invalidation rules are clear.

Do not cache references without understanding when they become stale.

---

## Optimization changes

Performance work must preserve behavior.

Do not trade correctness for an optimization without explicit justification and measurement.

---

# ASYNC AND THREAD SAFETY

Do not move work off the Game Thread merely because it is expensive.

Before using asynchronous execution, identify:

- which data is read;
- which data is written;
- who owns it;
- whether it is thread-safe;
- how cancellation works;
- how world teardown is handled;
- where completion returns.

Most UObject operations are not generally safe from arbitrary worker threads.

Do not access gameplay UObjects from background threads without a verified safe design.

Async callbacks must account for objects being destroyed before completion.

---

# ERROR HANDLING AND VALIDATION

Validate assumptions at the correct boundary.

---

## Caller errors

For recoverable caller mistakes:

- return failure;
- log when useful;
- preserve valid system state.

---

## Developer invariants

Use assertions for conditions that represent programming errors and should never occur in valid execution.

Choose appropriately between:

- `check`;
- `checkf`;
- `ensure`;
- `ensureMsgf`.

Do not use fatal assertions for normal runtime failure.

---

## Null checks

Do not add meaningless null checks everywhere.

A null check should answer a real design question:

- Is null valid?
- Is null recoverable?
- Does null indicate a programmer error?

Handle each case intentionally.

---

# STATE MANAGEMENT

State should have a single authoritative owner.

Avoid duplicate sources of truth.

When possible:

- centralize state transitions;
- validate transitions;
- expose state through queries;
- notify observers through events.

Do not allow unrelated callers to mutate critical state directly.

For complex systems, make invalid states difficult or impossible to represent.

---

# DOCUMENTATION

Documentation is a required deliverable for every module or plugin being worked on.

Documentation must be written in Markdown inside a `Docs` folder.

The documentation is for human users of the module or plugin, not for Codex.

---

## Documentation locations

For a project module:

```text
Source/MyModule/Docs/
```

For a plugin:

```text
Plugins/MyPlugin/Docs/
```

For a plugin module with substantial module-specific behavior:

```text
Plugins/MyPlugin/Source/MyModule/Docs/
```

Use plugin-level documentation for plugin-wide concepts and module-level documentation for module-specific behavior.

Avoid duplicating the same content in multiple locations.

---

## Required documentation purpose

Documentation must explain both:

1. how the module or plugin works;
2. how to use it correctly.

It should allow a developer or designer unfamiliar with the implementation to understand the system without reading all source files first.

---

## Recommended documentation structure

Use only the files that provide real value.

A module or plugin may include:

```text
Docs/
├── README.md
├── SETUP.md
├── USAGE.md
├── ARCHITECTURE.md
├── BLUEPRINT_API.md
├── CPP_API.md
├── DEBUGGING.md
├── EXTENDING.md
└── TROUBLESHOOTING.md
```

Do not create empty documentation files.

For small modules, one well-structured `README.md` may be sufficient.

For large systems, split documentation by topic.

---

## Minimum README content

The main documentation entry point should normally explain:

- purpose;
- core concepts;
- major classes or systems;
- setup;
- basic usage;
- configuration;
- common workflow;
- extension points;
- debugging options;
- important limitations.

Use practical examples where they improve understanding.

---

## Usage documentation

When the system is intended for designers, document the Blueprint workflow.

Where relevant explain:

- which Actor or Component to add;
- which properties must be configured;
- what happens at runtime;
- which events can be implemented;
- common mistakes.

When the system is intended for programmers, document the C++ integration path.

Where relevant explain:

- required module dependencies;
- headers to include;
- creation or initialization;
- main public APIs;
- ownership expectations;
- lifecycle requirements;
- extension points.

---

## Keep documentation synchronized

When implementation changes affect documented behavior, update the documentation in the same task.

Examples include changes to:

- setup;
- public APIs;
- Blueprint nodes;
- configuration;
- default values;
- lifecycle;
- extension points;
- debug controls;
- module dependencies;
- folder locations.

Outdated documentation is a bug.

---

## Do not document implementation noise

Documentation should not become a line-by-line translation of the source code.

Prioritize:

- concepts;
- contracts;
- workflows;
- usage;
- extension;
- troubleshooting.

Document internal implementation details only when they help a user understand constraints, debug problems, or safely extend the system.

---

## Documentation and `CODEX` must remain separate

Never use user documentation as a hidden instruction channel for Codex.

Never place AI workflow rules inside `Docs`.

Never place user tutorials inside `CODEX` unless they are directly required as implementation context.

The distinction is mandatory:

- `CODEX` = instructions for Codex.
- `Docs` = documentation for humans.

---

# COMMENTS AND DOCUMENTATION

Comments should explain why, not repeat what the code already says.

Public or Blueprint-facing Unreal declarations should be documented at the declaration site.

Use comments for:

- public classes, structs, delegates, reflected properties, and reflected functions;
- private helper declarations when their lifecycle, ownership, inputs, outputs, or failure behavior is not obvious;
- non-obvious runtime/editor constraints that affect how a caller should use the API.

Function comments should document inputs and outputs when useful:

- use `@param` for meaningful input parameters;
- use `@param Out...` for output parameters;
- use `@return` for success/failure, state, or ownership-relevant return values.

Blueprint-facing property comments should describe designer-facing intent, not implementation storage.

Good comments explain:

- non-obvious constraints;
- engine quirks;
- architectural decisions;
- reasons for unusual behavior;
- compatibility requirements.

Bad comments translate code into English.

Remove obsolete comments when behavior changes.

---

## TODO comments

A TODO must contain enough information to understand the missing work.

Avoid:

`// TODO Fix`

Prefer a description of:

- what is missing;
- why it is not implemented;
- what condition would allow completion.

Do not add TODOs as a substitute for completing the requested task.

---

# SOURCE CONTROL AND DIFF QUALITY

Keep diffs intentional.

Do not include:

- unrelated formatting;
- generated files;
- temporary debug code;
- dead code;
- commented-out implementations;
- accidental asset changes.

Before finishing, review the final diff as if reviewing another programmer's pull request.

Every changed line should have a reason.

---

# NEW MODULES AND PLUGINS

When creating a new module or plugin:

1. Define its responsibility clearly.
2. Define its public API boundary.
3. Minimize dependencies.
4. Create its standard `Public` and `Private` structure.
5. Organize files by coherent responsibility.
6. Separate runtime and editor code when needed.
7. Create its own log category.
8. Create its logging shortcut macros.
9. Define its global debug control when visual debugging is applicable.
10. Create a `CODEX` folder when the module requires domain-specific instructions.
11. Create a `Docs` folder with user-facing Markdown documentation.
12. Make performance-sensitive paths measurable in Unreal Insights where relevant.

The `CODEX` folder should contain focused Markdown documentation for Codex and AI-assisted work on that module.

Possible files include:

- `ARCHITECTURE.md`
- `RULES.md`
- `DEBUGGING.md`
- `BLUEPRINT_API.md`
- `LIFECYCLE.md`

Do not duplicate the root `AGENTS.md` inside local documentation.

Local `CODEX` documentation should contain only module-specific knowledge and rules.

The `Docs` folder should contain user-facing documentation explaining how the module or plugin works and how to use it.

---

# DEFINITION OF DONE

A task is complete only when all relevant points below are true.

- Relevant `CODEX` instructions were read.
- Relevant existing `Docs` were read.
- The current implementation was understood before modification.
- The root cause or requested behavior was identified.
- The smallest correct change was implemented.
- Module boundaries were respected.
- Folder structure rules were respected.
- Unreal ownership and lifecycle were considered.
- Blueprint compatibility was considered.
- No unrelated behavior was changed.
- Debug code follows local and global enable/disable rules.
- Logs use the module's log category and macros.
- No permanent `LogTemp` usage was introduced.
- No unnecessary Tick or hot-path cost was introduced.
- Performance-sensitive code is measurable in Unreal Insights where relevant.
- Performance claims were measured when reasonable.
- Required cleanup paths exist.
- The affected target compiles successfully.
- Relevant runtime behavior was validated where possible.
- Failure paths were considered.
- The relevant `Docs` folder exists.
- User-facing documentation was added or updated where needed.
- Documentation matches current behavior and usage.
- The final diff contains no unrelated changes.
- Temporary debug code and commented-out code were removed.

If the affected code does not compile, the task is not done.
