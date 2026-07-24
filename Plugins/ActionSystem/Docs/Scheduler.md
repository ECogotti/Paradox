# Scheduler, lock e priorità

## Ordine deterministico

Lo scheduler ordina le azioni con queste chiavi:

1. priorità decrescente;
2. submission sequence crescente.

Gli handle e le sequence sono monotoni. A pari priorità, la request sottomessa prima viene valutata prima. Anche un gruppo di conflitti preempted viene terminato nello stesso ordine.

## Lock esatti

I lock sono Gameplay Tag sotto `GameplayAction.Lock`, ma il conflitto usa uguaglianza esatta. Per esempio `GameplayAction.Lock.Hand` e `GameplayAction.Lock.Hand.Left` non confliggono automaticamente.

Un'azione acquisisce tutto il proprio lock set oppure nessun lock. Le azioni queued non possiedono lock. Questo evita acquisizioni parziali e deadlock impliciti.

## Decisione di submit

Se non esistono conflitti, l'azione parte. Se esistono conflitti, la nuova azione può preemptare soltanto quando ogni conflitto:

- è running o paused;
- è interruptible;
- ha priorità strettamente inferiore.

Il controllo è all-or-none: se una sola azione non soddisfa le condizioni, non viene interrotto nessun conflitto. La policy `Queue` accoda la request; `Reject` restituisce `RejectedBlocked`.

L'ordine osservabile della preemption è sempre:

```text
Incoming Accepted -> Previous Ended -> Incoming Started
```

Dopo l'Accepted transazionale, l'istanza viene posseduta dal componente e riceve `Action Init`. Se deve attendere, Init avviene dopo l'inserimento in `QueuedHandles`; se può partire, avviene in stato `Starting`. `Action Start` arriva solo dopo l'acquisizione dei lock e il passaggio a `Running`.

La coda viene rivalutata dopo il rilascio di lock. Un candidato queued può partire o preemptare solo se in quel momento riesce ad acquisire l'intero set.

## Avvio automatico dalla coda

Sì: una queued action parte automaticamente quando una transizione terminale rilascia tutti i lock che le servono. La rivalutazione avviene dopo `SucceedAction`, `FailAction`, `CancelAction`, interrupt e abort, e anche dopo `ResumeActions`.

Una queued action resta in attesa se:

- il componente è in pausa o in shutdown;
- un'altra action attiva conserva almeno uno dei lock esatti richiesti;
- per acquisire tutti i lock dovrebbe preemptare un conflitto di priorità pari/superiore o non interruptible.

Il semplice ritorno da `OnActionStarted` o la fine del flusso di esecuzione Blueprint non sono una transizione terminale. L'action resta `Running`, conserva i lock e blocca la coda finché non chiama `SucceedAction`/`FailAction` o viene terminata dal componente. `GetActionState`, `GetQueuedActionHandles` e `GetDebugSnapshot` permettono di distinguere questo caso da un problema di scheduling.

## Scadenza della queue

`MaxQueueTimeSeconds` è un limite per istanza:

- `0` significa queue illimitata;
- il contatore parte entrando realmente in `Queued`;
- usa `DeltaTime` gameplay-scaled;
- si congela durante world pause e `PauseActions`;
- smette definitivamente di avanzare quando l'action parte o termina.

All'inizio del tick del componente, le action queued con timeout vengono aggiornate in ordine scheduler. Quelle che raggiungono il limite terminano prima degli Action Tick delle running action, come `Failed` con `GameplayAction.Result.Failure.QueueTimeout`. Una action scaduta ha ricevuto Init e riceve Cleanup, ma non Start, Cancelled o Interrupted.

Il component tick viene abilitato automaticamente anche quando nessuna action running usa Action Tick ma esiste almeno una queued action con timeout.

## Percorso terminale

Ogni fine attraversa un solo percorso:

```text
Ending -> hook specifico -> Cleanup -> stato terminale -> rilascio lock -> Ended -> rilascio istanza
```

Gli stati terminali sono `Succeeded`, `Failed`, `Cancelled`, `Interrupted` e `Aborted`. `DiagnosticMessage` è soltanto diagnostico; `TerminalState` e `ReasonTag` sono i dati autoritativi.

## Journaling transazionale

Il primo `Accepted` viene offerto al sink prima di inserire l'istanza, acquisire lock o interrompere conflitti:

- `Disabled`: nessun evento dell'azione viene inviato al sink;
- `Optional`: sink assente o rifiuto non bloccano l'azione;
- `Required`: sink assente o rifiuto producono `RejectedJournal` senza alcuna modifica allo scheduler.

Il rifiuto di eventi successivi viene loggato ma non può annullare retroattivamente una transizione già commessa.
