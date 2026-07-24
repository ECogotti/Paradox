# GameplayActions

Le integrazioni opzionali sono separate dal core:

- `GameplayActionsAI` aggiunge task e preflight per Behavior Tree e StateTree;
- `GameplayActionsGridWorld` aggiunge la action pronta all'uso Grid Move To Cell.

Il modulo `GameplayActions` non dipende da questi bridge.

`GameplayActions` è un plugin runtime per Unreal Engine 5.8 che trasforma definizioni data-driven in esecuzioni transient, le ordina in modo deterministico e coordina la concorrenza tramite Gameplay Tag usati come lock.

Il plugin non dipende da `Paradox`, `GridWorld`, `PuzzleSystem` o `IntentReplay`. L’integrazione disponibile resta unidirezionale: `IntentReplay -> GameplayActions`.

## Setup

Il progetto abilita il plugin `GameplayActions` in `Paradox.uproject`. Per riutilizzarlo in un altro progetto:

1. copiare la cartella `ActionSystem` sotto `Plugins`;
2. abilitare `GameplayActions` nel Plugin Browser o nel file `.uproject`;
3. aggiungere `GameplayActions` alle dipendenze pubbliche o private del modulo C++ che usa l'API;
4. compilare il target Editor.

Il solo modulo runtime dipende da `Core`, `CoreUObject`, `Engine` e `GameplayTags`. Non esistono moduli Editor, dipendenze GAS, networking o salvataggio.

## Concetti

- `UGameplayActionDefinition`: Primary Data Asset di authoring. Contiene classe, Action Tag, schema/default del Property Bag, priorità, lock, policy, interruptibility, journaling, timeout opzionale di esecuzione, limite di permanenza in queue e metadati di debug.
- `FGameplayActionRequest`: copia isolata dei default. Una request valida nasce esclusivamente da `Create Gameplay Action Request`.
- `UGameplayActionInstance`: esecuzione transient posseduta dal componente. I suoi snapshot sono privati e non mutabili dai consumer.
- `UGameplayActionComponent`: unica autorità per scheduling, lock, transizioni, cleanup ed eventi.
- `FGameplayActionHandle`: identificatore `int64`, crescente e mai riutilizzato durante la vita del componente.
- `FGameplayActionEvent`: snapshot strutturato e indipendente destinato a observer e journal.
- `UGameplayActionJournalSink`: contratto sincrono con un solo sink registrabile per componente.

Ogni action accettata riceve `Action Init` una sola volta. Se deve attendere, Init avviene già nello stato `Queued`; `Action Start` viene invece chiamato soltanto dopo l'acquisizione atomica di tutti i lock e il passaggio a `Running`.

Le istanze possono richiedere un `Action Tick` opt-in, disponibile in Blueprint e C++. Il componente abilita il proprio tick soltanto quando serve a una action running oppure al conteggio di almeno un queue timeout.

## Creare una Definition

Nel Content Browser creare un Data Asset e scegliere `GameplayActionDefinition`, quindi configurare almeno:

- una `Instance Class` concreta;
- un `Action Tag` valido;
- i parametri nel `Default Parameters` Property Bag, anche se lo schema è vuoto;
- gli eventuali lock sotto `GameplayAction.Lock`.

`Max Queue Time Seconds` controlla per quanto tempo l'action può restare realmente in `Queued`:

- `0`: queue senza limite;
- valore `> 0`: allo scadere l'action termina come `Failed` con reason `GameplayAction.Result.Failure.QueueTimeout`;
- il conteggio usa tempo gameplay-scaled e si congela durante world pause e `Pause Actions`.

Per provare l'azione di riferimento scegliere `GameplayWaitAction` e aggiungere al Property Bag un campo numerico chiamato esattamente `Duration`, espresso in secondi. Un valore negativo viene respinto in validazione; zero completa l'azione immediatamente.

`Optional Timeout` descrive invece un futuro limite di esecuzione: viene copiato negli snapshot runtime/eventi, ma non viene ancora applicato automaticamente.

## Documentazione correlata

- [Workflow Blueprint](BlueprintWorkflow.md)
- [API e ownership C++](CppAPI.md)
- [Scheduler, lock e priorità](Scheduler.md)
- [Debug e troubleshooting](Debugging.md)
- [Integrazione con IntentReplay](IntentReplayIntegration.md)
