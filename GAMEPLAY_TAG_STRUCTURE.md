# Struttura dei Gameplay Tag di Paradox

## Obiettivo

La tassonomia usa pochi root semantici e separa il **ruolo del dato** dal sistema che lo produce.
Un tag deve dire prima che cosa rappresenta (`Type`, `Result`, `State`, `Event`, ecc.) e solo dopo
chi ne possiede la semantica (`Paradox`, `GridWorld`, `IntentReplay`, ecc.).

La forma generale è:

```text
<Root>.<Role>.<Owner o Domain>.<Concept>.<Variant>
```

I segmenti non necessari si omettono. Per esempio, `GameplayAction.Lock.Movement` è un lock
riutilizzabile e non ha owner; `GameplayAction.Type.Paradox.TimeLoop.TimeTravel` è invece un tipo di
azione specifico del gioco.

## Root ammessi

| Root | Responsabilità | Rami principali |
| --- | --- | --- |
| `GameplayAction` | Identità, lock, origine e risultato delle Gameplay Action | `Type`, `Lock`, `Origin`, `Result`, `Test` |
| `IntentReplay` | Metadati e fallimenti propri di registrazione e playback | `Correlation`, `Failure`, `Test` |
| `Relation` | Domini, esiti e motivazioni delle relazioni tra entità | `Domain`, `Outcome`, `Reason`, `Test` |
| `PerceptionKnowledge` | Sensi e semantica delle osservazioni | `Sense`, `State`, `Event`, `Cause`, `Test` |
| `Puzzle` | Vocabolario dei segnali e dei circuiti puzzle | `Signal`, `Test` |
| `Interaction` | Identità delle opzioni/comandi di interazione | owner o dominio, `Test` |

Non si devono introdurre root per un singolo oggetto di gioco. I precedenti `Barrier`, `Computer` e
`Paradox` sono stati rimossi come root: sono concetti o owner all'interno di un ramo semantico.

## Struttura canonica corrente

```text
GameplayAction
|-- Type
|   |-- GridWorld.MoveToGridCell
|   `-- Paradox
|       |-- Character.SetCrouched
|       |-- TimeLoop.TimeTravel
|       |-- Investigation.Inspect
|       `-- Interaction
|           |-- Receiver.SetState
|           `-- Emitter.SetSignal
|-- Lock
|   |-- Primary
|   |-- Movement
|   |-- Stance
|   |-- Interaction
|   `-- Paradox.TimeTravel
|-- Origin
|   |-- Player
|   |-- Replay
|   `-- Paradox.Investigation
|-- Result
|   |-- Success
|   |-- Failure
|   |   |-- Unspecified / InvalidRequest / CannotStart / JournalRejected / QueueTimeout
|   |   |-- GridWorld.MoveToGridCell.*
|   |   `-- Paradox.Interaction.*
|   |-- Cancelled
|   |   |-- ByRequester
|   |   `-- IntentReplay.PlaybackStopped
|   |-- Interrupted
|   |   |-- External / HigherPriority
|   |   `-- Paradox.Investigation.* / Paradox.Barrier.Transport
|   `-- Aborted
|       `-- OwnerEndPlay / SystemReset / ComponentDeactivated
`-- Test

IntentReplay
|-- Correlation.RecordedIntent
|-- Failure.*
`-- Test

Relation
|-- Domain.*
|-- Outcome.Paradox.FutureObserved
|-- Reason.Paradox.FutureTemporalOrder
|-- Reason.Paradox.SafeTemporalOrder
`-- Test

PerceptionKnowledge
|-- Sense.Sight / Sense.Hearing
|-- State
|   `-- Paradox.Computer.* / Paradox.Barrier.*
|-- Event
|   `-- Paradox.Noise.Character.* / PressurePlate.* / Barrier.*
|-- Cause
|   `-- Paradox.CharacterMovement.* / PressurePlate.* / Barrier.*
`-- Test

