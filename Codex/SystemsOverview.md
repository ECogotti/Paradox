# Systems Overview

## Obiettivo

Questo documento descrive i principali sistemi modulari da sviluppare come plugin UE5 per il progetto **Paradox**.

I plugin devono essere:

- generici;
- modulari;
- riutilizzabili in altri progetti;
- estendibili tramite C++ e Blueprint;
- configurabili tramite dati;
- privi di dipendenze dirette dalle regole specifiche di Paradox.

Le regole specifiche del gioco, come il Temporal Index, la generazione del paradosso e la creazione dei cloni, devono essere implementate in un modulo di integrazione del progetto che utilizza i plugin senza modificarne la struttura interna.

Il sistema dei puzzle ambientali è considerato già disponibile e dovrà integrarsi con questi plugin tramite interfacce, componenti ed eventi.

Ogni plugin dovrà includere strumenti di debug specifici per il proprio dominio. Non è previsto, per il momento, un plugin diagnostico centralizzato.

---

# 1. GridWorld

## Scopo

`GridWorld` rappresenta lo spazio logico navigabile del gioco.

Gestisce:

- griglia e celle;
- conversione tra coordinate del mondo e coordinate della griglia;
- ostacoli;
- costi di attraversamento;
- occupazione delle celle;
- ricerca dei percorsi;
- raggiungibilità;
- modificatori dinamici della navigazione;
- visualizzazione e debug della griglia.

Il sistema non deve conoscere cloni, rewind, porte, terminali o paradossi.

Gli elementi ambientali possono modificare la griglia tramite interfacce o componenti generici.

## Dipendenze

Nessuna.

## Sistemi che dipendono da GridWorld

- `GameplayActions`, per le azioni di movimento;
- `GoalAgents`, per obiettivi spaziali e navigazione;
- `IntentReplay`, quando registra celle o destinazioni.

---

# 2. GameplayActions

## Scopo

`GameplayActions` fornisce un linguaggio comune per rappresentare le azioni eseguibili da player, AI e agenti registrati.

Esempi di azione:

- raggiungere una destinazione;
- interagire con un oggetto;
- attendere;
- guardare verso una posizione;
- raccogliere o rilasciare un oggetto;
- attivare una capacità;
- interrompere l'azione corrente.

Ogni azione dovrebbe separare:

1. richiesta;
2. validazione;
3. esecuzione;
4. interruzione;
5. completamento;
6. fallimento e relativa motivazione.

Il sistema puzzle ambientale dovrà esporre i propri oggetti come target compatibili con queste azioni, senza che `GameplayActions` dipenda direttamente dalle classi concrete del puzzle system.

## Dipendenze

- `GridWorld` per le azioni che richiedono movimento o destinazioni sulla griglia.

La dipendenza da `GridWorld` può essere mantenuta opzionale per consentire anche azioni non spaziali.

## Sistemi che dipendono da GameplayActions

- `GoalAgents`;
- `IntentReplay`;
- `GameplayFeedback`.

---

# 3. WorldState

## Scopo

`WorldState` gestisce la registrazione e il ripristino dello stato del mondo.

Gestisce:

- baseline iniziale;
- snapshot;
- reset completo;
- reset parziale;
- ordine di ripristino;
- dipendenze tra oggetti da ripristinare;
- attori creati o distrutti;
- stato temporaneo;
- stato persistente tra più iterazioni;
- validazione del risultato del reset.

Gli attori partecipano al sistema tramite interfacce o componenti. Il plugin non deve contenere riferimenti hardcoded a porte, terminali, oggetti distruttibili o altri elementi specifici.

Il sistema puzzle ambientale dovrà registrare i propri elementi come partecipanti al salvataggio e al reset.

## Dipendenze

Nessuna.

## Sistemi che dipendono da WorldState

- il sistema puzzle ambientale, per il ripristino degli elementi della mappa.

---

# 4. EntityRelations

## Scopo

`EntityRelations` descrive identità e relazioni tra entità.

