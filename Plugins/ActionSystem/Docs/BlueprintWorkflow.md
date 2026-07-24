# Workflow Blueprint

## Flusso consigliato

1. Aggiungere `GameplayActionComponent` all'Actor che possiede le azioni.
2. Passare una `GameplayActionDefinition` a `Create Gameplay Action Request`.
3. Verificare il risultato con `Was Action Request Created`.
4. Configurare, se necessario, `Set Request Priority`, `Set Request Blocked Policy` e `Set Request Context`.
5. Usare `Set Request Parameter` per modificare soltanto campi già dichiarati nello schema della Definition.
6. Chiamare `Submit Action` e verificare `Was Action Submission Accepted` oppure lo `Status` del risultato.
7. Conservare l'handle per cancel/query, non il puntatore all'istanza.

Una struct `FGameplayActionRequest` costruita manualmente resta non inizializzata e viene respinta. La factory copia profondamente lo storage del Property Bag: cambiare Definition o altre request non modifica la request già creata.

## Parametri wildcard

I nodi seguenti hanno un pin wildcard type-safe:

- `Set Request Parameter`;
- `Get Request Parameter`;
- `Get Action Parameter`;
- `Get Action Event Parameter`.

Sono supportati bool e numerici, enum, struct, Gameplay Tag, Vector, Rotator, Transform, riferimenti object/soft object e class/soft class. Il tipo collegato deve corrispondere al tipo riflesso del campo. I nodi:

- non aggiungono campi mancanti;
- non cambiano lo schema;
- restituiscono `ParameterNotFound` o `TypeMismatch` in caso di errore;
- lasciano invariato il valore di output se una lettura fallisce.

## Eventi

`On Action Event` riceve tutti gli eventi. Sono disponibili anche i delegate specifici:

- `On Action Accepted`;
- `On Action Rejected`;
- `On Action Started`;
- `On Action Paused`;
- `On Action Resumed`;
- `On Action Ended`.

Gli eventi sono distribuiti FIFO. È consentito invocare normalmente il componente da questi delegate, per esempio sottomettere una nuova request da `On Action Ended` o cancellare un'altra azione. Submit, cancel, pause e registrazione del sink devono avvenire sul Game Thread.

Le chiamate rientranti effettuate dentro la validazione `Can Start Action` o dentro la scrittura transazionale dell'evento Accepted vengono respinte intenzionalmente.

## Lifecycle Init e Start

Gli hook dell'istanza hanno responsabilità distinte:

1. `Can Start Action` valida request, owner e configurazione senza avviare gameplay.
2. `Action Init` viene chiamato una sola volta dopo l'accettazione. Viene eseguito anche per action accodate ed è il posto corretto per leggere parametri e memorizzare riferimenti transient.
3. `Action Start` viene chiamato una sola volta esclusivamente quando l'action acquisisce tutti i lock ed entra in `Running`.
4. `Action Cleanup` viene sempre chiamato per ogni action accettata, anche se viene cancellata o scade in queue senza aver mai ricevuto Start.

Durante `Action Init` sono disponibili i getter, ma il componente respinge submit, cancel, pause/reset, registrazione del journal sink e completamenti terminali rientranti. Init deve quindi preparare stato locale reversibile, non modificare lo scheduler.

`Accepted` e `Started` restano eventi strutturati del componente. Non esiste un evento `Initialized` nel journal: Init è un dettaglio lifecycle dell'istanza.

## Tick dell'istanza

Il tick è opt-in per ogni classe/istanza di azione:

1. nella Blueprint derivata da `GameplayActionInstance`, abilitare `Action Tick Enabled` nei Class Defaults, oppure chiamare `Set Action Tick Enabled` da un hook protetto;
2. implementare l'evento `Action Tick` e usare il relativo `Delta Seconds`;
3. terminare comunque l'azione con `Succeed Action` o `Fail Action`.

`Action Tick` viene chiamato sul Game Thread soltanto nello stato `Running`. Non viene chiamato mentre l'azione è `Queued`, `Paused`, `Ending` o terminale. Il componente disabilita automaticamente il proprio tick quando non esistono action running opt-in né action queued con timeout attivo.

È sicuro completare o fallire l'azione, cancellarne un'altra o effettuare una submission da `Action Tick`: il componente itera uno snapshot degli handle e le azioni avviate durante quel callback iniziano a ricevere tick dal frame successivo.

## Esempio Jump queue-safe

`BP_ActionTest_Jump` nel Content del plugin mostra il pattern usato per action dipendenti da una condizione che può diventare vera un frame dopo:

- `Can Start Action` verifica soltanto che l'owner del componente sia un `Character`;
- `Action Init` legge `TestProperty` e memorizza il Character;
- `Action Start` registra il proprio callback `Landed`, prova `CanJump` e salta subito quando possibile;
- se `CanJump` è ancora falso, abilita `Action Tick` e ritenta finché può emettere un solo `Jump`;
- `Landed` viene ignorato finché questa action non ha realmente emesso il salto;
- `Action Cleanup` disabilita il tick, usa `Unbind Event from Landed Delegate` per rimuovere soltanto il proprio binding e azzera riferimenti e flag.

Questo risolve il caso in cui una Jump queued parte dentro il callback `Landed` della Jump precedente: in quel momento il Character può risultare ancora `Falling`, ma diventa nuovamente jumpable nel frame successivo.

## Tempo massimo in queue

Impostare `Max Queue Time Seconds` nella Definition. Il conteggio inizia soltanto quando l'istanza entra realmente in `Queued`; una action avviata immediatamente non consuma questo budget. Alla scadenza:

- non viene chiamato `Action Start`;
- viene chiamato `Action Cleanup`;
- lo stato terminale è `Failed`;
- il reason tag è `GameplayAction.Result.Failure.QueueTimeout`;
- `On Action Ended` e il journal ricevono limite e tempo effettivamente trascorso.

Il valore `0` disabilita la scadenza. `Pause Actions` e la world pause congelano il conteggio.

## Pause e shutdown

`Pause Actions` mantiene i lock e porta le azioni running in stato `Paused`. Le submission effettuate durante la pausa sono accettate in coda anche se la policy è `Reject`. `Resume Actions` ripristina le azioni e rivaluta la coda.

La fine del grafo di `Action Start` non conclude automaticamente l'azione. Finché non viene chiamato `Succeed Action`, `Fail Action`, `Cancel Action` o un percorso di interrupt/abort, lo stato resta `Running` e i lock restano acquisiti. Quando la transizione terminale rilascia i lock, la coda viene rivalutata automaticamente.

`Deactivate`, `EndPlay` e distruzione bloccano nuove submission e abortiscono deterministicamente azioni running e queued. Dopo `Ended`, l'istanza transient viene rilasciata; stato e risultato terminale leggero restano interrogabili tramite handle.
