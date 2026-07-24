# Modulo IntentReplay

Modulo runtime responsabile di:

- registrazione transazionale degli eventi `Accepted` di `GameplayActions`;
- finalizzazione di `UIntentReplayTrack` immutabili;
- validazione ricorsiva dei parametri;
- preparazione asincrona e compatibilità;
- scheduling one-shot del replay;
- isolamento di sessioni, handle e journal.

L’API pubblica parte da `UIntentReplayComponent`. La documentazione d’uso completa è in [`../../../Docs/README.md`](../../../Docs/README.md).
