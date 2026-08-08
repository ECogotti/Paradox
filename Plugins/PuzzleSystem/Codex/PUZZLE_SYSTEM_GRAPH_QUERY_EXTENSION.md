# PuzzleSystem Graph Query Extension — Codex Specification

## Scopo

Estendere il plugin runtime generico `PuzzleSystem` con un layer di **query read-only del grafo puzzle**.

Questo task implementa solamente la query/topologia.

Non implementare:

- rendering;
- ParadoxGameplay;
- GridWorld;
- spline;
- mesh;
- Custom Depth;
- materiali;
- routing;
- surface detection;
- anti-crossing;
- GOAP;
- IntentReplay;
- WorldState;
- PCG.

La feature deve permettere a sistemi esterni di chiedere:

```text
Quali connessioni entrano in questo Actor?
Quali connessioni escono da questo Actor?
Quale Emitter alimenta questo Receiver?
Quali Emitter agiscono da gate per questo specifico input?
Attraverso quale Controller/InputBinding esiste la relazione?
Qual è lo stato raw del segnale?
Il gate è bypassato, aperto, chiuso o invalido?
Qual è lo stato effettivo del primary input?
```

La feature deve essere generica e riutilizzabile anche fuori da Paradox.

---

# 1. Workflow obbligatorio

Prima di modificare il plugin Codex deve:

1. leggere `AGENTS.md`;
2. individuare il modulo reale di `PuzzleSystem`;
3. leggere tutti i `CODEX` rilevanti;
4. leggere i `Docs` del plugin;
5. ispezionare l'implementazione reale di:
   - `UPuzzleEmitterComponent`;
   - `APuzzleController`;
   - `FPuzzleInputBinding`;
   - gate binding attuale;
   - `EmitterGates`;
   - `GateConditions`;
   - cache raw primary;
   - cache gate-local;
   - effective primary state/revision;
   - `UPuzzleReceiverComponent`;
   - Receiver bindings;
   - lifecycle del Controller;
   - reentrancy;
   - delegati runtime;
   - debug;
   - test;
6. leggere la specifica esistente dei Controller Input Gates;
7. compilare il target prima della modifica;
8. non inventare API Unreal;
9. aggiornare i `Docs`;
10. ricompilare fino a build riuscita.

I nomi introdotti in questo documento sono concettuali finché non esistono già nel codice.

---

# 2. Architettura autorevole invariata

Il flusso gameplay rimane:

```text
UPuzzleEmitterComponent
        ↓
APuzzleController
        ↓
UPuzzleReceiverComponent
```

Il Graph Query layer è solo:

```text
observer
index
read-only view
```

Non deve:

- ricevere segnali al posto dei Controller;
- valutare condizioni;
- attivare Receiver;
- diventare obbligatorio per il funzionamento dei puzzle;
- introdurre un quarto ruolo gameplay.

I ruoli fondamentali restano:

```text
Emitter
Controller
Receiver
```

---

# 3. Semantica dei gate da preservare

Ogni primary `FPuzzleInputBinding` può avere:

```text
EmitterGates[]
GateConditions[]
```

Il gate è locale a:

```text
uno specifico InputBinding
di uno specifico APuzzleController
```

Il gate non modifica il source Emitter.

Semantica:

```text
RawPrimaryActive
        +
Gate result
        ↓
EffectivePrimaryActive
```

Quindi lo stesso Emitter può essere:

```text
ammesso dal Controller A
bloccato dal Controller B
```

contemporaneamente.

Il sistema di query deve preservare questo contesto.

Non creare uno stato globale tipo:

```text
Emitter A OutputActive
```

perché sarebbe semanticamente errato.

---

# 4. Perché il Graph Query appartiene a PuzzleSystem

Le informazioni necessarie sono già proprietà del plugin:

- primary bindings;
- Emitter bindings;
- gate bindings;
- gate conditions;
- Receiver bindings;
- raw state;
- gate state;
- effective state;
- Controller result.

I consumer esterni non devono ricostruire autonomamente il wiring leggendo internals dei Controller.

Il plugin deve esporre un contratto read-only stabile.

