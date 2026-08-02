# Compatibilità, failure e troubleshooting

## Compatibilità

La modalità `StrictRecordedSchema` richiede fedeltà per:

- schema del Property Bag;
- Action Tag;
- execution lock;
- interruptibility;
- timeout opzionale;
- limite di permanenza in queue.

Priorità e blocked policy registrate vengono applicate esplicitamente alla nuova request.

La modalità `CopyCompatibleValuesUseCurrentDefaults` copia soltanto i parametri con nome e tipo esatti. Campi nuovi mantengono i default correnti; campi rimossi o cambiati vengono riportati nel `FIntentReplayCompatibilityReport`.

## Failure policy

Default:

- submit rifiutato: `StopPlayback`;
- action terminata senza successo: registra la divergence e continua la timeline.

Le alternative sono configurabili in `FIntentReplayPlaybackOptions`.

`StopReplay`, failure e teardown cancellano soltanto gli handle presenti nella playback session. Action estranee sullo stesso `UGameplayActionComponent` non vengono toccate.

Una sostituzione registrata, per esempio un secondo `MoveTo` che interrompe il primo, viene
riconosciuta confrontando `FGameplayActionResult.CausingActionHandle` con gli handle appartenenti
alla playback session. Non viene usato il testo diagnostico. Con `StopPlayback`, le interruzioni
causate da action esterne continuano a fermare il replay.

## Execution Journal

Ogni recording e playback session possiede un journal distinto. Il componente conserva anche un journal ambientale per gli eventi osservati senza sessione.

Capacity policy:

- `Disabled`;
- `BoundedRingBuffer`, default;
- `UnboundedForCurrentSession`.

Il journal conserva snapshot strutturati. Le stringhe diagnostiche servono all’analisi, non sono dati gameplay autoritativi.

## Debug runtime

Il modulo usa una sola categoria:

```text
LogIntentReplay
```

I dettagli per istanza richiedono entrambe le condizioni:

```text
IntentReplay.Debug 1
bEnableDebug = true
```

`GetDebugSnapshot` espone binding, stato recording/playback, ID, conteggi, drain pendenti, prossimo indice, handle posseduti e ultima diagnostica. Il plugin aggiunge scope Unreal Insights alle operazioni principali di recording, preparazione e submit.

Il modulo opzionale usa una categoria e un gate separati:

```text
LogIntentReplayPerception
IntentReplayPerception.Debug 1
bEnableDebug = true
```

Per colori, HUD, filtri, dump e matching consultare
[`IntentReplayPerception/Docs/DEBUGGING.md`](../Source/IntentReplayPerception/Docs/DEBUGGING.md).

## Problemi comuni

### Il track ha zero entry

Prima distinguere un track realmente vuoto da un riferimento nullo. Se il log Blueprint contiene
`Accessed None` su `GetLastFinalizedTrack`, `GetEntryCount` mostra `0` soltanto come valore di fallback:
non esiste ancora un track finalizzato.

Dalla seconda registrazione in poi il getter può ancora contenere il track precedente durante
`AsyncStop`; non usarlo come segnale che la sessione corrente sia già finalizzata. Il riferimento
autoritativo del nuovo track arriva da `OnRecordingFinalized`.

Con `RequestStopRecording(AsyncStop)` la finalizzazione è asincrona. Non leggere
`GetLastFinalizedTrack` subito dopo la richiesta: attendere `OnRecordingFinalized`. Se il workflow
deve rendere il track disponibile nello stesso ramo di esecuzione, lasciare il default `Immediate`;
le entry
`Accepted` vengono conservate, mentre gli original result delle action ancora attive restano assenti.

Se un vecchio asset mostra ancora `DrainTrackedActions`, ricompilarlo e selezionare `AsyncStop`: il
vecchio valore resta riconosciuto soltanto per compatibilità ed è nascosto nei nuovi menu.

Verificare:

- la Definition non usa journaling `Disabled`;
- la recording session era `Recording`, non `Paused` o `Draining`;
- la query di eleggibilità accetta Action Tag e Origin;
- i parametri non contengono riferimenti runtime non registrabili;
- l’azione ha superato la transazione journal.

In UE 5.8 i soft path possono contenere internamente una `FUtf8String`. IntentReplay considera
`FUtf8StrProperty` e `FAnsiStrProperty` valori stringa deterministici; una warning che li segnala
come unsupported indica una build precedente alla correzione della policy di recordability.

Per le action GridWorld, `InjectedPath.AgentProperties.PreferredNavData.SubPathString` è un caso
normale e registrabile. Se il log riporta `Optional journal rejected action` per quel percorso,
ricompilare il plugin e verificare che il track finalizzato abbia un numero di entry maggiore di zero.

### `InitializeIntentReplay` fallisce

Controllare che:

- esista un `UGameplayActionComponent` sullo stesso Actor;
- nessun altro journal sink sia registrato;
- il componente non sia in teardown.

### `PrepareReplay` resta `Preparing`

Le Definition vengono caricate tramite soft reference/Primary Asset ID. Attendere `OnReplayPrepared` e verificare che gli asset siano inclusi e risolvibili.

### Il track sparisce durante il reset

Il riferimento non è trattenuto dal Garbage Collector. Spostarlo in una `UPROPERTY(Transient)` posseduta da un oggetto che sopravvive al reset.

### Il replay si registra di nuovo

La query predefinita esclude `GameplayAction.Origin.Replay`. Verificare di non aver sostituito `TrackEligibilityQuery` con una query che include esplicitamente l’origine replay.

## Test

Le suite principali sono:

```text
IntentReplay.*
IntentReplayPerception.*
GameplayActions.*
```

Per ambienti CI o locali senza DDC condivisa usare `-DDC-ForceMemoryCache`.