Le relazioni devono poter essere asimmetriche. Il risultato della relazione tra A e B può essere diverso dal risultato tra B e A.

Il sistema può rispondere a domande come:

- A considera B alleato, nemico o neutrale?
- A può interagire con B?
- A può danneggiare B?
- A può bloccare B?
- B appartiene a una generazione successiva?
- A conosce già B?
- quale conseguenza deve essere applicata alla relazione tra A e B?

In Paradox il sistema verrà utilizzato per rappresentare il `Temporal Index` e la relazione tra versioni passate e future dello stesso personaggio.

La regola specifica secondo cui un clone con indice minore genera un paradosso vedendo un clone con indice maggiore non deve essere hardcoded nel plugin. Deve essere implementata come policy specifica del progetto.

## Dipendenze

Nessuna.

## Sistemi che dipendono da EntityRelations

- `GoalAgents`, opzionalmente, per Goal condizionati dalle relazioni;
- futuri sistemi di percezione o combattimento;
- il modulo specifico di Paradox, per la logica temporale e la classificazione contestuale delle entità.

---

# 5. GoalAgents

## Scopo

`GoalAgents` gestisce agenti guidati da obiettivi.

Un agente non esegue necessariamente una sequenza rigida. Tenta invece di raggiungere uno stato desiderato, adattandosi ai cambiamenti del mondo.

Un Goal può contenere:

- obiettivo desiderato;
- target;
- destinazione;
- priorità;
- precondizioni;
- condizioni di completamento;
- condizioni di invalidazione;
- alternative;
- fallback;
- numero massimo di tentativi;
- comportamento in caso di fallimento.

L'agente dovrebbe poter gestire:

- Goal principale;
- sotto-obiettivi;
- sospensione;
- ripresa;
- interruzioni;
- replanning;
- sostituzione del Goal;
- fallimento controllato.

In Paradox questo sistema permette ai cloni di eseguire le intenzioni registrate anche quando lo stato corrente della mappa è diverso da quello della run originale.

## Dipendenze

- `GameplayActions`;
- `GridWorld`, per Goal spaziali e navigazione;
- `EntityRelations`, opzionalmente, per condizioni basate sulle relazioni.

## Sistemi che dipendono da GoalAgents

- `IntentReplay`;
- `GameplayFeedback`.

---

# 6. IntentReplay

## Scopo

`IntentReplay` registra intenzioni e azioni semantiche, invece di limitarsi a registrare trasformazioni frame per frame.

Una sequenza registrata potrebbe contenere:

1. raggiungi una zona;
2. interagisci con un terminale;
3. attendi;
4. raggiungi una porta;
5. attiva un oggetto.

La registrazione deve rimanere separata dallo stato runtime dell'agente.

Un agente può:

- seguire la sequenza originale;
- interromperla temporaneamente;
- ricalcolare un percorso;
- scegliere un'alternativa;
- riprendere la sequenza;
- fallire un'intenzione senza modificare la registrazione originale.

Il plugin può essere riutilizzato anche per ghost, tutorial registrati, demo automatiche o companion che imitano il giocatore.

## Dipendenze

- `GameplayActions`;
- `GoalAgents`;
- `GridWorld`, opzionalmente, per celle, percorsi e destinazioni registrate.

## Sistemi che dipendono da IntentReplay

- `GameplayFeedback`;
- il modulo specifico di Paradox, per la creazione e l'esecuzione dei cloni temporali.

---

# 7. TacticalPause

## Scopo

`TacticalPause` fornisce un controllo generico sullo stato e sulla velocità della simulazione.

Il plugin è limitato alla gestione del tempo di gioco e non deve occuparsi di selezione, ispezione delle entità, percorsi, relazioni, classificazioni o overlay informativi.

Gestisce:

