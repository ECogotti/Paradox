# Paradox Base Time Loop — Piano di implementazione per Codex

## Scopo

Implementare nel modulo specifico del progetto **Paradox** il gameplay loop temporale di base, prima dell'introduzione del sistema GOAP.

La milestone deve fornire ai collaboratori una versione completa e giocabile del loop principale, composta da:

- selezione del Chrono Spawn iniziale;
- esecuzione e registrazione della run del player;
- rewind;
- reset del mondo;
- creazione dei cloni delle timeline consolidate;
- selezione di un nuovo Chrono Spawn;
- avvio sincronizzato del player e dei playback;
- rilevamento dei paradossi;
- retry dopo un paradosso;
- limite massimo di timeline determinato dalla mappa;
- game over quando non sono più disponibili Chrono Spawn;
- camera ortografica libera;
- supporto alla Tactical Pause;
- fallback temporaneo quando il playback di un clone fallisce.

In questa fase i cloni devono utilizzare esclusivamente il playback tramite `IntentReplay`.

Il GOAP non deve ancora essere implementato, simulato o usato come fallback.

---

# 1. Regole preliminari per Codex

Prima di modificare il progetto, Codex deve:

1. leggere il file root `AGENTS.md`;
2. individuare il modulo specifico che contiene o conterrà la logica di Paradox;
3. cercare e leggere tutti i file presenti nei `CODEX` folder rilevanti;
4. leggere la documentazione presente nei `Docs` folder dei moduli e plugin coinvolti;
5. ispezionare le API e l'implementazione reale dei sistemi esistenti;
6. verificare come il progetto gestisce:
   - Gameplay Actions;
   - registrazione e playback di `IntentReplay`;
   - reset tramite `WorldState`;
   - identità e relazioni tramite `EntityRelations`;
   - Tactical Pause;
   - input;
   - player, controller e camera;
   - il plugin Fab `Line of Sight. Dynamic Mesh.`;
7. individuare i principali caller e le dipendenze;
8. comprendere ownership, lifecycle, registrazione e cleanup;
9. compilare il progetto prima delle modifiche;
10. compilare nuovamente dopo ogni milestone significativa.

Le API reali dei plugin hanno priorità sui documenti progettuali precedenti.

Codex non deve presumere nomi di classi, delegate, funzioni o moduli senza verificarli nel repository e nei sorgenti dei plugin.

La logica specifica di Paradox deve essere implementata nel modulo di gioco che integra i plugin. I plugin generici non devono ricevere regole hardcoded su:

- Chrono Spawn;
- Temporal Index di Paradox;
- creazione dei cloni;
- rewind specifico del gioco;
- paradossi;
- game over per esaurimento delle timeline.

Codex deve evitare refactor non necessari e applicare il cambiamento minimo che produce il risultato richiesto.

---

# 2. Principio C++ e Blueprint

## 2.1 Regola generale

Usare il seguente criterio:

> Se il programmatore deve poter aggiungere, rimuovere o sostituire rapidamente un comportamento, e Blueprint non introduce svantaggi architetturali o di performance, realizzarlo o renderlo estendibile in Blueprint.  
> Se il comportamento mantiene invarianti, stato autorevole, ownership, lifecycle, integrazione tra sistemi o codice sensibile alle performance, implementarlo in C++.

L'obiettivo è ottenere:

- un core nativo stabile;
- un comportamento di default completo;
- una superficie Blueprint rapida da modificare;
- nessuna dipendenza obbligatoria da Blueprint per mantenere valido il gameplay;
- nessuna logica critica dispersa in Blueprint difficili da verificare.

## 2.2 Responsabilità preferibilmente C++

Devono normalmente essere implementati in C++:

- stato autorevole del loop temporale;
- transizioni tra le fasi del loop;
- validazione delle transizioni;
- gestione delle timeline consolidate;
- associazione tra timeline, Chrono Spawn e Replay Track;
- assegnazione e query del Temporal Index;
- integrazione tra i plugin;
- richiesta e completamento del reset;
- costruzione e cleanup delle entità temporali;
- sincronizzazione dell'avvio della run;
- prevenzione di callback e overlap duplicati;
- autorizzazione del rilevamento del paradosso;
- vincoli della camera;
- calcolo dei limiti della vista ortografica;
- gestione dei fallimenti di playback;
- log, diagnostica ed error handling;
- configurazione dati condivisa;
- codice eseguito frequentemente;
- API pubbliche sicure per Blueprint e C++.

## 2.3 Responsabilità preferibilmente Blueprint

Quando non crea svantaggi, devono poter essere gestiti in Blueprint:

- materiali dei Chrono Spawn;
- feedback `Available`, `Hovered`, `Selected`, `Occupied` e `Disabled`;
- VFX e audio;
- animazioni dei widget;
- presentazione del paradosso;
- presentazione del game over;
- effetti della selezione;
- feedback del clone fermo per playback fallito;
- composizione degli Actor finali;
- varianti estetiche per singola mappa;
- transizioni di presentazione;
- interpolazioni estetiche della camera, se non compromettono i vincoli autorevoli.

