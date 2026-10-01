# Modulo IntentReplay

BeginExternalReplayInterruption preserva le azioni già in background, senza inserirle nel pending
set di reissue. StopReplay/reset/teardown conservano la cancellazione esplicita. Per investigazioni
durante lavoro in background usare bPauseBoundActions=false.

La strategia può specializzare IsExpectedTerminalResult(Intent, Result), default false. Un fallimento
semantico riconosciuto resta nel journal con lo stato reale e non attiva StopPlayback. Gli errori
inattesi di target/configurazione/lifecycle devono restare osservabili.

OriginalResult conserva anche OutcomeParameters copiati da GameplayActions prima del cleanup,
se Ended arriva prima della finalizzazione immutabile del track. Usare solo dati replay-safe.

Il runtime espone snapshot per interruzioni esterne recuperabili e API di reissue/already
satisfied. La playback session mantiene privato il pending set; track e Recorded Intent restano
immutabili.

Modulo runtime responsabile di:

- registrazione transazionale degli eventi `Accepted` di `GameplayActions`;
- finalizzazione di `UIntentReplayTrack` immutabili;
- validazione ricorsiva dei parametri;
- preparazione asincrona e compatibilità;
- scheduling one-shot del replay;
- interruzione reentrancy-safe dei batch di entry dovute dopo ogni transizione fuori da `Playing`;
- barriera di completamento sulla durata totale registrata, inclusa l'eventuale coda inattiva;
- isolamento di sessioni, handle e journal.

Quando la playback viene fermata, il motivo di cancellazione della Gameplay Action è il tag
`GameplayAction.Result.Cancelled.IntentReplay.PlaybackStopped`. Il ramo `IntentReplay.Failure.*`
resta invece riservato agli errori propri del servizio di recording/playback e non ai risultati
terminali dello scheduler Gameplay Actions.

Il core pubblica inoltre:

- Recording Session ID separato dal Track ID;
- snapshot immutabili dei clock recording/playback;
- allocazione atomica di timeline point e sequence condivise;
- lifecycle generico Blueprint/native per ogni transizione autorevole;
- Action Replay Track formato `2`, con compatibilità di validazione/replay per il formato `1`.

Le dipendenze AI e `PerceptionKnowledge` appartengono esclusivamente al modulo fratello
`IntentReplayPerception`; non devono essere aggiunte a questo modulo.

L’API pubblica parte da `UIntentReplayComponent`. La documentazione d’uso completa è in [`../../../Docs/README.md`](../../../Docs/README.md).