Puzzle
|-- Signal.Pressed
`-- Test

Interaction
|-- Paradox.Barrier.Open
|-- Paradox.Barrier.Close
`-- Test
```

## Regole per i tag di test

Ogni tag usato esclusivamente da fixture o Automation Test deve contenere il segmento esatto
`Test`. Il testo successivo deve descrivere lo scenario; non è richiesto un indice numerico.

Quando il ruolo è importante per il test, `Test` può comparire dopo il ruolo, per esempio
`GameplayAction.Lock.Test.Conflict.Primary` o
`PerceptionKnowledge.State.Test.IntentReplayPerception.Primary`. Per test di sistema generici si usa
`<Root>.Test.<Scenario>`.

La struttura riserva tre scenari descrittivi per ogni root:

| Root | Tre tag di test di riferimento |
| --- | --- |
| `GameplayAction` | `GameplayAction.Test.Validation`, `GameplayAction.Test.Execution`, `GameplayAction.Test.Cancellation` |
| `IntentReplay` | `IntentReplay.Test.Recording`, `IntentReplay.Test.Playback`, `IntentReplay.Test.Recovery` |
| `Relation` | `Relation.Test.Policy`, `Relation.Test.Symmetry`, `Relation.Test.Temporal` |
| `PerceptionKnowledge` | `PerceptionKnowledge.Test.Observation`, `PerceptionKnowledge.Test.Memory`, `PerceptionKnowledge.Test.Evidence` |
| `Puzzle` | `Puzzle.Test.Signal`, `Puzzle.Test.Graph`, `Puzzle.Test.Completion` |
| `Interaction` | `Interaction.Test.Availability`, `Interaction.Test.Execution`, `Interaction.Test.Rejection` |

Questi nomi sono la convenzione per nuovi test; si registra un tag solo quando una fixture lo usa.
I test esistenti possono specializzare ulteriormente lo scenario, purché mantengano `Test` nel
percorso.

## Separazione tra stato, evento, risultato e comando

- Uno **stato osservabile** usa `PerceptionKnowledge.State.*` e descrive una condizione persistente.
- Un **evento osservabile** usa `PerceptionKnowledge.Event.*` e descrive un fatto puntuale.
- Una **causa** usa `PerceptionKnowledge.Cause.*` e spiega l'origine dell'osservazione.
- Un **comando/opzione di interazione** usa `Interaction.*` e identifica ciò che il giocatore può
  richiedere.
- Un **tipo di Gameplay Action** usa `GameplayAction.Type.*` e identifica l'operazione schedulata.
- Un **risultato terminale** usa `GameplayAction.Result.*` e non deve essere collocato sotto il root
  del servizio che lo ha prodotto.

Per questo motivo la porta usa contemporaneamente:

- `PerceptionKnowledge.State.Paradox.Barrier.Open` per la conoscenza dello stato;
- `Interaction.Paradox.Barrier.Open` per il comando mostrato nel catalogo/UI;
- `GameplayAction.Type.Paradox.Interaction.Receiver.SetState` per l'azione eseguita;
- `GameplayAction.Result.Failure.Paradox.Interaction.*` per i suoi fallimenti.

Questi tag non sono intercambiabili anche quando condividono la parola `Open`.

## Ownership delle dichiarazioni

- `GameplayActions` possiede lock e risultati generici.
- `GameplayActionsGridWorld` possiede il tipo di azione e i risultati GridWorld.
- `IntentReplay` possiede origin replay, correlazione, failure di servizio e cancellazione playback.
- `EntityRelations` possiede i domini `Relation.Domain.*`.
- `PerceptionKnowledge` possiede i sensi generici.
- il modulo `Paradox` possiede le specializzazioni di gioco sotto i root condivisi.
- i tag di test locali restano `UE_DEFINE_GAMEPLAY_TAG_STATIC` nel relativo file di test quando non
  devono essere condivisi.

Le dichiarazioni native sono la fonte autorevole per i tag runtime. Data Table e configurazione
possono aggiungere vocabolario data-driven, ma non devono creare un secondo nome per lo stesso
concetto.

## Compatibilità e migrazione

`Config/DefaultGameplayTags.ini` mantiene redirect solo per mapping univoci. I vecchi
`Barrier.State.Open`, `Barrier.State.BlockingPassage` e `Barrier.State.Moving` non hanno un redirect
permanente: erano stati riutilizzati sia come stato sia come identità di interazione, quindi un
redirect globale sarebbe semanticamente errato.

Gli asset conosciuti sono stati risalvati con il significato corretto:

| Asset | Tag canonici |
| --- | --- |
| `BP_Door` | `Interaction.Paradox.Barrier.Open`, `Interaction.Paradox.Barrier.Close` |
| `WBP_InteractionWidget_Test_01` | `Interaction.Paradox.Barrier.Open`, `Interaction.Paradox.Barrier.Close` |
| `DA_ActivateReceiverAction` | `Interaction.Paradox.Barrier.Open` |
| `DA_DeactivateReceiverAction` | `Interaction.Paradox.Barrier.Close` |

## Checklist per nuovi tag

1. Identificare il tipo di dato prima dell'owner.
2. Riutilizzare uno dei sei root; non creare root per Actor o feature singole.
3. Inserire l'owner dopo il ruolo quando il concetto non è generico.
4. Usare un leaf che descriva il significato, non il nome della classe C++.
5. In un test includere sempre il segmento `Test` e preferire uno scenario descrittivo.
6. Cercare tag equivalenti prima di dichiararne uno nuovo.
7. Se si rinomina un tag serializzato, aggiungere un redirect solo se il mapping è univoco e
   risalvare gli asset che lo contengono.
8. Verificare C++, config, Blueprint, Data Asset, Data Table, mappe ed External Actor prima di
   rimuovere il nome precedente.