La parte Blueprint deve ricevere eventi semantici dal core, senza dover ricostruire lo stato interno interrogando molti sistemi.

## 2.4 Classi base ed estensione

Codex deve creare o adattare il core C++ in modo che, quando appropriato, sia possibile creare Blueprint derivati per:

- Chrono Spawn;
- volume/configurazione della camera;
- camera rig o camera pawn;
- player e clone;
- widget di selezione;
- widget di paradosso;
- widget di game over;
- feedback del playback fallito.

La gerarchia concreta deve seguire l'architettura già presente nel progetto.

Non introdurre classi astratte o livelli di ereditarietà non necessari.

---

# 3. Risultato funzionale del loop temporale

## 3.1 Avvio del livello

All'avvio del livello:

1. viene preparata la baseline del mondo;
2. vengono individuati e validati i Chrono Spawn;
3. viene individuato e validato il volume della camera;
4. viene attivata la camera ortografica libera;
5. non viene ancora avviata una run;
6. il player non deve necessariamente esistere;
7. tutti i Chrono Spawn validi risultano disponibili;
8. il giocatore può esplorare la mappa;
9. il giocatore seleziona il primo Chrono Spawn cliccando direttamente l'Actor nel mondo;
10. viene creato o attivato il player nello spawn selezionato;
11. al player viene assegnato il primo Temporal Index;
12. viene avviata la registrazione della prima run.

Il primo Chrono Spawn non è predefinito: deve essere scelto dal giocatore.

## 3.2 Run attiva

Durante una run:

- il player riceve input;
- le Gameplay Actions del player vengono registrate tramite `IntentReplay`;
- i cloni delle timeline precedenti eseguono i rispettivi Replay Track;
- il sistema di paradosso è attivo;
- la camera resta libera e non agganciata permanentemente al player;
- il giocatore può spostare e zoomare la camera;
- il giocatore può ricentrare la camera sul player;
- il giocatore può attivare il rewind;
- il livello può comunicare una condizione di vittoria;
- Tactical Pause può sospendere la simulazione senza bloccare la camera.

## 3.3 Rewind valido

Quando viene richiesto il rewind:

1. la richiesta viene validata;
2. richieste duplicate vengono ignorate o rifiutate;
3. l'input di gameplay viene sospeso;
4. viene terminata la registrazione corrente;
5. la run viene consolidata in un Replay Track immutabile;
6. la timeline viene associata al Chrono Spawn usato;
7. lo spawn diventa logicamente occupato;
8. viene avviata la transizione verso il reset;
9. playback e rilevamento dei paradossi vengono disattivati;
10. il mondo viene ripristinato alla baseline;
11. le entità temporali runtime vengono ricostruite;
12. il sistema entra nella fase di selezione del nuovo Chrono Spawn.

Il reset non deve modificare i Replay Track consolidati.

## 3.4 Ricostruzione delle timeline

Dopo ogni reset:

- per ogni timeline consolidata viene creato un clone;
- ogni clone appare nel Chrono Spawn utilizzato dalla propria run;
- ogni clone riceve il Temporal Index corretto;
- ogni clone riceve il Replay Track corretto;
- ogni clone è visibile;
- ogni clone è fermo;
- nessun clone ha ancora iniziato il playback;
- ogni Chrono Spawn associato a una timeline consolidata appare occupato;
- gli spawn occupati non sono selezionabili.

Il giocatore deve vedere i cloni già presenti mentre sceglie il nuovo punto di partenza.

## 3.5 Selezione del nuovo Chrono Spawn

Durante questa fase:

- il player può non essere presente;
- i cloni sono presenti ma immobili;
- il playback è fermo;
- la registrazione è ferma;
- il sistema di paradosso non è autorevole;
- la camera è completamente utilizzabile;
- il giocatore può esplorare la mappa;
- gli spawn disponibili reagiscono a hover e click;
- gli spawn occupati rifiutano la selezione;
- la nuova run non inizia finché non viene selezionato uno spawn valido.

La selezione avviene cliccando direttamente l'Actor Chrono Spawn nel mondo.

La logica di selezione non deve dipendere da un widget che elenca gli spawn.

Il sistema di input deve restare configurabile. Non hardcodare un tasto o un pulsante concreto nella logica.

## 3.6 Avvio sincronizzato della nuova run

Dopo la selezione:

1. viene creato o attivato il player nello spawn scelto;
2. al player viene assegnato il nuovo Temporal Index;
3. viene preparata la nuova registrazione;
4. i cloni vengono preparati per il playback;
5. i sistemi puzzle risultano ripristinati;
6. il sistema di paradosso viene predisposto ma resta ancora disabilitato;
7. il core verifica che tutti i partecipanti siano pronti;
8. player, registrazione, playback e rilevamento vengono attivati nello stesso momento logico;
9. la nuova run ha inizio.

Non deve essere possibile che un clone inizi il playback mentre il giocatore sta ancora scegliendo il Chrono Spawn.

---

# 4. Chrono Spawn

## 4.1 Ruolo

I Chrono Spawn sono Actor piazzati dal level designer.