---

# 5. Tipi di relazione

Introdurre almeno due link semantici.

Working enum:

```text
EPuzzleGraphLinkKind
```

Valori iniziali:

```text
PrimarySignal
GateInfluence
```

Non aggiungere tipi speculativi.

---

## 5.1 PrimarySignal

Rappresenta:

```text
Primary Emitter
        ↓
Controller Primary InputBinding
        ↓
Receiver
```

Contesto minimo:

```text
SourceEmitterComponent
Controller
PrimaryInputId
PrimarySignalTag
TargetReceiverComponent
```

Se lo stesso input controlla più Receiver:

```text
Receiver A
Receiver B
Receiver C
```

esporre un `PrimarySignal` per ogni Receiver endpoint.

Lo stato primary può essere condiviso internamente, ma i link devono distinguere il target.

---

## 5.2 GateInfluence

Rappresenta:

```text
Gate Emitter
        ↓
GateInputId
        ↓
GateConditions
        ↓
ammissione di uno specifico Primary InputBinding
```

Per consumer Actor-centrici è lecito interpretarlo geometricamente come:

```text
Gate Emitter -> Primary Emitter
```

ma il descriptor deve mantenere:

```text
LinkKind = GateInfluence
```

perché il gate non modifica né ripubblica il primary Emitter.

Non trasformare questa relazione visuale in un nuovo flusso gameplay.

---

# 6. Link contestuale, non semplice Actor->Actor

Non basta memorizzare:

```text
Actor A -> Actor B
```

Ogni relazione deve conservare il contesto che la rende unica:

```text
Controller
PrimaryInputId
PrimaryEmitterComponent
PrimarySignalTag
TargetReceiverComponent
GateInputId, se gate
GateEmitterComponent, se gate
GateSignalTag, se gate
```

Due link con gli stessi Actor endpoint ma Controller diversi sono link distinti.

Due link dello stesso Emitter verso Receiver diversi sono link distinti.

Due InputBinding dello stesso Controller verso lo stesso Receiver sono link distinti.

---

# 7. Opaque runtime handle

Ogni relazione runtime deve avere un'identità opaca.

Working type:

```text
FPuzzleGraphLinkHandle
```

Serve per:

- recuperare lo stato corrente;
- correlare eventi;
- confrontare snapshot;
- distinguere link con stessi endpoint.

Il consumer non deve costruire manualmente il handle.

Internamente può derivare da:

```text
Controller
Primary Binding identity/index
Receiver Binding identity/index
Gate Binding identity/index
GraphTopologyRevision
```

ma non esporre un semplice array index come contratto stabile se può diventare stale.

Gli handle devono essere validati dopo topology changes.

Non creare persistent IDs cross-save in questo task.

---

# 8. Link descriptor read-only

Working struct:

```text
FPuzzleGraphLink
```

Minimo contenuto concettuale:

```text
LinkHandle
LinkKind

Controller

PrimaryInputId
PrimarySignalTag

PrimaryEmitterActor
PrimaryEmitterComponent

TargetReceiverActor
TargetReceiverComponent

GateInputId
GateSignalTag
GateEmitterActor
GateEmitterComponent
```

Campi non pertinenti al tipo di link possono essere null/invalid secondo convenzione chiara.

Utilizzare riferimenti Unreal-safe.

Non esporre:

- binding mutabili;
- cache mutabili;
- delegate handles;
- mappe interne.

---

# 9. Topologia e stato separati

Separare:

```text
FPuzzleGraphLink
= cosa è collegato a cosa

FPuzzleGraphLinkState
= cosa sta succedendo ora
```

Questo è obbligatorio.

Il futuro renderer deve poter ricostruire la geometria solo quando cambia la topologia, ma cambiare luminosità quando cambia lo stato.

---

# 10. Stato primary da esporre

Per ogni primary context devono essere queryable almeno:

```text
RawPrimaryValid
RawPrimaryActive
RawPrimaryRevision

GateMode
GateValid
GateAllowsSignal

EffectivePrimaryValid
EffectivePrimaryActive
EffectiveRevision
```