- pausa della simulazione;
- ripresa della simulazione;
- velocità normale `x1`;
- velocità accelerate configurabili, inizialmente `x1.5`, `x2` e `x3`;
- stato autorevole del controllo temporale;
- validazione delle transizioni tra pausa, riproduzione e cambio di velocità;
- eventi pubblici per pausa, ripresa e variazione della velocità;
- ripristino sicuro dello stato temporale precedente quando il sistema viene disattivato o il mondo viene chiuso.

Il plugin deve includere un widget UMG predefinito e sostituibile con almeno i seguenti controlli:

- `Play`, per riprendere la simulazione;
- `Pause`, per sospendere la simulazione;
- `x1`, per tornare alla velocità normale;
- `x1.5`;
- `x2`;
- `x3`.

Le velocità disponibili non devono essere hardcoded nella UI. Devono essere configurabili tramite impostazioni o dati, così il progetto può aggiungere, rimuovere o riordinare i pulsanti senza modificare il core del plugin.

Il sistema deve poter utilizzare il controllo temporale globale di Unreal, ma deve anche prevedere un meccanismo generico di partecipazione o adattamento per eventuali sistemi che non rispettano automaticamente la pausa o la variazione della velocità globale.

Questa estensione non deve creare dipendenze dirette da `GameplayActions`, `GoalAgents`, `IntentReplay` o altri plugin. Gli eventuali adattatori specifici devono essere implementati nei moduli di integrazione che conoscono entrambi i sistemi.

Il plugin non deve contenere:

- selezione di Actor o entità;
- pannelli con Goal, azioni o percorsi;
- visualizzazione della griglia;
- highlight rosso o blu;
- coni visivi;
- aree di rumore;
- classificazioni di pericolo;
- regole specifiche dei paradossi.

Tutte le informazioni tattiche e le relative visualizzazioni appartengono al modulo di gioco che utilizza `TacticalPause`.

## Dipendenze

Nessuna dipendenza logica obbligatoria dagli altri plugin.

## Sistemi che dipendono da TacticalPause

Nessun plugin generico.

Il modulo specifico di Paradox può ascoltare gli eventi del plugin per mostrare o nascondere la propria interfaccia tattica e i propri overlay durante la pausa.

---

# 8. GameplayFeedback

## Scopo

`GameplayFeedback` separa gli eventi semantici del gameplay dalla loro presentazione.

Gli altri sistemi pubblicano eventi come:

- azione completata;
- azione fallita;
- Goal invalidato;
- nuovo Goal selezionato;
- reset iniziato;
- reset completato;
- relazione proibita;
- vittoria;
- fallimento.

`GameplayFeedback` associa questi eventi a:

- audio;
- VFX;
- widget;
- messaggi;
- effetti di camera;
- effetti sui materiali;
- evidenziazioni;
- rallentamenti;
- animazioni dell'interfaccia.

Il plugin non deve controllare direttamente la logica di gameplay.

## Dipendenze

Nessuna dipendenza logica obbligatoria.

Ascolta eventi pubblicati da:

- `GameplayActions`;
- `GoalAgents`;
- `IntentReplay`;
- `WorldState`;
- `EntityRelations`;
- `TacticalPause`;
- modulo specifico di Paradox;
- sistema puzzle ambientale.

## Sistemi che dipendono da GameplayFeedback

Nessuno.

---

# Dipendenze generali

```text
GridWorld
    ↓
GameplayActions
    ↓
GoalAgents
    ↓
IntentReplay

WorldState ─────────────── indipendente
EntityRelations ────────── indipendente
TacticalPause ──────────── indipendente

GameplayFeedback ascolta gli eventi degli altri sistemi e del modulo
specifico di Paradox, ma gli altri sistemi non dipendono da GameplayFeedback.

Il modulo specifico di Paradox utilizza i plugin necessari e può integrare
TacticalPause con UI, selezione e overlay specifici del gioco.
```

---

# Ordine di sviluppo consigliato

## Fase 1 — Fondamenta indipendenti

### 1. GridWorld

È la base della navigazione, della rappresentazione spaziale e delle query di raggiungibilità.

### 2. WorldState

Può essere sviluppato parallelamente a `GridWorld`, perché non dipende da esso.