Il numero di Chrono Spawn validi presenti nella mappa determina il numero massimo di timeline utilizzabili.

Ogni Chrono Spawn deve poter rappresentare almeno gli stati:

- `Available`;
- `Hovered`;
- `Selected`;
- `Occupied`;
- `Disabled`.

La rappresentazione interna concreta può seguire le convenzioni già presenti nel progetto.

## 4.2 Stato occupato

Uno spawn diventa occupato soltanto quando la run associata viene consolidata con successo.

Dopo il reset:

- il clone della timeline consolidata appare sullo spawn;
- lo spawn resta occupato;
- lo spawn non può essere selezionato;
- il feedback visivo deve poter essere personalizzato in Blueprint.

La presenza visiva del clone non sostituisce lo stato logico di occupazione.

## 4.3 Run fallita

Se la run corrente genera un paradosso:

- la sua registrazione parziale viene scartata;
- la run non viene consolidata;
- il Chrono Spawn usato dalla run fallita torna disponibile;
- le timeline consolidate precedenti restano occupate;
- dopo il reset il giocatore sceglie nuovamente un Chrono Spawn disponibile.

## 4.4 Validazione

Il sistema deve segnalare chiaramente configurazioni non valide, come:

- nessun Chrono Spawn;
- Chrono Spawn disabilitati ma ancora referenziati;
- identificatori o ordini duplicati, se utilizzati;
- riferimenti mancanti;
- più autorità del loop nella stessa mappa;
- spawn impossibili da utilizzare.

La validazione deve usare log e risultati espliciti, non fallimenti silenziosi.

---

# 5. Camera ortografica libera

## 5.1 Principio

La camera deve essere indipendente dal player Character.

Non deve essere montata sul Character né restare agganciata ad esso.

Deve continuare a esistere e funzionare quando:

- il player non è ancora stato creato;
- il player viene distrutto;
- il mondo viene resettato;
- il giocatore sta scegliendo un Chrono Spawn;
- il gioco è in Tactical Pause;
- la run è fallita;
- il livello è in una transizione.

## 5.2 Rendering

La camera deve utilizzare proiezione:

```text
Orthographic
```

Devono essere configurabili almeno:

- orientamento;
- velocità di spostamento;
- zoom iniziale;
- zoom minimo;
- zoom massimo;
- sensibilità o velocità di zoom;
- velocità o durata del ricentramento;
- centro logico della mappa.

## 5.3 Movimento libero

Il giocatore deve poter muovere la camera sul piano della mappa tramite Input Actions configurabili equivalenti a:

- avanti;
- indietro;
- sinistra;
- destra.

Il movimento deve:

- essere indipendente dal frame rate;
- funzionare durante una run;
- funzionare durante la selezione;
- funzionare durante Tactical Pause;
- rispettare sempre i limiti della mappa;
- non interferire con input di UI o selezione.

## 5.4 Ricentramento

Deve esistere una Input Action configurabile.

Comportamento:

- durante una run con player valido: spostare la camera sul player;
- quando il player non esiste: spostare la camera sul centro logico della mappa.

Il ricentramento:

- non modifica lo zoom;
- non modifica la rotazione;
- non attiva un follow permanente;
- non impedisce di riprendere immediatamente il movimento libero;
- deve rispettare i limiti della camera.

Il tasto concreto, per esempio `Space`, verrà configurato successivamente e non deve essere hardcodato.

## 5.5 Zoom

Deve esistere un input configurabile di zoom in/out, compatibile con input incrementale come la rotellina del mouse.

Lo zoom deve:

- modificare l'ampiezza della vista ortografica;
- rispettare minimo e massimo;
- funzionare durante una run;
- funzionare durante la selezione;
- funzionare durante Tactical Pause;
- non modificare orientamento o modalità della camera;
- non trasformare la camera in una follow camera;
- non mostrare zone esterne al volume consentito.

Devono esistere valori globali di default.

Il level designer deve poterli sovrascrivere per singola mappa.

---

# 6. Volume di navigazione e configurazione camera

## 6.1 Volume autorevole

Il level designer deve poter piazzare nella mappa un volume che definisce l'area navigabile della camera.

La camera non deve mai poter uscire da questo volume.

Il vincolo deve essere rispettato:

- durante il movimento manuale;
- durante lo zoom;
- durante il ricentramento;
- durante la selezione;
- durante una run;
- durante Tactical Pause;
- dopo il reset;
- durante eventuali interpolazioni.

## 6.2 Estensione visibile della camera

Il vincolo non deve limitarsi al solo punto centrale della camera.

Il sistema deve tenere conto dell'area visibile prodotta dalla proiezione ortografica.

Quindi:

- zoomando verso l'esterno, il centro può dover essere spostato verso l'interno;
- la vista non deve mostrare porzioni esterne al volume;
- un ricentramento vicino al bordo deve essere corretto;
- uno zoom incompatibile con la dimensione del volume deve essere limitato o segnalato;
- eventuali aspect ratio diversi devono essere considerati.

## 6.3 Configurazione per mappa

Il volume può essere usato anche come configurazione principale della camera della mappa.

Deve poter definire o sovrascrivere:

- centro logico della mappa;
- zoom minimo;
- zoom massimo;
- zoom iniziale;
- velocità di movimento;
- velocità dello zoom;
- durata del ricentramento;
- eventuali margini interni.

Devono esistere valori globali di default.

Gli override per-map devono essere opzionali e chiaramente configurabili dal Details Panel.

## 6.4 Validazione

Il sistema deve rilevare almeno:

- assenza del volume richiesto;
- presenza di più volumi autorevoli;
- zoom minimo e massimo invertiti;
- zoom iniziale fuori range;
- volume troppo piccolo per la configurazione;
- centro logico non compatibile con il volume.

La strategia concreta può seguire le convenzioni del progetto, ma il comportamento non deve essere ambiguo.

---

# 7. Tactical Pause

La camera deve restare utilizzabile durante Tactical Pause.

Durante la pausa il giocatore deve poter:

- muovere la camera;
- usare lo zoom;
- ricentrare la camera;
- osservare i cloni;
- osservare il livello;
- usare eventuali sistemi di selezione e overlay specifici di Paradox.

Il plugin `TacticalPause` non deve conoscere la camera specifica di Paradox.

L'integrazione deve avvenire nel modulo di gioco.

Codex deve verificare il comportamento reale della pausa e usare un meccanismo corretto per mantenere attivi input e aggiornamento della camera anche quando la simulazione globale è sospesa.

Non introdurre dipendenze inverse dal plugin generico verso il modulo di gioco.

---

# 8. Player e cloni

## 8.1 Player

Durante una run il player deve:

- ricevere input;
- eseguire Gameplay Actions;
- registrare le proprie intenzioni;
- possedere il Temporal Index della timeline corrente;
- poter richiedere il rewind;
- poter essere rilevato dai cloni precedenti;
- non essere il proprietario fisico della camera libera.

## 8.2 Clone

Un clone deve:

- possedere il Replay Track della propria timeline;
- possedere il corretto Temporal Index;
- eseguire esclusivamente il playback;
- non ricevere input;
- non usare GOAP;
- non modificare il Replay Track;
- restare fermo prima dell'avvio sincronizzato;
- rilevare entità temporali future tramite il cono del plugin Fab;
- restare un'entità temporale valida anche se il proprio playback fallisce.

## 8.3 Collisione tra entità temporali

Codex deve verificare e configurare il comportamento di collisione in modo che:

- player e cloni siano rilevabili dalla mesh dinamica del cono visivo;
- il sistema riceva overlap affidabili;
- componenti secondari dello stesso Character non producano paradossi multipli;
- eventuali collisioni fisiche tra player e cloni seguano le regole già previste dal progetto;
- collision profile e channel siano configurabili e documentati.

Non modificare collisioni globali senza verificare gli effetti sugli altri sistemi.

---

# 9. Intent Replay e playback

## 9.1 Track immutabile

Ogni timeline consolidata deve mantenere separati:

- il Replay Track registrato;
- lo stato runtime del clone che lo sta eseguendo.

Il Replay Track deve restare immutato quando:

- il clone viene ricreato;
- il mondo viene resettato;
- si verifica un paradosso;
- il playback fallisce;
- la run corrente viene ripetuta.

## 9.2 Avvio sincronizzato

Prima di iniziare la run devono essere pronti:

- mondo ripristinato;
- cloni;
- Replay Track;
- player;
- recorder;
- sistemi puzzle;
- mesh dinamiche dei coni;
- collisioni e overlap;
- sistema del paradosso.

Soltanto dopo questa barriera logica devono partire:

- controllo del player;
- registrazione;
- playback;
- rilevamento autorevole del paradosso.

## 9.3 Fallimento del playback

Se una Gameplay Action del clone fallisce:

- soltanto quel clone interrompe il playback;
- il clone resta fermo;
- gli altri cloni continuano;
- il player continua;
- la run non viene automaticamente invalidata;
- non viene generato automaticamente un paradosso;
- non viene modificato il Replay Track;
- il fallimento deve restare diagnosticabile.

Devono essere disponibili informazioni su:

- Temporal Index del clone;
- azione fallita;
- causa del fallimento;
- target o destinazione coinvolti;
- posizione nel Replay Track;
- stato dell'executor.

In futuro questo punto causerà lo switch al GOAP.

L'implementazione attuale non deve introdurre GOAP, ma deve evitare di rendere impossibile aggiungere successivamente la transizione:

```text
Intent Replay
→ Action Failure
→ GOAP
```

---

# 10. Temporal Index

Ogni player o clone temporale deve possedere un Temporal Index.

Esempio:

```text
Clone T0
Clone T1
Player T2
```

La relazione è asimmetrica.

La regola specifica di Paradox è:

```text
ObserverTemporalIndex < TargetTemporalIndex
```

Quando la condizione è vera, l'osservatore sta percependo una versione futura di se stesso e deve essere generato un paradosso.

Esempi:

```text
T0 vede T1 → paradosso
T0 vede T2 → paradosso
T1 vede T2 → paradosso

T1 vede T0 → nessun paradosso
T2 vede T0 → nessun paradosso
T2 vede T1 → nessun paradosso
```