`GateMode` o equivalente deve distinguere chiaramente:

```text
Bypassed
Open
Closed
Invalid
```

Non usare un singolo bool ambiguo.

---

# 11. Bypass gate

Preservare esattamente l'attuale truth table:

```text
EmitterGates empty
GateConditions empty
    -> Bypassed

EmitterGates populated
GateConditions empty
    -> Bypassed

EmitterGates empty
GateConditions populated
    -> Bypassed

entrambi populated
    -> evaluate
```

Un array non accoppiato non deve rendere invalido il primary signal.

---

# 12. Raw vs Effective

Caso fondamentale:

```text
RawPrimaryActive = true
GateValid = true
GateAllowsSignal = false
```

Il Graph State deve restituire:

```text
RawPrimaryActive = true
EffectivePrimaryActive = false
GateMode = Closed
```

Non esporre soltanto:

```text
bActive = false
```

perdendo l'informazione che il source Emitter sta realmente pubblicando.

---

# 13. GateInfluence state

Ogni gate link deve esporre almeno:

```text
GateInputValid
GateInputActive
GateInputRevision
```

e il contesto aggregato dell'input che controlla:

```text
OwningGateMode
OwningGateValid
OwningGateAllowsSignal

OwningEffectivePrimaryValid
OwningEffectivePrimaryActive
OwningEffectiveRevision
```

Questo è necessario perché:

```text
GateInputActive = true
```

non significa necessariamente:

```text
GateConditions = true
```

Esempi:

- `Not`;
- `Any`;
- `Threshold`;
- custom payload condition;
- più top-level GateConditions.

Non confondere individual gate state con aggregate gate result.

---

# 14. Controller result e Receiver state

Dove possibile esporre anche:

```text
ControllerResult
TargetReceiverEffectiveActive
```

come dati read-only contestuali.

Devono restare distinti da:

```text
EffectivePrimaryActive
```

Esempio:

```text
Input A effective = true
Input B effective = false
RootCondition = All
ControllerResult = false
```

Il link di A è effettivamente attivo, ma il Controller non è attivo.

Altro esempio:

```text
Controller A request false
Controller B request true
ReceiverEffectiveActive = true
```

Il Receiver può essere attivo per aggregazione di un altro Controller.

Non inferire una cosa dall'altra.

Se Receiver effective state richiede integrazione invasiva, può essere snapshot on-demand e non fonte primaria di `LinkStateChanged`.

Il Receiver endpoint espone inoltre la propria modalità `Automatic`/`Manual`, i prerequisiti aggregati,
il latch manuale e lo stato effettivo. Questi campi descrivono la politica del Receiver e non spostano
nel graph alcuna autorità di mutazione. I comandi manuali già riconciliati possono notificare lo stato
dei link del Receiver senza modificare la topology revision.

---

# 15. Payload

Non duplicare payload nel grafo.

Se utile esporre read-only:

```text
Payload pointer
Payload class
Revision
```

ma continuare a usare:

```text
UPuzzleSignalPayload
```

Non introdurre:

```text
BoolValue
FloatValue
IntValue
stringified payload
```

---

# 16. Actor-centric queries

Il sistema deve supportare query per Actor senza perdere component identity.

Working result:

```text
FPuzzleActorGraphView
```

Gruppi concettuali:

```text
IncomingPrimaryLinks
IncomingGateLinks
OutgoingPrimaryLinks
```

---

## 16.1 Actor con Receiver

Per ogni `UPuzzleReceiverComponent` posseduto:

```text
Input = PrimarySignal links
        che terminano su quei Receiver
```

Quindi:

```text
Emitter Actor -> Queried Actor
```

con descriptor completo di:

```text
Emitter component
Controller
PrimaryInputId
SignalTag
Receiver component
```

---

## 16.2 Actor con Emitter

Per ogni `UPuzzleEmitterComponent` posseduto:

### Incoming gate

Trovare gli input Controller dove quel componente è il primary Emitter.

Restituire:

```text
Gate Emitter -> Queried Emitter
```

come `GateInfluence`.

### Outgoing

