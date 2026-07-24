# Report: replay di BP_TopDownCharacter

Data indagine: 18 luglio 2026.

## Esito

Il trasferimento da `BP_TopDownCharacter` a `BP_CloneCharacter` non era la causa del mancato replay.
Il clone veniva spawnato e il suo `UIntentReplayComponent` si inizializzava correttamente sul proprio
`UGameplayActionComponent`. Il track passato al clone era però vuoto, quindi `StartReplay` non aveva
entry da sottomettere.

Sono state individuate due cause indipendenti nella fase di registrazione:

1. le action di movimento GridWorld venivano rifiutate dal journal sink perché la policy di
   recordability non riconosceva `FUtf8StrProperty`, usata in UE 5.8 dal soft path contenuto in
   `InjectedPath.AgentProperties.PreferredNavData.SubPathString`;
2. `DA_GameplayActionTest_01`, la Definition usata per il Jump, aveva
   `Journal Requirement = Disabled`, quindi il contratto di GameplayActions non inviava mai i suoi
   eventi `Accepted` al recorder.

## Evidenze

Nei log delle sessioni PIE il pattern era ripetibile:

```text
IntentReplayComponent started recording track ...
Optional journal rejected action ... Utf8StrProperty at
InjectedPath.AgentProperties.PreferredNavData.SubPathString
IntentReplayComponent finalized track ... with 0 entries
IntentReplayComponent initialized for owner BP_CloneCharacter_C_0 ...
```

L'ispezione runtime degli asset ha inoltre restituito:

```text
DA_GameplayActionTest_01.journal_requirement = DISABLED
DA_GameplayAction_MoveToGridCell.journal_requirement = OPTIONAL
```

Questo spiega perché il gameplay originale continuava a funzionare: il movimento usa journaling
`Optional`, perciò il rifiuto del sink viene riportato ma non impedisce l'esecuzione live. Il Jump con
journaling `Disabled` non entra proprio nella transazione di registrazione.

## Correzioni applicate

- La policy ricorsiva di IntentReplay accetta ora `FUtf8StrProperty` e `FAnsiStrProperty` come valori
  stringa deterministici.
- Il test `IntentReplay.Recordability.RecursiveRuntimeReference` include un `FSoftObjectPath` reale,
  che in UE 5.8 attraversa il campo UTF-8 responsabile della regressione.
- `DA_GameplayActionTest_01` è configurato con `Journal Requirement = Optional`, così il Jump viene
  inviato al recorder senza trasformare un eventuale rifiuto del journal in un rifiuto dell'azione
  gameplay.

La dipendenza resta `IntentReplay -> GameplayActions`: non è stata introdotta alcuna dipendenza da
GridWorld, Blueprint specifici o Behavior Tree.

## Verifica finale degli asset

Il caricamento headless degli asset dopo il salvataggio ha confermato:

- `DA_GameplayActionTest_01` usa ora journaling `Optional`;
- `BP_CloneCharacter` eredita sia `GameplayActionComponent` sia `IntentReplayComponent`;
- l'`Action Component Override` del clone punta al `GameplayActionComponent` appartenente al clone,
  non al componente del player o al CDO padre;
- `BP_CloneCharacter` usa `BP_CloneController` e `Auto Possess AI = Spawned`.

Non risultano quindi errori di ownership, binding del sink o possesso AI nel setup esaminato.

## Build e test

Verifica eseguita con Unreal Engine 5.8, target `ParadoxEditor Win64 Development`:

- compilazione: riuscita;
- `IntentReplay.*`: 4 test superati;
- `GameplayActions.*`: 13 test superati;
- `GameplayActionsGridWorld.*`: 5 test superati.

Tutte le suite sono state avviate con `-DDC-ForceMemoryCache`.

## Verifica Blueprint consigliata

Dopo `Request Stop Recording`, controllare `Get Entry Count` sul track ricevuto da
`On Recording Finalized`: deve essere maggiore di zero prima dello spawn del clone.

Sul clone:

1. passare quel riferimento a `Prepare Replay` sul componente ereditato dal clone;
2. chiamare `Start Replay` subito soltanto per esito `Ready`;
3. per esito `Preparing`, attendere `On Replay Prepared` e verificare che l'esito sia `Ready`;
4. per esito `Rejected`, stampare il failure strutturato e non chiamare `Start Replay`.

Per una sessione che include click e Spacebar, il conteggio atteso è almeno il numero delle action
effettivamente accettate durante la finestra di registrazione. Il conteggio non deve più restare a zero
per il percorso UTF-8 e il Jump non deve più essere escluso dal journaling.

## Follow-up: apparente conteggio zero dopo la prima correzione

Una nuova sessione PIE ha mostrato un problema diverso:

```text
Accessed None trying to read property CallFunc_GetLastFinalizedTrack_ReturnValue
```

Il dump dell'EventGraph ha confermato che `RequestStopRecording` usava il valore allora chiamato
`FinalizeMode = DrainTrackedActions` (oggi `AsyncStop`), mentre il ramo K leggeva direttamente
`GetLastFinalizedTrack`. In quelle sessioni non compariva alcun evento `finalized track`: il valore
stampato come zero era il fallback Blueprint causato dal riferimento `None`, non il conteggio di un
track esistente.

Per rendere coerente il workflow sincrono attuale M → K, il nodo `RequestStopRecording` di
`BP_TopDownCharacter` è stato configurato con `FinalizeMode = Immediate`. Il Blueprint è stato
ricompilato e salvato senza errori. Le entry già accettate restano nel track; soltanto gli original
result delle action non ancora terminali possono essere assenti.

Per usare la modalità `AsyncStop`, il ramo K non dovrà leggere il getter immediatamente:
dovrà usare il track ricevuto da `OnRecordingFinalized` e abilitare lo spawn/replay soltanto dopo
quell'evento.

In seguito l'API è stata resa meno ambigua: `Immediate` è diventato il default del nodo e
`DrainTrackedActions` è stato rinominato `AsyncStop`, con tooltip che specifica l'obbligo di attendere
`OnRecordingFinalized`. Il nome precedente resta soltanto come alias nascosto/deprecato per gli asset
serializzati.