La query deve usare il sistema di identità/relazioni già implementato nel progetto.

La regola specifica non deve essere spostata all'interno del plugin generico `EntityRelations`.

---

# 11. Rilevamento del paradosso tramite Line of Sight Dynamic Mesh

## 11.1 Vincolo principale

Il paradosso deve essere rilevato tramite **overlap della mesh dinamica generata dal plugin Fab `Line of Sight. Dynamic Mesh.`**.

La mesh dinamica del cono visivo non è soltanto una visualizzazione.

Deve costituire la geometria di overlap usata dal gameplay per individuare un'altra entità temporale.

Il flusso autorevole richiesto è:

```text
Dynamic Line-of-Sight Mesh overlap
→ individuazione dell'Actor temporale
→ recupero dei Temporal Index
→ confronto degli indici
→ eventuale paradosso
```

## 11.2 Divieti

Codex non deve sostituire il requisito con:

- line trace usati come fonte autorevole del paradosso;
- sphere trace;
- box trace;
- perception sight;
- un volume a cono separato;
- una collision shape approssimativa;
- un secondo Actor di detection indipendente dalla mesh visibile;
- una query geometrica che non utilizza l'overlap della mesh dinamica.

Questi sistemi possono esistere internamente al plugin per generare o aggiornare la mesh, ma l'evento di gameplay richiesto dal progetto deve derivare dall'overlap della mesh dinamica.

Una soluzione alternativa è accettabile soltanto se Codex dimostra, dopo aver ispezionato sorgenti e API della versione installata, che la mesh dinamica del plugin non può tecnicamente generare collisioni o overlap affidabili.

In tale caso Codex deve:

1. non procedere silenziosamente con un'approssimazione;
2. documentare il limite tecnico trovato;
3. indicare i file e le API verificate;
4. proporre la modifica minima necessaria;
5. mantenere la geometria di detection aderente alla mesh dinamica;
6. segnalare il rischio residuo.

## 11.3 Configurazione della mesh

Codex deve verificare come il plugin crea e aggiorna la mesh dinamica e configurare correttamente:

- collision enabled;
- collision response;
- overlap events;
- object type;
- collision channel;
- aggiornamento della collisione dopo la modifica runtime della mesh;
- lifecycle del componente;
- cleanup;
- comportamento durante reset e distruzione.

Non inventare API del plugin.

Ispezionare sorgenti, header, Blueprint API e documentazione installata.

## 11.4 Actor validi

Quando la mesh genera un overlap:

1. recuperare l'Actor associato al componente sovrapposto;
2. ignorare il proprietario del cono;
3. verificare che l'Actor sia un'entità temporale valida;
4. ignorare oggetti ambientali;
5. recuperare il Temporal Index dell'osservatore;
6. recuperare il Temporal Index del bersaglio;
7. verificare che entrambi gli indici siano validi;
8. applicare la regola temporale;
9. generare il paradosso soltanto se l'indice dell'osservatore è minore di quello del bersaglio.

Il solo overlap non genera automaticamente il paradosso.

L'overlap identifica il candidato; il confronto del Temporal Index decide il risultato.

## 11.5 Direzione della relazione

L'osservatore è sempre il proprietario della mesh dinamica che ha generato l'overlap.

Il bersaglio è l'Actor temporale entrato nella mesh.

Quindi:

```text
Observer = owner del cono
Target = Actor sovrapposto
```

Il confronto non deve essere invertito.

## 11.6 Duplicati

Un Character può possedere più componenti collisionabili.

Il sistema deve deduplicare il rilevamento a livello Actor-to-Actor.

Una coppia osservatore/bersaglio non deve generare più paradossi a causa di:

- capsule;
- skeletal mesh;
- child component;
- più callback nello stesso frame;
- ricostruzione della collisione della mesh;
- overlap iniziale durante lo spawn.

Dopo il primo paradosso valido della run:

- il sistema entra nello stato di fallimento;
- nuovi overlap vengono ignorati;
- la detection viene disabilitata;
- nessun secondo widget o secondo reset deve essere avviato.

## 11.7 Fasi disabilitate

Gli overlap della mesh non devono generare paradossi durante:

- inizializzazione;
- reset del mondo;
- spawn dei cloni;
- aggiornamento della collisione della mesh;
- selezione del Chrono Spawn;
- attesa dell'avvio sincronizzato;
- fade;
- schermata di paradosso;
- game over;
- vittoria;
- teardown.

La collisione può esistere, ma il risultato non deve essere considerato autorevole finché la run non è attiva.

## 11.8 Overlap già presente all'avvio

Codex deve gestire esplicitamente il caso in cui, al momento dell'attivazione della run, un'entità temporale si trovi già all'interno della mesh dinamica.

Il comportamento deve essere coerente e documentato.

La soluzione deve garantire che un overlap temporalmente valido venga rilevato anche senza un nuovo movimento successivo all'attivazione, evitando allo stesso tempo falsi positivi durante la costruzione.

Non usare delay arbitrari come soluzione principale.

## 11.9 Debug

