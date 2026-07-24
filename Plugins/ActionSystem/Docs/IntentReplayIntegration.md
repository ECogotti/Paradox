# Integrazione con IntentReplay

L’integrazione runtime è disponibile nel plugin separato `IntentReplay`. `GameplayActions` continua a non includere né conoscere tipi di `IntentReplay`.

## Direzione della dipendenza

La sola direzione ammessa è:

```text
IntentReplay -> GameplayActions
```

`IntentReplay` aggiunge `GameplayActions` al proprio `Build.cs`, implementa il journal sink e lo registra sul componente interessato. Non esiste alcuna dipendenza inversa, include o adapter dentro `GameplayActions`.

## Origin e correlation

Quando `IntentReplay` costruisce una request di replay valorizza:

- `OriginTag` con `GameplayAction.Origin.Replay`;
- `Correlation.Type` con `IntentReplay.Correlation.RecordedIntent`;
- `Correlation.Id` con il GUID del `FRecordedIntentId`.

`FGameplayActionCorrelationData` resta volutamente generico: `GameplayActions` non deve includere `FRecordedIntentId`.

## Cosa registrare

Il sink riceve `FGameplayActionEvent`, che contiene asset ID/soft reference della Definition, Action Tag, copia dei parametri, priorità, policy, lock, timeout opzionale, origin, correlation, sequence e gli eventuali risultati di submit/termine.

Per le decisioni di queue sono disponibili anche:

- `MaxQueueTimeSeconds`, limite copiato dalla Definition;
- `QueueElapsedSeconds`, tempo effettivamente accumulato al momento dell'evento;
- reason `GameplayAction.Result.Failure.QueueTimeout` sull'Ended di una scadenza.

Non esiste un evento journal `Initialized`: `Action Init` è un hook locale dell'istanza. La sequenza autoritativa osservabile resta `Accepted`, eventuali eventi di preemption, `Started` quando l'action entra realmente in esecuzione, quindi `Ended`.

Per una registrazione autoritativa usare enum, tag, handle, sequence, GUID e Property Bag. `DiagnosticMessage` non è un campo autoritativo e può cambiare senza invalidare una registrazione.

Una Definition con journal `Required` garantisce che l'Accepted sia scritto prima di qualsiasi preemption o acquisizione lock. Il sink non deve effettuare submit/cancel nella stessa chiamata sincrona: tali operazioni vengono respinte come rientranti. Eventuali reazioni vanno differite oppure eseguite dai delegate lifecycle.

Ogni componente accetta un solo sink. `UIntentReplayComponent` coordina ownership, registrazione e deregistrazione durante il proprio lifecycle.

Le Definition destinate alla registrazione devono usare journal `Required` o `Optional`. Con `Disabled`, gli eventi restano osservabili attraverso i delegate e l’Execution Journal di `IntentReplay`, ma l’`Accepted` non partecipa alla registrazione transazionale e non entra nel track.

Il track finalizzato è transient e indipendente dall’Actor sorgente. Il sistema di reset deve conservarlo tramite `UPROPERTY` e può passarlo a uno o più `UIntentReplayComponent` appartenenti ad altre entità.