Per ogni Controller primary input che usa quel componente:

```text
Queried Emitter -> Receiver target
```

come `PrimarySignal`.

---

## 16.3 Actor con Emitter + Receiver

Restituire l'unione:

```text
IncomingPrimaryLinks
IncomingGateLinks
OutgoingPrimaryLinks
```

Non assumere che Emitter e Receiver siano ruoli esclusivi.

---

# 17. Multiple components

PuzzleSystem supporta più Emitter/Receiver sullo stesso Actor.

Il Graph Query deve conservare il componente risolto esatto.

Esempio:

```text
RuneMechanism
├── RotationEmitter
└── PowerEmitter
```

Se il binding esplicito usa `PowerEmitter`, il graph link deve usare `PowerEmitter`.

Non fare fallback al primo component.

Stessa regola per Receiver multipli.

---

# 18. Same Emitter, different Controller state

Caso obbligatorio:

```text
Emitter A raw active = true

Controller 1
Gate open
Receiver X
Effective = true

Controller 2
Gate closed
Receiver Y
Effective = false
```

Query dell'Actor di A:

```text
OutgoingPrimaryLinks = 2
```

Stati:

```text
A -> X : EffectivePrimaryActive = true
A -> Y : EffectivePrimaryActive = false
```

Se questo scenario non è rappresentabile, l'architettura è sbagliata.

---

# 19. Multiple Receivers

Un Controller può puntare più Receiver.

Il graph deve esporre un link per endpoint:

```text
Primary Emitter
├── Receiver A
├── Receiver B
└── Receiver C
```

Non obbligare il consumer a rileggere `ReceiverBindings`.

---

# 20. Multiple primary inputs

Un Controller può avere più primary input.

Esempio:

```text
Input A active
Input B inactive
Root = All
Receiver R
```

Restituire due link:

```text
A -> R
B -> R
```

con:

```text
A EffectivePrimaryActive = true
B EffectivePrimaryActive = false
ControllerResult = false
```

Non rappresentare il Controller come se ogni input attivasse il Receiver individualmente.

---

# 21. Gate conditions non diventano nodi grafici

Nel primo task non creare AST o nodi grafici per ogni `UPuzzleCondition`.

Il grafo espone:

```text
GateInfluence
aggregate gate result
```

e, se già disponibile senza redesign:

```text
diagnostic condition result
```

Non costruire un full condition-expression graph.

---

# 22. World-level graph service

Per query Actor-centriche efficienti serve un indice world-level.

Se non esiste già una struttura adatta, preferire:

```text
UPuzzleGraphSubsystem : UWorldSubsystem
```

o equivalente coerente col plugin.

Questa è una feature concreta di global read-only introspection, quindi il subsystem è ammesso.

Ma non deve diventare signal router.

---

# 23. Graph subsystem non è gameplay routing

Può:

```text
register Controllers
index topology
answer queries
publish graph observation events
cache lightweight metadata
```

Non può:

```text
ricevere signal al posto del Controller
valutare RootCondition
valutare GateConditions una seconda volta
attivare Receiver
diventare necessario al normale puzzle flow
```

Il gameplay deve funzionare anche con zero graph consumer.

---

# 24. Controller registration

Non usare:

```text
GetAllActorsOfClass<APuzzleController>
```

a ogni query.

I Controller devono registrarsi/unregistrarsi durante lifecycle controllato.

Concept:

```text
Controller runtime bindings resolved
    -> Register/Refresh topology

Controller EndPlay
    -> Unregister
```

La registrazione non deve alterare evaluation order.

Non dipendere da BeginPlay ordering arbitrario.

---

# 25. Indici runtime

Mantenere indici leggeri per query efficienti.

Concettualmente:

```text
Controller -> links

PrimaryEmitterComponent -> PrimarySignal links
PrimaryEmitterComponent -> gate relationships affecting that primary

GateEmitterComponent -> GateInfluence links

ReceiverComponent -> incoming PrimarySignal links

Actor -> relevant links
```

La struttura esatta è privata.

`QueryActorGraph` deve essere proporzionale principalmente ai link dell'Actor, non al numero totale di Controller nel world.