Il debug deve permettere di osservare almeno:

- owner del cono;
- Actor candidato;
- componenti che hanno generato l'overlap;
- Temporal Index dell'osservatore;
- Temporal Index del bersaglio;
- risultato del confronto;
- motivo di un candidato ignorato;
- stato di autorizzazione della detection;
- ultimo paradosso accettato;
- overlap duplicati scartati.

Il debug deve avere controllo locale e globale, disabilitato di default.

---

# 12. Gestione del paradosso

## 12.1 Evento di paradosso

Quando l'overlap della mesh dinamica supera il check del Temporal Index, il sistema deve produrre un contesto contenente almeno:

- osservatore;
- bersaglio;
- Temporal Index dell'osservatore;
- Temporal Index del bersaglio;
- causa del paradosso;
- timeline o generazione corrente;
- eventuali informazioni diagnostiche utili.

La presentazione deve ricevere questi dati e non ricostruirli autonomamente.

## 12.2 Sequenza

Quando viene accettato un paradosso:

1. la run viene invalidata;
2. ulteriori overlap vengono ignorati;
3. input, recorder e playback vengono fermati;
4. la detection delle mesh viene disabilitata;
5. viene avviata una transizione verso il nero;
6. viene mostrato un widget con le timeline coinvolte;
7. la registrazione parziale della run viene scartata;
8. il mondo viene resettato;
9. vengono ricreati i cloni delle timeline consolidate;
10. gli spawn consolidati restano occupati;
11. lo spawn della run fallita torna disponibile;
12. il giocatore torna alla selezione del Chrono Spawn;
13. viene avviata una nuova run dopo una nuova selezione.

## 12.3 Testo suggerito

Titolo:

```text
TIMELINE COLLAPSE
```

Messaggio:

```text
T0 witnessed T1.
The past saw the future.
```

Il testo deve essere costruito dinamicamente usando i Temporal Index reali.

Esempio:

```text
T1 witnessed T3.
The past saw the future.
```

Il layout e l'animazione devono poter essere personalizzati in Blueprint.

---

# 13. Game over per esaurimento delle timeline

Il numero di Chrono Spawn determina il limite massimo di timeline consolidate.

Quando non esistono più spawn disponibili per una nuova run:

- il sistema non deve permettere un altro rewind valido;
- non deve aprire la selezione senza opzioni;
- deve entrare in uno stato distinto dal paradosso;
- deve mostrare il game over;
- deve permettere di ricominciare il livello o tornare al menu.

Testo suggerito:

```text
NO TIMELINES REMAIN

The loop has no future left.
```

Un paradosso e l'esaurimento delle timeline sono condizioni differenti:

- il paradosso scarta la run corrente e consente un nuovo tentativo usando uno spawn disponibile;
- l'esaurimento indica che non è più possibile creare una nuova timeline.

Codex deve gestire con chiarezza il caso in cui l'ultima run disponibile fallisca.

---

# 14. Stati funzionali richiesti

La struttura C++ concreta è libera, ma il sistema deve distinguere in modo autorevole almeno le seguenti fasi:

- preparazione del livello;
- selezione del Chrono Spawn;
- preparazione della run;
- run attiva;
- preparazione del rewind;
- reset del mondo;
- ricostruzione delle timeline;
- attesa dell'avvio sincronizzato;
- fallimento per paradosso;
- game over;
- completamento del livello.

Evitare combinazioni ambigue di booleani indipendenti.

Le transizioni non valide devono essere rifiutate e diagnosticate.

---

# 15. Eventi e hook di presentazione

Il core dovrebbe esporre eventi semantici equivalenti a:

- livello temporale preparato;
- selezione spawn iniziata;
- spawn evidenziato;
- spawn selezionato;
- selezione rifiutata;
- spawn diventato occupato;
- spawn tornato disponibile;
- run in preparazione;
- run iniziata;
- rewind iniziato;
- reset iniziato;
- reset completato;
- clone creato;
- clone pronto;
- playback iniziato;
- playback fallito;
- overlap temporale rilevato;
- candidato temporale ignorato;
- paradosso accettato;
- game over;
- livello completato.

Nomi e firme reali devono seguire le convenzioni del modulo esistente.

Gli eventi devono fornire dati sufficienti e non esporre stato interno mutabile.

---

# 16. Milestone di implementazione

## Milestone 0 — Investigazione

Risultato:

- istruzioni lette;
- moduli individuati;
- API reali documentate;
- plugin Fab ispezionato;
- target compilato nello stato iniziale;
- rischi e incompatibilità note identificate.

## Milestone 1 — Loop temporale minimo

Risultato:

- esiste un'autorità del loop;
- le fasi sono distinguibili;
- le transizioni sono validate;
- il reset può essere richiesto e completato;
- errori e stati invalidi sono osservabili.

## Milestone 2 — Chrono Spawn

Risultato:

- il level designer può piazzare gli spawn;
- il giocatore sceglie anche il primo spawn;
- la selezione avviene cliccando l'Actor nel mondo;
- gli stati visivi possono essere personalizzati;
- gli spawn occupati non sono selezionabili;
- il limite della mappa dipende dal numero degli spawn.