### 3. EntityRelations

Può essere sviluppato parallelamente agli altri sistemi fondamentali.

---

## Fase 2 — Linguaggio comune delle azioni

### 4. GameplayActions

Deve essere sviluppato prima dei sistemi di Goal e registrazione.

In questa fase dovrebbe essere integrato almeno con:

- movimento sulla griglia;
- interazioni;
- attese;
- cancellazione delle azioni;
- sistema puzzle ambientale.

---

## Fase 3 — Comportamento degli agenti

### 5. GoalAgents

Deve poter eseguire Goal semplici prima dell'introduzione del replay.

Prima milestone consigliata:

- assegnare una destinazione;
- raggiungerla;
- gestire un ostacolo;
- completare o fallire il Goal.

### 6. IntentReplay

Arriva dopo `GoalAgents`, perché la registrazione deve poter essere trasformata in intenzioni o Goal eseguibili.

Prima milestone consigliata:

- registrare una sequenza del player;
- creare un agente;
- trasformare la sequenza in Goal;
- eseguirla in un mondo invariato.

---

## Fase 4 — Controllo temporale del giocatore

### 7. TacticalPause

Non dipende dalla catena principale e può essere sviluppato anche in parallelo.

Conviene tuttavia validarlo quando esistono già sistemi runtime reali, così è possibile verificare che:

- pausa e ripresa non perdano stato;
- i sistemi rispettino correttamente il controllo temporale;
- le velocità `x1.5`, `x2` e `x3` producano un'accelerazione coerente;
- il widget rifletta sempre lo stato autorevole della simulazione;
- gli eventuali adattatori specifici vengano implementati fuori dal plugin.

Prima milestone consigliata:

- pausa;
- ripresa;
- velocità `x1`, `x1.5`, `x2` e `x3`;
- widget UMG predefinito;
- eventi di stato e velocità;
- ripristino sicuro dello stato precedente.

---

## Fase 5 — Presentazione

### 8. GameplayFeedback

L'infrastruttura di eventi può essere introdotta presto, ma i profili completi di feedback dovrebbero essere realizzati quando gli eventi di gameplay sono abbastanza stabili.

---

# Sequenza pratica

```text
1. GridWorld
2. WorldState
3. EntityRelations
4. GameplayActions
5. GoalAgents
6. IntentReplay
7. TacticalPause
8. GameplayFeedback
```

`GridWorld`, `WorldState`, `EntityRelations` e `TacticalPause` non formano una singola catena obbligatoria e possono essere sviluppati in parallelo quando utile.

La catena principale di dipendenze rimane:

```text
GridWorld
    ↓
GameplayActions
    ↓
GoalAgents
    ↓
IntentReplay
```

`WorldState`, `EntityRelations` e `TacticalPause` restano indipendenti dalla catena principale.

---

# Modulo specifico di Paradox

Sopra i plugin generici dovrà esistere un modulo di integrazione specifico del progetto, per esempio `ParadoxGameplay`.

Questo modulo conterrà:

- orchestrazione del ciclo temporale tra run, reset e ricostruzione dei cloni;
- assegnazione del Temporal Index;
- relazione tra passato e futuro;
- regola di generazione del paradosso;
- creazione e configurazione dei cloni;
- configurazione dei Chrono Spawn;
- limiti di rewind;
- condizioni specifiche di vittoria e fallimento;
- collegamento con il sistema puzzle ambientale;
- configurazione dei feedback temporali;
- selezione delle entità durante la pausa;
- pannelli informativi con Goal, azioni, destinazioni e percorsi;
- visualizzazione di percorsi, celle, coni visivi e aree di rumore;
- classificazione contestuale rossa e blu dei cloni;
- gestione e filtraggio degli overlay informativi;
- collegamento tra lo stato di `TacticalPause` e l'interfaccia tattica specifica del gioco.

Il modulo `ParadoxGameplay` utilizza i plugin, ma i plugin non devono dipendere da esso.