---

# 26. GraphTopologyRevision

Mantenere:

```text
GraphTopologyRevision
```

monotonicamente crescente.

Incrementa per cambiamenti strutturali, ad esempio:

```text
Controller registered
Controller unregistered
binding topology refreshed
endpoint resolved/unresolved quando trattato come topology
```

Non incrementare per:

```text
raw signal state change
gate open/close
effective state change
ControllerResult change
```

Quelli sono state changes.

---

# 27. API pubblica

Working operations:

```text
QueryActorGraph(Actor)
QueryIncomingLinksForActor(Actor)
QueryOutgoingLinksForActor(Actor)

QueryLinksForEmitterComponent(Component)
QueryLinksForReceiverComponent(Component)

TryGetLink(Handle, OutLink)
TryGetLinkState(Handle, OutState)

GetGraphTopologyRevision()
```

Nomi esatti coerenti col plugin.

Preferire un'API piccola e composabile.

Non duplicare decine di wrapper equivalenti.

---

# 28. Query results sono snapshot

Preferire value snapshots:

```text
FPuzzleGraphLink
FPuzzleGraphLinkState
FPuzzleActorGraphView
```

contenenti riferimenti read-only Unreal-safe.

Il handle è runtime/opaque.

Non restituire reference dirette a `TArray`/`TMap` interni che possono invalidarsi.

Dopo topology revision change, un consumer deve poter rivalidare il handle.

---

# 29. Deterministic ordering

Le query devono restituire ordine deterministico.

Non usare ordine casuale di `TMap`.

Possibile sort:

```text
Controller deterministic order
PrimaryBinding order
GateBinding order
ReceiverBinding order
LinkKind
```

Definire e documentare la regola effettiva.

Non usare pointer address come semantic sort.

---

# 30. Runtime events

Il sistema deve essere event-driven.

Aggiungere due concetti distinti:

```text
OnPuzzleGraphTopologyChanged
OnPuzzleGraphLinkStateChanged
```

Non usare Tick.

---

# 31. TopologyChanged

Usato quando la struttura cambia.

Payload utile:

```text
GraphTopologyRevision
affected Controller
affected Actors/components
change kind
```

Working change kind:

```text
Added
Removed
Refreshed
Invalidated
```

solo se realmente utile.

Consumer contract:

```text
TopologyChanged
    -> re-query / rebuild structure
```

---

# 32. LinkStateChanged

Usato quando stessa relazione cambia stato.

Payload deve identificare:

```text
LinkHandle
new state
```

eventualmente previous state.

Consumer contract:

```text
LinkStateChanged
    -> topology unchanged
    -> update state only
```

Questo contratto è fondamentale per il futuro renderer.

---

# 33. State changes significativi

Notificare quando cambia qualcosa di query-visible, ad esempio:

```text
RawPrimaryValid
RawPrimaryActive
RawPrimaryRevision

GateMode
GateValid
GateAllowsSignal

EffectivePrimaryValid
EffectivePrimaryActive
EffectiveRevision

GateInputValid
GateInputActive
GateInputRevision
```

Evitare duplicate notification se lo stato pubblico non cambia.

---

# 34. Gate closes without primary republish

Caso obbligatorio:

```text
Primary raw remains active

Gate:
true -> false
```

Expected:

```text
PrimarySignal EffectivePrimaryActive
true -> false

GateInfluence state updates

LinkStateChanged events
Topology unchanged
GraphTopologyRevision unchanged
```

Il primary Emitter non deve repubblicare.

---

# 35. Broadcast dopo stato coerente

Non inviare graph event mentre il Controller è a metà reevaluation.

Preferred:

```text
Emitter/gate update
    ↓
Controller cache update
    ↓
gate/effective calculation
    ↓
RootCondition settles
    ↓
reentrancy settles
    ↓
Graph state snapshot coherent
    ↓
Graph notification
```

Non esporre stati intermedi incoerenti.

---

# 36. Reentrancy

Il Controller mantiene il suo attuale reentrancy guard.

Il Graph Query non deve:

- forzare nested evaluation;
- cambiare scope condition;
- chiamare Receiver;
- creare global queue Tick-based.

Se più update sincroni collassano nello stato finale, preferire notification coerente con lo stato finale.

---

# 37. Endpoint destruction

Invalid e inactive sono concetti diversi.

Se Emitter/Receiver viene distrutto:

- non riportarlo come semplice inactive;
- rimuovere o invalidare la relazione secondo policy coerente col runtime Controller;
- notificare state/topology change appropriato;
- stale handle deve fallire in modo prevedibile.

Codex deve scegliere policy:

```text
remove topology
```

oppure:

```text
retain invalid link
```

in base all'implementazione attuale.

La policy deve essere documentata e deterministica.

---

# 38. Controller destruction

`APuzzleController::EndPlay`:

```text
Unregister graph links
Invalidate handles
Increment topology revision
Notify topology change
```

Nessun cleanup polling.

---

# 39. Late query

Un consumer può iniziare dopo l'inizializzazione dei Controller.

La query deve restituire immediatamente:

```text
topologia corrente
stato corrente
```

Il consumer non deve aver ascoltato gli eventi iniziali.

---

# 40. Runtime rewiring

Non inventare un sistema runtime di rewiring se non esiste già.

Se i binding sono statici dopo initialization:

- register initial topology;
- handle endpoint destruction;
- handle Controller destruction.

Se esiste già un API controllata di refresh binding, integra il graph refresh lì.

Non osservare reflected arrays ogni frame.

---

# 41. Blueprint API

Esporre Blueprint read-only dove pratico:

```text
ActorGraphView
GraphLink
GraphLinkState
GraphLinkKind
GraphTopologyRevision
query functions
delegates
```

Non esporre Blueprint mutation per:

```text
register
unregister
cache edit
topology edit
```

---

# 42. C++ API

Supportare query efficienti:

```text
Actor
EmitterComponent
ReceiverComponent
LinkHandle
```

senza aprire le private cache dei Controller.

Preferire const/value snapshot.

---

# 43. Controller read-only state API

Prima di aggiungere funzioni, controllare cosa esiste già.

Riutilizzare semantiche tipo:

```text
TryGetRawInputState
TryGetEffectiveInputState
IsInputGateBypassed
IsInputGateValid
DoesInputGateAllowSignal
TryGetGateInputState
```

I nomi sono concettuali.

Aggiungere solo il minimo necessario.

Mai esporre l'intera runtime cache mutabile.

---

# 44. State cache nel graph service

Il Controller resta authoritative.

Sono accettabili due implementazioni:

### A

```text
Graph stores topology
State query asks controlled Controller API
```

### B

```text
Graph stores lightweight state snapshot
updated after Controller settles
```

Scegliere quella più piccola e coerente col codice reale.

Mai usare la graph cache per gameplay evaluation.

---

# 45. Performance

La feature deve costare quasi zero se non usata.

Vietato:

```text
Tick
polling
GetAllActorsOfClass per query
full graph rebuild per signal change
large temp allocation per event
runtime debug string formatting sempre attivo
```

Preferred:

```text
Controller registration
    -> update only that Controller topology

signal/gate event
    -> update affected link state only

Actor query
    -> indexed lookup
```

---

# 46. Logging

Usare:

```text
LogPuzzleSystem
```

e macro esistenti.

No `LogTemp`.

Loggare warning/error solo per problemi reali:

```text
failed topology registration
stale handle
duplicate internal identity
resolved binding mismatch
```

Non loggare ogni query normale.

---

# 47. Debug

Non creare un secondo renderer.

Il debug graph può essere minimale.

Dati utili:

```text
RegisteredControllers
LinkCount
GraphTopologyRevision
LinkKind
Controller/InputId
Raw/Gate/Effective state
```

Il debug visuale Controller già esistente resta invariato.

---

# 48. Unreal Insights

Solo se utile dopo profiling.

Candidate scopes:

```text
PuzzleSystem_Graph_RegisterController
PuzzleSystem_Graph_QueryActor
PuzzleSystem_Graph_RebuildControllerLinks
```