## Milestone 3 — Registrazione e consolidamento

Risultato:

- la run del player viene registrata;
- il rewind termina la registrazione;
- la timeline viene consolidata;
- il Replay Track resta immutabile;
- lo spawn viene occupato soltanto dopo consolidamento riuscito.

## Milestone 4 — Reset e cloni

Risultato:

- il mondo torna alla baseline;
- i cloni vengono ricreati nei vecchi spawn;
- i Temporal Index sono corretti;
- i cloni sono visibili e fermi;
- gli spawn consolidati risultano occupati.

## Milestone 5 — Camera ortografica

Risultato:

- camera indipendente dal player;
- movimento libero;
- zoom;
- ricentramento;
- utilizzo senza player;
- funzionamento durante Tactical Pause;
- aggiornamento sicuro durante reset e transizioni.

## Milestone 6 — Volume camera

Risultato:

- il level designer può piazzare il volume;
- la camera non può uscire;
- l'area visibile resta nel volume;
- gli override per-map funzionano;
- configurazioni incompatibili vengono segnalate.

## Milestone 7 — Selezione e avvio sincronizzato

Risultato:

- dopo il reset i cloni restano fermi;
- il giocatore esplora la mappa;
- sceglie un nuovo spawn;
- il player viene creato nello spawn;
- playback, recorder e detection partono insieme.

## Milestone 8 — Playback

Risultato:

- ogni clone esegue il proprio Track;
- nessun GOAP viene usato;
- un fallimento ferma soltanto il clone;
- gli altri partecipanti continuano;
- il fallimento è diagnosticabile.

## Milestone 9 — Overlap della dynamic mesh

Risultato:

- la mesh dinamica del plugin genera overlap;
- collisione e aggiornamento runtime sono validati;
- i candidati vengono filtrati;
- gli overlap sono deduplicati per Actor;
- nessun trace o volume approssimativo sostituisce la mesh come fonte autorevole.

## Milestone 10 — Check Temporal Index e paradosso

Risultato:

- l'owner della mesh è l'osservatore;
- l'Actor sovrapposto è il bersaglio;
- entrambi gli indici vengono recuperati;
- il paradosso avviene soltanto quando `ObserverIndex < TargetIndex`;
- gli eventi duplicati vengono ignorati;
- viene mostrata la schermata `TIMELINE COLLAPSE`.

## Milestone 11 — Retry e game over

Risultato:

- la run fallita viene scartata;
- lo spawn della run fallita torna disponibile;
- le timeline consolidate restano;
- il player sceglie nuovamente uno spawn;
- l'esaurimento degli spawn produce un game over distinto.

## Milestone 12 — Debug, test e documentazione

Risultato:

- debug locale e globale;
- test dei flussi principali e dei fallimenti;
- documentazione aggiornata;
- target compilato;
- diff finale privo di modifiche non correlate.

---

# 17. Criteri di accettazione

L'implementazione è accettabile quando sono verificati almeno i seguenti scenari.

## 17.1 Avvio e selezione

1. Il livello si apre senza player attivo.
2. La camera può esplorare la mappa.
3. Il giocatore sceglie il primo Chrono Spawn cliccandolo.
4. Uno spawn disabilitato non viene accettato.
5. La prima run inizia soltanto dopo una selezione valida.

## 17.2 Rewind

6. Il player esegue azioni registrabili.
7. Il rewind consolida la run.
8. Il mondo viene resettato.
9. Il clone appare nel vecchio spawn.
10. Lo spawn appare occupato.
11. Il clone resta fermo.
12. La camera resta utilizzabile.
13. Il giocatore sceglie un altro spawn.
14. Player, recorder e cloni partono insieme.

## 17.3 Camera

15. La camera usa proiezione ortografica.
16. La camera non è agganciata al Character.
17. Il movimento rispetta il volume.
18. Lo zoom rispetta minimo e massimo.
19. Gli override per-map sostituiscono i default.
20. Lo zoom out non mostra zone esterne al volume.
21. Il ricentramento durante una run porta la camera sul player.
22. Il ricentramento non cambia lo zoom.
23. Dopo il ricentramento la camera resta libera.
24. Movimento, zoom e ricentramento funzionano durante Tactical Pause.

## 17.4 Overlap e Temporal Index

25. Il cono visivo usa la mesh dinamica del plugin.
26. La mesh dinamica genera overlap con le entità temporali.
27. L'overlap identifica l'Actor bersaglio.
28. Il sistema recupera il Temporal Index dell'owner del cono.
29. Il sistema recupera il Temporal Index del bersaglio.
30. `T0` che sovrappone `T1` genera un paradosso.
31. `T0` che sovrappone `T2` genera un paradosso.
32. `T1` che sovrappone `T0` non genera un paradosso.
33. Il player corrente che sovrappone un clone precedente non genera un paradosso.
34. Più componenti dello stesso bersaglio producono un solo evento logico.
35. Un overlap durante la costruzione non genera un falso paradosso.
36. Un Actor già dentro la mesh all'avvio della run viene gestito correttamente.
37. Dopo il primo paradosso non vengono avviati reset o widget duplicati.
38. Il paradosso non è basato su un trace o su un volume separato.