Non strumentare getter triviali.

---

# 49. Folder structure suggerita

Seguire prima la struttura esistente.

Se non esiste pattern migliore:

```text
Public/
└── Graph/
    ├── PuzzleGraphTypes.h
    └── PuzzleGraphSubsystem.h

Private/
└── Graph/
    ├── PuzzleGraphSubsystem.cpp
    └── Tests/
        └── PuzzleGraphTests.cpp
```

Modificare i Controller solo per integrazione minima.

Non riorganizzare file non correlati.

---

# 50. Test obbligatori

## Test 1 — Single link

```text
Emitter A
    -> Controller Main
    -> Receiver B
```

Query A:

```text
one outgoing PrimarySignal
```

Query B:

```text
one incoming PrimarySignal
```

---

## Test 2 — Descriptor corretto

Verificare:

```text
Emitter Actor/component
Controller
PrimaryInputId
SignalTag
Receiver Actor/component
```

---

## Test 3 — No gate

```text
Raw = true
Gate = Bypassed
Effective = true
```

---

## Test 4 — Closed gate

```text
Raw = true
Gate = Closed
Effective = false
```

---

## Test 5 — GateInfluence

```text
Gate G -> Primary A
```

Query A deve restituire incoming GateInfluence con:

```text
GateInputId
GateSignalTag
GateEmitter
Controller
PrimaryInputId
PrimaryEmitter
```

---

## Test 6 — Gate raw update

```text
Gate false -> true
```

Expected:

```text
GateInfluence state changes
LinkStateChanged
no TopologyChanged
```

---

## Test 7 — Gate closes without primary republish

Expected:

```text
PrimarySignal Effective true -> false
GateInfluence updated
state events
same topology revision
```

---

## Test 8 — Same Emitter / two Controllers

```text
A raw = true

Controller 1 gate open -> X
Controller 2 gate closed -> Y
```

Query A:

```text
2 outgoing links

A->X Effective = true
A->Y Effective = false
```

---

## Test 9 — Multiple Receivers

One input / three Receiver.

Expected:

```text
3 PrimarySignal links
```

---

## Test 10 — Multiple primary inputs

```text
Input A effective true
Input B effective false
Root All
Receiver R
```

Expected:

```text
A->R true
B->R false
ControllerResult false
```

---

## Test 11 — Actor Receiver + Emitter

Expected actor view may contain:

```text
IncomingPrimary
IncomingGate
OutgoingPrimary
```

---

## Test 12 — Multiple Emitter components

Explicit component binding must preserve exact component identity.

---

## Test 13 — Multiple Receiver components

Explicit component binding must preserve exact component identity.

---

## Test 14 — Same source used primary + gate

Entrambe le relazioni devono esistere.

Nessuna dedup semantica deve rimuoverne una.

---

## Test 15 — Gate bypass one-array-only

Entrambe le configurazioni:

```text
gates only
conditions only
```

devono restituire:

```text
GateMode = Bypassed
```

Se si espongono gate metadata configurati ma bypassed, devono essere chiaramente marcati come non attivi.

---

## Test 16 — Invalid gate

Expected:

```text
GateMode = Invalid
EffectivePrimaryValid = false
```

Non semplice inactive.

---

## Test 17 — Primary Emitter destruction

Handle stale/invalid safe.

Event appropriato.

---

## Test 18 — Gate Emitter destruction

Graph state deve seguire il fail-closed del Controller.

---

## Test 19 — Receiver destruction

Nessun valid stale endpoint.

---

## Test 20 — Controller destruction

Expected:

```text
all Controller links removed
TopologyRevision++
TopologyChanged
```

---

## Test 21 — Late consumer

Query dopo startup restituisce stato corrente completo.

---

## Test 22 — Deterministic ordering

Query identica ripetuta -> stesso ordine.

---

## Test 23 — No graph consumer

Normal puzzle gameplay invariato.

Questo test è obbligatorio.

---

# 51. Docs

Aggiornare i Docs del plugin spiegando:

- scopo Graph Query;
- cosa non fa;
- PrimarySignal;
- GateInfluence;
- context per Controller/InputId;
- raw vs effective;
- gate bypass/open/closed/invalid;
- Actor queries;
- component queries;
- topology revision;
- TopologyChanged;
- LinkStateChanged;
- lifecycle degli handle;
- Blueprint usage;
- C++ usage;
- performance.

Inserire l'esempio:

```text
stesso Emitter
due Controller
gate diversi
due EffectivePrimaryActive diversi
```

Non mettere istruzioni Codex nei Docs utente.

---

# 52. Shortcut vietati

Non:

```text
route signal via graph subsystem
evaluate RootCondition nel graph subsystem
evaluate GateConditions due volte
activate Receiver dal graph
modify Emitter state
flatten Controller-local state
create global Emitter output state
treat closed as invalid
treat invalid as inactive
lose component identity
deduplicate different Controller relationships
world-scan every query
Tick
poll
expose mutable caches
depend on GridWorld
depend on ParadoxGameplay
depend on rendering
depend on navigation
depend on PCG
depend on IntentReplay
depend on WorldState
use LogTemp
break old serialized Controllers
change gate bypass semantics
change Receiver aggregation
change Emitter->Controller->Receiver flow
```

---

# 53. Milestone di implementazione

## Milestone 1 — Investigation + types

Definire:

```text
EPuzzleGraphLinkKind
FPuzzleGraphLinkHandle
FPuzzleGraphLink
FPuzzleGraphLinkState
FPuzzleActorGraphView
topology revision contract
```

Compilare.

---

## Milestone 2 — Registry/index

Implementare:

```text
world graph service
Controller registration
Controller unregister
per-Controller link build
Emitter/Receiver/Actor indices
deterministic ordering
GraphTopologyRevision
```

Compilare e testare topology queries.

---

## Milestone 3 — Runtime state

Collegare API read-only Controller per:

```text
raw
gate
effective
gate-local inputs
ControllerResult
Receiver snapshot, se non invasivo
```

Non duplicare evaluation logic.

Compilare e testare.

---

## Milestone 4 — Events

Implementare:

```text
LinkStateChanged
TopologyChanged
```

Rispettare reentrancy.

Verificare gate-only changes.

Compilare.

---

## Milestone 5 — Blueprint/C++ API

Esporre query e snapshot minimi.

Validare Blueprint.

Compilare.

---

## Milestone 6 — Lifecycle + tests

Validare:

```text
Emitter destruction
Gate destruction
Receiver destruction
Controller destruction
world teardown
late query
stale handle
multi-component
Emitter+Receiver Actor
```

Compilare.

---

## Milestone 7 — Docs + review

Aggiornare Docs.

Review:

```text
public API
Blueprint API
dependencies
delegate cleanup
performance
test coverage
diff
```

Build finale.

---

# 54. Definition of Done

La feature è completa solo se:

- `PuzzleSystem` espone graph query read-only;
- il gameplay flow originale resta autorevole;
- il graph service non route-a segnali;
- non serve world scan per ogni query;
- `PrimarySignal` conserva Controller/InputId;
- `GateInfluence` conserva gate e primary context;
- exact component identity è preservata;
- actor-centric queries funzionano;
- Actor con Emitter+Receiver funziona;
- same endpoint Actors attraverso Controller diversi restano link distinti;
- multiple Receiver endpoints restano distinti;
- raw primary e effective primary sono separati;
- bypass/open/closed/invalid sono distinguibili;
- gate input state e aggregate gate result sono separati;
- stesso Emitter può avere output effettivi diversi per Controller diversi;
- gate changes aggiornano link senza primary republish;
- topology events e state events sono separati;
- query order è deterministico;
- stale handle fallisce in modo sicuro;
- non viene aggiunto Tick;
- nessuna dipendenza da GridWorld/Paradox/rendering/navigation/PCG/IntentReplay/WorldState;
- il puzzle gameplay funziona anche senza consumer;
- automated tests passano;
- Docs aggiornati;
- target compilato;
- diff senza modifiche non correlate.

Se il target non compila, il task non è finito.