## 17.5 Paradosso

39. Il gameplay viene fermato.
40. Compare il fade verso il nero.
41. Il widget mostra gli indici reali.
42. La registrazione fallita viene scartata.
43. Lo spawn della run fallita torna disponibile.
44. I cloni consolidati vengono ricreati.
45. Il giocatore sceglie un nuovo Chrono Spawn.
46. Il Temporal Index della nuova run segue la regola definita dal sistema.

## 17.6 Playback fallito

47. Un'azione impossibile ferma soltanto il clone coinvolto.
48. Gli altri cloni continuano.
49. Il player continua.
50. Il clone fermo resta rilevabile dalla mesh dinamica.
51. Il Replay Track non viene modificato.
52. Nessun GOAP viene attivato.

## 17.7 Game over

53. Tutti gli spawn consolidati risultano occupati.
54. Non è possibile iniziare una nuova timeline senza spawn disponibili.
55. Il sistema mostra `NO TIMELINES REMAIN`.
56. Il game over è distinto dal paradosso.
57. Il restart elimina tutto lo stato temporale residuo.

---

# 18. Test minimi raccomandati

Creare test automatici o procedure ripetibili per:

- validazione della configurazione della mappa;
- prima selezione del Chrono Spawn;
- occupazione dopo consolidamento;
- liberazione dopo paradosso;
- ordine delle timeline;
- assegnazione del Temporal Index;
- reset ripetuti;
- avvio sincronizzato;
- fallimento del recorder;
- fallimento del reset;
- fallimento dello spawn;
- Track mancante;
- playback fallito;
- mesh dinamica senza collisione;
- collisione non aggiornata dopo una modifica della mesh;
- overlap duplicato;
- overlap iniziale;
- overlap durante reset;
- target privo di identità temporale;
- indici invalidi;
- confronto asimmetrico;
- paradosso accettato una sola volta;
- camera ai bordi;
- zoom massimo e minimo;
- aspect ratio differenti;
- camera durante Tactical Pause;
- esaurimento Chrono Spawn;
- restart completo.

---

# 19. Debug e logging

Il modulo deve usare la propria log category e le proprie macro di logging.

Non lasciare `LogTemp` nel codice finale.

Il debug deve poter mostrare almeno:

- fase corrente del loop;
- timeline consolidate;
- Temporal Index di player e cloni;
- Chrono Spawn disponibili;
- Chrono Spawn occupati;
- spawn hovered e selezionato;
- Replay Track associato;
- stato di playback;
- clone fermo per errore;
- volume della camera;
- posizione camera;
- zoom;
- limiti calcolati;
- owner della dynamic mesh;
- overlap attivi;
- Actor candidato;
- confronto degli indici;
- motivo di accettazione o rifiuto;
- ultimo paradosso;
- stato di autorizzazione della detection.

Ogni visual debug deve avere:

- controllo locale;
- controllo globale del modulo;
- stato disabilitato di default;
- costo trascurabile quando disabilitato.

I log ad alta frequenza devono essere protetti o rate-limited.

Le parti potenzialmente costose devono essere misurabili tramite Unreal Insights quando appropriato.

---

# 20. Documentazione richiesta

Prima di concludere il task, Codex deve creare o aggiornare il `Docs` folder del modulo.

La documentazione deve spiegare:

- scopo del sistema;
- setup della mappa;
- setup dei Chrono Spawn;
- setup del volume camera;
- valori globali e override;
- input richiesti;
- selezione degli spawn;
- sequenza del rewind;
- ricostruzione dei cloni;
- avvio sincronizzato;
- registrazione e playback;
- fallimento del playback;
- configurazione della mesh dinamica;
- collision channel;
- overlap;
- check del Temporal Index;
- gestione del paradosso;
- game over;
- Tactical Pause;
- Blueprint extension points;
- debug;
- troubleshooting.

Deve essere inclusa una sezione specifica che chiarisca:

> Il rilevamento autorevole del paradosso deriva dall'overlap della mesh dinamica generata da `Line of Sight. Dynamic Mesh.`. L'overlap produce un candidato e il confronto del Temporal Index decide se l'evento è un paradosso.

---

# 21. Definition of Done

Il task è completato soltanto quando:

- le istruzioni locali sono state lette;
- l'implementazione esistente è stata ispezionata;
- le API del plugin Fab sono state verificate;
- la mesh dinamica genera overlap validi;
- il check del Temporal Index è funzionante;
- il gameplay loop è giocabile dall'inizio al game over;
- la camera funziona in run, selezione e Tactical Pause;
- i cloni eseguono solo Intent Replay;
- il fallimento del playback ferma soltanto il clone;
- nessun GOAP è stato introdotto;
- errori e stati invalidi sono osservabili;
- il target interessato compila;
- i flussi principali sono stati validati;
- i failure path sono stati considerati;
- la documentazione è aggiornata;
- il diff non contiene modifiche non correlate;
- non restano debug temporanei o codice commentato.
