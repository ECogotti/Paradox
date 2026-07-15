# **PARADOX**

## **Game Design Document**

**Versione:** 0.3  
 **Stato:** Concept / Pre-produzione  
 **Engine:** Unreal Engine 5  
 **Genere:** Puzzle stealth temporale  
 **Visuale:** Top-down inclinata  
 **Modalità:** Single player  
 **Struttura:** Livelli handcrafted  
 **Direzione artistica:** 3D voxel  
 **Target iniziale:** PC, con possibilità di porting su console e dispositivi touch

---

# **1\. High Concept**

**Paradox è uno stealth in cui il giocatore non deve essere visto da se stesso.**

Il giocatore deve completare una mappa raggiungendo un obiettivo ambientale, come aprire una porta, attivare un dispositivo o arrivare dal punto A al punto B.

Quando una singola run non è sufficiente, il giocatore torna indietro nel tempo e ricomincia il livello da un'altra posizione. La run appena conclusa viene trasformata in un clone temporale che cerca di ripetere gli obiettivi e le azioni precedentemente eseguite.

Ogni nuova run aggiunge quindi una nuova versione del personaggio alla mappa.

I cloni sono contemporaneamente:

* strumenti necessari alla soluzione;  
* attori che modificano l'ambiente;  
* sorgenti di rumore;  
* osservatori stealth;  
* potenziali cause di paradosso.

Un paradosso si verifica quando una versione passata vede una versione di se stessa proveniente dal futuro.

---

# **2\. Unique Selling Proposition**

**Ogni tentativo crea una nuova guardia che ripete le tue azioni passate. Per completare il livello devi collaborare con le tue precedenti timeline senza permettere al passato di vedere il futuro.**

La seconda componente distintiva è rappresentata dall'AI contestuale:

**I cloni non riproducono semplicemente una registrazione: perseguono gli obiettivi della run originale, ma possono essere distratti, perdere i propri target e riprogrammare il comportamento in base al nuovo stato della mappa.**

---

# **3\. Player Fantasy**

Il giocatore deve sentirsi contemporaneamente:

* pianificatore;  
* infiltrato;  
* autore dei percorsi delle guardie;  
* collaboratore delle proprie versioni precedenti;  
* manipolatore di una catena temporale;  
* causa dei problemi che dovrà successivamente risolvere.

La fantasia centrale è:

**Costruire progressivamente il sistema stealth che dovrai attraversare.**

Ogni run rende possibile una parte della soluzione, ma introduce una nuova versione di te che non deve vedere ciò che appartiene al suo futuro.

---

# **4\. Design Pillars**

## **4.1 Stealth contro se stessi**

Le uniche AI previste nella prima versione sono i cloni temporali.

Non esistono guardie tradizionali o nemici esterni.

Il rischio stealth nasce dai percorsi, dai coni visivi e dai cambiamenti comportamentali prodotti dalle versioni precedenti del giocatore.

## **4.2 Pianificazione temporale**

Ogni run deve essere progettata pensando contemporaneamente:

* al risultato immediato;  
* al clone che quella run genererà;  
* alle timeline future;  
* ai percorsi che diventeranno pericolosi;  
* ai rumori e alle interazioni che verranno ripetuti.

## **4.3 Emergenza controllata**

I cloni possono:

* percepire rumori;  
* deviare dal proprio percorso;  
* cambiare orientamento;  
* perdere un obiettivo;  
* cercare un'alternativa;  
* modificare indirettamente il comportamento di altri cloni.

I risultati devono poter sorprendere il giocatore, ma devono sempre derivare da regole leggibili e deterministiche.

## **4.4 Informazione accessibile**

Il gioco può generare situazioni complesse, ma il giocatore deve poter fermare il tempo e analizzarle.

Percorsi, indici temporali, coni visivi, relazioni di paradosso e cambi di Goal devono essere consultabili attraverso una modalità di pausa tattica.

## **4.5 Basso scope produttivo**

La profondità deve derivare dall'interazione tra pochi sistemi riutilizzabili, non dalla quantità di contenuti.

Il gioco utilizza:

* livelli costruiti a mano;  
* asset voxel;  
* static mesh;  
* animazioni minime;  
* nessun doppiaggio;  
* nessuna skeletal mesh;  
* nessuna generazione procedurale.

---

# **5\. Obiettivo del giocatore**

Ogni mappa possiede un **Main Goal** chiaramente definito.

Esempi:

* raggiungere l'uscita;  
* andare dal punto A al punto B;  
* aprire una porta e attraversarla;  
* riattivare un ascensore;  
* alimentare un dispositivo;  
* trasportare un oggetto;  
* completare una sequenza di interazioni;  
* raggiungere e utilizzare una console.

Il livello viene completato quando il personaggio attualmente controllato soddisfa il Main Goal.

I cloni possono contribuire alla soluzione:

* mantenendo attiva una piastra;  
* utilizzando un terminale;  
* aprendo una porta;  
* spostando un oggetto;  
* producendo rumore;  
* distraendo un clone più antico;  
* modificando il contesto per le timeline successive.

---

# **6\. Condizioni di fallimento**

La condizione di fallimento principale è il **paradosso visivo**.

Un paradosso si verifica quando:

1. un clone individua un'altra versione del personaggio;  
2. la versione osservata possiede un indice temporale maggiore;  
3. la linea visiva non è ostruita;  
4. il rilevamento supera l'eventuale tempo minimo previsto.

Possono inoltre esistere fallimenti ambientali specifici, come:

* raggiungere una zona letale;  
* distruggere un elemento indispensabile senza alternative;  
* esaurire i rewind disponibili;  
* rendere il Main Goal irraggiungibile.

Un fallimento riavvia la timeline corrente, ma non cancella automaticamente i cloni consolidati nelle run precedenti.

---

# **7\. Core Gameplay Loop**

1. Il giocatore osserva la mappa.  
2. Mette eventualmente in pausa il tempo.  
3. Analizza cloni, percorsi, coni visivi e relazioni temporali.  
4. Riprende il tempo.  
5. Seleziona una cella della griglia.  
6. Il personaggio raggiunge la destinazione tramite pathfinding.  
7. Interagisce con gli elementi ambientali.  
8. Costruisce una run utile per il presente o per una timeline futura.  
9. Attiva il rewind.  
10. La run appena conclusa viene trasformata in un clone.  
11. Il mondo ritorna allo stato iniziale.  
12. Tutti i cloni consolidati iniziano a perseguire i propri obiettivi.  
13. Il giocatore ricomincia da un nuovo Chrono Spawn.  
14. Evita che le versioni passate vedano quelle future.  
15. Sfrutta azioni, distrazioni e cambi di contesto.  
16. Completa il Main Goal oppure genera una nuova timeline.

Il loop può essere sintetizzato come:

**Osserva → Pianifica → Esegui → Registra → Riavvolgi → Convivi con il tuo passato → Completa l'obiettivo**

---

# **8\. Camera**

## **8.1 Inquadratura**

La camera utilizza una visuale top-down inclinata tra i **45° e i 60°**.

L'angolazione iniziale consigliata è di circa **55°**.

Questa configurazione deve permettere di:

* leggere la griglia;  
* comprendere la profondità;  
* distinguere muri e ostacoli;  
* visualizzare i coni visivi;  
* riconoscere i cloni;  
* valorizzare gli asset voxel.

## **8.2 Comportamento**

Per la vertical slice la camera mantiene:

* inclinazione fissa;  
* rotazione orizzontale fissa;  
* distanza controllata;  
* movimento morbido sul giocatore.

Durante la pausa tattica, il giocatore può:

* spostare liberamente la camera entro i limiti della mappa;  
* effettuare uno zoom limitato;  
* selezionare cloni ed elementi ambientali.

La rotazione manuale della camera non è necessaria nella prima versione.

## **8.3 Occlusione**

Gli elementi che coprono il giocatore o i cloni possono:

* diventare trasparenti;  
* utilizzare una dissolvenza;  
* essere temporaneamente nascosti dalla camera.

La trasparenza grafica non modifica:

* collisioni;  
* propagazione del rumore;  
* calcolo della linea visiva dei cloni.

---

# **9\. Movimento su griglia**

## **9.1 Struttura**

Ogni livello è suddiviso in celle.

Una cella può essere:

* libera;  
* bloccata;  
* occupata da un elemento ambientale;  
* interagibile;  
* rumorosa;  
* pericolosa;  
* temporaneamente non attraversabile.

La griglia viene utilizzata per:

* movimento;  
* pathfinding;  
* interazioni;  
* propagazione del rumore;  
* registrazione dei percorsi;  
* valutazione della raggiungibilità.

## **9.2 Controllo**

Il giocatore clicca o seleziona una cella raggiungibile.

Il sistema:

1. identifica la destinazione;  
2. calcola il percorso;  
3. mostra un'anteprima;  
4. muove il personaggio lungo il path.

Il giocatore non controlla direttamente ogni passo del personaggio.

## **9.3 Pathfinding**

Il movimento utilizza un algoritmo A\* o equivalente sulla griglia.

Il percorso considera:

* celle bloccate;  
* porte aperte o chiuse;  
* ostacoli;  
* oggetti spostabili;  
* modifiche ambientali;  
* costi di attraversamento.

Se il percorso viene modificato durante il movimento:

1. il personaggio prova a ricalcolarlo;  
2. se esiste una nuova strada, continua;  
3. se la destinazione è irraggiungibile, si ferma;  
4. il Goal corrente viene rivalutato.

## **9.4 Movimento visivo**

Il personaggio è una static mesh voxel.

La locomozione può utilizzare:

* traslazione;  
* piccole rotazioni;  
* oscillazioni;  
* squash and stretch;  
* pochi frame voxel opzionali.

Non è previsto l'uso di:

* skeletal mesh;  
* Animation Blueprint;  
* framework di locomozione tradizionali.

---

# **10\. Sistema delle run temporali**

## **10.1 Run**

Una Run è il periodo compreso tra:

* l'ingresso nella timeline;  
* il rewind;  
* il completamento della mappa;  
* il fallimento.

Durante la Run vengono registrati:

* destinazioni selezionate;  
* percorsi principali;  
* interazioni;  
* target;  
* attese;  
* rumori prodotti;  
* Goal perseguiti;  
* condizioni contestuali rilevanti.

## **10.2 Rewind**

Quando il giocatore attiva il rewind:

1. la run viene consolidata;  
2. viene creato un nuovo clone;  
3. al clone viene assegnato un Temporal Index;  
4. la mappa ritorna allo stato baseline;  
5. tutti i cloni consolidati vengono ripristinati;  
6. il giocatore entra da un nuovo Chrono Spawn;  
7. la simulazione ricomincia.

Ogni livello definisce:

* numero massimo di rewind;  
* Chrono Spawn disponibili;  
* ordine di utilizzo degli spawn;  
* condizioni necessarie per attivare il rewind.

## **10.3 Stato baseline**

A ogni nuova timeline vengono ripristinati:

* porte;  
* terminali;  
* interruttori;  
* oggetti;  
* elementi distruttibili;  
* dispositivi ambientali.

Durante la timeline corrente, però, tutti gli attori condividono lo stesso stato del mondo.

Le azioni di un clone possono quindi modificare il contesto degli altri cloni.

---

# **11\. Identità temporale**

## **11.1 Temporal Index**

Ogni versione del personaggio possiede un indice temporale univoco.

| Entità | Indice |
| ----- | ----- |
| Primo clone | T0 |
| Secondo clone | T1 |
| Terzo clone | T2 |
| Clone successivo | Tn |
| Giocatore corrente | Indice più alto presente |

L'indice rappresenta l'ordine di generazione, non una categoria cromatica.

Il sistema deve poter utilizzare qualsiasi numero di indici senza dipendere da una palette di colori.

Il limite di cloni di una mappa viene determinato esclusivamente da:

* leggibilità;  
* complessità del puzzle;  
* performance;  
* scope del livello.

## **11.2 Memoria temporale**

Ogni clone considera coerente la presenza delle versioni con indice inferiore.

Durante la run che ha generato T2 esistevano già:

* T0;  
* T1.

T2 può quindi osservare T0 e T1 senza generare un paradosso.

T2 non può invece osservare:

* T3;  
* T4;  
* il giocatore corrente quando possiede un indice superiore.

## **11.3 Regola sintetica**

**Ogni clone ricorda quelli venuti prima di lui. Non deve vedere quelli venuti dopo.**

Oppure:

**Il passato non può vedere il futuro.**

---

# **12\. Sistema dei cloni**

## **12.1 Clone come agente contestuale**

Un clone non riproduce frame per frame la trasformazione del giocatore.

La run viene convertita in una sequenza di:

* Goal;  
* destinazioni;  
* target;  
* interazioni;  
* condizioni;  
* attese.

Il clone cerca di riprodurre l'intenzione della run originale all'interno dello stato attuale della mappa.

## **12.2 Main Path**

Il **Main Path** è la sequenza principale registrata durante la run.

Esempio:

1. raggiungi la cella B12;  
2. utilizza il terminale A;  
3. attraversa la porta nord;  
4. attiva la piastra;  
5. raggiungi la destinazione finale.

Il clone mantiene il Main Path come comportamento prioritario finché i Goal rimangono validi.

## **12.3 Interazione fisica tra versioni**

Player e cloni:

* non si bloccano reciprocamente sulla griglia;  
* non vengono considerati ostacoli dal pathfinding;  
* possono attraversarsi;  
* non bloccano reciprocamente i coni visivi.

Quando due versioni occupano temporaneamente la stessa posizione, viene utilizzato un effetto visivo di interferenza temporale.

Questa scelta riduce:

* deadlock;  
* desincronizzazioni;  
* deviazioni non intenzionali;  
* difficoltà di authoring

Le versioni continuano comunque a rilevarsi attraverso il sistema visivo.

---

# **13\. AI basata su obiettivi e contesto**

## **13.1 Struttura di un Goal**

Ogni Goal contiene:

* tipo;  
* target;  
* destinazione;  
* precondizioni;  
* risultato atteso;  
* priorità;  
* stato di validità;  
* eventuali alternative.

## **13.2 Gerarchia decisionale**

La priorità base del clone è:

1. reagire a una distrazione valida;  
2. terminare l'investigazione;  
3. riprendere il Main Path;  
4. completare il prossimo Goal registrato;  
5. cercare un'alternativa contestuale;  
6. perseguire direttamente il Main Goal della mappa.

## **13.3 Invalidazione di un Goal**

Un Goal diventa invalido quando:

* il target è stato distrutto;  
* il target non esiste;  
* l'interazione non è disponibile;  
* il percorso è permanentemente bloccato;  
* l'obiettivo è già stato completato;  
* le precondizioni non possono essere soddisfatte.

Esempio:

Il Clone T0 deve utilizzare `PC_A`.

Una versione successiva distrugge `PC_A`.

Quando T0 raggiunge quel Goal:

1. verifica che il target non sia valido;  
2. annulla l'interazione;  
3. cerca un terminale alternativo;  
4. se non esistono alternative, rivaluta il Main Goal;  
5. calcola un nuovo comportamento.

## **13.4 Alternative contestuali**

Le alternative sono definite dal level designer.

Il clone non utilizza un planner completamente libero.

Può scegliere tra possibilità controllate, come:

* utilizzare un terminale alternativo;  
* raggiungere un interruttore;  
* cercare un altro accesso;  
* attivare una piastra;  
* recuperare un oggetto;  
* dirigersi verso il Main Goal.

## **13.5 Replanning**

Quando il clone cambia piano:

* il vecchio Goal viene marcato come invalido;  
* viene selezionato il nuovo Goal;  
* viene calcolato un nuovo path;  
* il cambiamento viene comunicato al giocatore;  
* il clone prosegue secondo il nuovo contesto.

Il replanning può produrre:

* nuovi percorsi;  
* nuovi orientamenti;  
* nuovi coni visivi;  
* rumori imprevisti;  
* ulteriori invalidazioni;  
* chain reaction temporali.

---

# **14\. Sistema di paradosso visivo**

## **14.1 Regola**

Un osservatore genera un paradosso quando vede una versione con indice maggiore.

Formalmente:

**Ti genera un paradosso vedendo Tj quando `j > i`.**

## **14.2 Relazione asimmetrica**

| Osservatore | Bersaglio | Risultato |
| ----- | ----- | ----- |
| T0 | T1 | Paradosso |
| T0 | T2 | Paradosso |
| T1 | T0 | Sicuro |
| T1 | T2 | Paradosso |
| T2 | T0 | Sicuro |
| T2 | T1 | Sicuro |
| T2 | T3 | Paradosso |

La relazione non è reciproca.

T1 può vedere T0 perché T0 esisteva già nella timeline di T1.

T0 non può vedere T1 perché T1 appartiene al futuro di T0.

## **14.3 Giocatore corrente**

Il giocatore corrente possiede sempre l'indice maggiore.

Può osservare tutti i cloni senza conseguenze.

Qualsiasi clone che vede il giocatore corrente genera invece un paradosso.

## **14.4 Cono visivo**

Ogni clone possiede un cono visivo definito da:

* posizione;  
* orientamento;  
* ampiezza;  
* distanza;  
* ostacoli ambientali.

Il cono non è universalmente pericoloso.

La pericolosità dipende dall'indice dell'entità che lo attraversa.

Il cono di T1 è pericoloso per:

* T2;  
* T3;  
* T4;  
* il giocatore corrente se successivo.

Non è pericoloso per:

* T0.

## **14.5 Rilevamento**

Il paradosso richiede:

1. bersaglio dentro il cono;  
2. linea visiva libera;  
3. indice del bersaglio maggiore;  
4. eventuale permanenza minima nel cono.

Per la vertical slice può essere previsto un breve tempo di rilevamento per offrire:

* leggibilità;  
* possibilità di reazione;  
* feedback visivo.

## **14.6 Paradossi fra cloni**

Un clone più antico può generare un paradosso vedendo un clone più recente.

Esempio:

1. T2 produce un rumore.  
2. T0 si gira verso il rumore.  
3. Il nuovo cono di T0 intercetta T1.  
4. T0 vede T1.  
5. Viene generato un paradosso.

Il giocatore deve proteggere non solo se stesso, ma anche le versioni più recenti dai coni delle versioni più antiche.

## **14.7 Feedback di fallimento**

Quando avviene un paradosso:

1. il tempo rallenta;  
2. osservatore e bersaglio vengono evidenziati;  
3. il resto della scena viene desaturato;  
4. appare una linea tra i due;  
5. vengono mostrati gli indici;  
6. appare un messaggio esplicativo;  
7. la timeline collassa.

Esempio:

**PARADOSSO — T0 HA VISTO T2**

Oppure:

**T0 NON DOVREBBE POTER VEDERE T2**

---

# **15\. Sistema di rumore**

## **15.1 Sorgenti**

I rumori possono essere generati da:

* superfici rumorose;  
* porte;  
* terminali;  
* macchinari;  
* oggetti spostati;  
* elementi distrutti;  
* emettitori acustici;  
* azioni del giocatore;  
* azioni dei cloni.

Ogni rumore contiene:

* posizione;  
* intensità;  
* raggio;  
* durata;  
* categoria;  
* indice temporale della sorgente.

## **15.2 Regola temporale**

Una versione può distrarre esclusivamente versioni più antiche.

Formalmente:

**Una sorgente Tj può distrarre un ascoltatore Ti soltanto quando `j > i`.**

Esempi:

* T3 può distrarre T0, T1 e T2;  
* T2 può distrarre T0 e T1;  
* T1 può distrarre T0;  
* T0 non può distrarre versioni successive.

## **15.3 Motivazione causale**

Il rumore prodotto da una versione precedente faceva già parte del contesto nel quale la run successiva è stata registrata.

Non costituisce quindi una nuova informazione per il clone futuro.

Un rumore proveniente da una versione successiva, invece, non esisteva nella timeline originale del clone antico e può alterarne il comportamento.

## **15.4 Propagazione**

Il rumore si propaga sulla griglia.

Pareti e porte possono:

* bloccarlo;  
* attenuarlo;  
* deviarne il percorso.

La propagazione deve essere deterministica e visualizzabile durante la pausa tattica.

## **15.5 Distrazione**

Quando un clone percepisce un rumore valido:

1. sospende il Main Path;  
2. salva il Goal corrente;  
3. genera `Investigate Noise`;  
4. raggiunge la zona di investigazione;  
5. osserva l'area;  
6. rivaluta il contesto;  
7. riprende il Main Path oppure effettua replanning.

## **15.6 Priorità degli stimoli**

Se sono presenti più rumori, il clone li valuta in base a:

* intensità;  
* distanza;  
* categoria;  
* momento di generazione;  
* priorità definita dal livello.

La scelta deve essere deterministica.

---

# **16\. Chain Reaction temporali**

Le chain reaction costituiscono una meccanica emergente fondamentale.

## **16.1 Esempio**

Sono presenti:

* T0;  
* T1;  
* T2;  
* Player T3.

### **Sequenza**

1. T2 attiva un emettitore acustico.  
2. T0 percepisce il rumore.  
3. T0 abbandona temporaneamente il Main Path.  
4. T0 cambia orientamento.  
5. Il nuovo cono di T0 attraversa il percorso di T1.  
6. T1 entra nel cono di T0.  
7. T0 vede una versione futura.  
8. Si genera un paradosso.

## **16.2 Esempio con replanning**

1. T3 distrugge un terminale.  
2. T0 raggiunge il terminale previsto dal proprio Main Path.  
3. Il Goal di T0 diventa invalido.  
4. T0 sceglie un terminale alternativo.  
5. T0 cambia percorso.  
6. Il nuovo percorso attraversa una superficie rumorosa.  
7. Il rumore distrae un altro clone antico.  
8. Il clone distratto cambia orientamento.  
9. Un nuovo cono visivo minaccia T2.  
10. Il giocatore deve modificare il proprio piano.

La soluzione non deriva quindi esclusivamente dalle azioni registrate, ma dalle conseguenze contestuali prodotte dalle timeline sovrapposte.

---

# **17\. Pausa tattica**

## **17.1 Funzione**

Il giocatore può mettere in pausa il tempo in qualsiasi momento consentito dal livello.

La pausa blocca completamente:

* movimento;  
* AI;  
* pathfinding in esecuzione;  
* animazioni;  
* timer;  
* rumori;  
* interazioni;  
* dispositivi ambientali;  
* rilevamento visivo;  
* progressione del paradosso.

La pausa non è:

* un rewind;  
* una nuova timeline;  
* un'azione registrata;  
* una modifica dello stato temporale.

## **17.2 Scopo**

La pausa serve a:

* comprendere la situazione;  
* selezionare i cloni;  
* leggere il loro Temporal Index;  
* visualizzare il Goal corrente;  
* osservare il Main Path;  
* identificare le relazioni di paradosso;  
* controllare i coni visivi;  
* analizzare i rumori;  
* comprendere un replanning.

## **17.3 Interazioni consentite**

Durante la pausa il giocatore può:

* spostare la camera;  
* effettuare zoom;  
* selezionare cloni;  
* selezionare il player;  
* selezionare elementi ambientali;  
* mostrare o nascondere percorsi;  
* consultare informazioni;  
* evidenziare coni visivi;  
* visualizzare la propagazione dei rumori.

Durante la prima versione non può:

* muovere il personaggio;  
* interagire con oggetti;  
* modificare i Goal;  
* impartire ordini ai cloni;  
* creare nuovi path;  
* attivare il rewind.

La pausa è quindi uno strumento di analisi, non un sistema di comando tattico.

## **17.4 Selezione di un clone**

Quando il giocatore seleziona un clone Tx, il sistema considera Tx come **bersaglio analizzato**.

Gli altri cloni vengono classificati in due categorie.

### **Evidenziazione rossa**

Vengono evidenziati temporaneamente in rosso i cloni con indice inferiore:

`Ti < Tx`

Questi cloni genererebbero un paradosso se vedessero Tx.

Esempio selezionando T3:

* T0: rosso;  
* T1: rosso;  
* T2: rosso;  
* T4: blu.

### **Evidenziazione blu**

Vengono evidenziati temporaneamente in blu i cloni con indice maggiore:

`Ti > Tx`

Questi cloni possono vedere Tx senza generare un paradosso.

### **Clone selezionato**

Il clone selezionato rimane:

* bianco;  
* neutro;  
* evidenziato tramite contorno;  
* accompagnato dal proprio badge temporale.

## **17.5 Colori contestuali**

Rosso e blu sono utilizzati esclusivamente durante l'analisi.

Non identificano permanentemente i cloni.

Quando il giocatore chiude la pausa o deseleziona il clone:

* tutte le evidenziazioni scompaiono;  
* i cloni ritornano al materiale standard;  
* l'identità continua a essere comunicata tramite indice.

Questo permette di avere un numero di cloni non limitato dalla quantità di colori distinguibili.

## **17.6 Coni durante la selezione**

Selezionando T3:

* i coni di T0, T1 e T2 diventano rossi;  
* rappresentano i coni pericolosi per T3;  
* i coni di cloni successivi diventano blu o vengono attenuati;  
* i coni irrilevanti possono essere nascosti.

Il giocatore vede quindi immediatamente:

**Da chi deve essere nascosto il clone selezionato.**

## **17.7 Informazioni mostrate**

Il pannello di un clone può mostrare:

* Temporal Index;  
* Goal corrente;  
* prossimo Goal;  
* destinazione;  
* stato del Main Path;  
* stato di distrazione;  
* rumore investigato;  
* target corrente;  
* eventuale Goal invalido;  
* motivo del replanning.

Esempio:

**T2**

* Stato: Main Path  
* Goal: Usa Terminale B  
* Destinazione: Cella F12  
* Può vedere senza paradosso: T0, T1  
* Non deve essere visto da: T0, T1  
* Può essere visto in sicurezza da: T3, T4  
* Distrazione: Nessuna

## **17.8 Selezione del giocatore corrente**

Se viene selezionato il player:

* tutti i cloni vengono evidenziati in rosso;  
* qualsiasi clone può generare un paradosso vedendo il player;  
* tutti i relativi coni vengono mostrati come pericolosi.

Il giocatore corrente, essendo la versione più recente, non possiede osservatori temporalmente successivi.

---

# **18\. Interfaccia e comunicazione visiva**

## **18.1 Nessun color coding permanente**

I cloni non utilizzano colori diversi per indicare l'indice temporale.

Tutti condividono:

* modello;  
* materiale di base;  
* palette;  
* stile visivo.

Possono differire leggermente dal player tramite:

* trasparenza;  
* dithering;  
* effetto ghost;  
* scia temporale.

Queste differenze identificano la categoria “clone”, non il singolo indice.

## **18.2 Badge temporale**

Ogni clone possiede un badge:

* T0;  
* T1;  
* T2;  
* T3;  
* Tn.

Il badge può apparire:

* sempre in forma ridotta;  
* quando il cursore passa sul clone;  
* durante la pausa;  
* quando il clone è selezionato;  
* durante un paradosso.

Durante il gameplay normale il badge deve essere discreto.

Durante la pausa diventa pienamente leggibile.

## **18.3 Identificazione scalabile**

L'uso di indici numerici permette di supportare:

* T0;  
* T1;  
* T2;  
* fino a qualsiasi Tn richiesto dal livello.

L'interfaccia non dipende da:

* colori univoci;  
* simboli differenti per ogni clone;  
* palette limitate.

## **18.4 Percorsi**

Durante la pausa può essere mostrato:

* il Main Path completo;  
* il segmento attualmente percorso;  
* il Goal successivo;  
* il punto di investigazione;  
* un eventuale percorso alternativo.

Il path del clone selezionato viene mostrato con una linea neutra ad alto contrasto.

Le porzioni che attraversano un cono pericoloso vengono evidenziate in rosso.

## **18.5 Rumore**

Il rumore può essere visualizzato mediante:

* celle evidenziate;  
* onda circolare;  
* linea verso il clone che lo percepirà;  
* indicazione dell'indice della sorgente;  
* indicazione degli ascoltatori validi.

Durante la pausa il giocatore può selezionare una sorgente per vedere quali cloni possono essere distratti.

## **18.6 Cambio di Goal**

Quando un Goal viene invalidato:

1. appare un'icona sopra il clone;  
2. il target precedente viene barrato;  
3. il nuovo target viene evidenziato;  
4. il path viene ricalcolato;  
5. il pannello della pausa spiega la causa.

Esempio:

**Goal annullato: Terminale A distrutto**

**Nuovo Goal: Raggiungi Terminale B**

## **18.7 Feedback di paradosso**

Il feedback deve mostrare chiaramente:

* chi ha visto;  
* chi è stato visto;  
* perché la relazione non è valida.

Esempio:

**OSSERVATORE:** T0  
 **BERSAGLIO:** T2  
 **CONDIZIONE:** T2 appartiene al futuro di T0

---

# **19\. Elementi dei puzzle ambientali**

La prima versione contiene esclusivamente puzzle ambientali.

## **19.1 Porta**

Può essere:

* aperta;  
* chiusa;  
* bloccata;  
* temporizzata.

## **19.2 Interruttore**

Modifica lo stato di uno o più dispositivi.

## **19.3 Piastra a pressione**

Rimane attiva finché una versione o un oggetto occupa la cella.

## **19.4 Terminale**

Richiede un'interazione e può:

* aprire porte;  
* disattivare barriere;  
* alimentare dispositivi;  
* modificare percorsi.

## **19.5 Superficie rumorosa**

Produce rumore quando viene attraversata.

## **19.6 Emettitore acustico**

Genera un rumore controllato.

Può essere:

* monouso;  
* ripetibile;  
* temporizzato.

## **19.7 Oggetto trasportabile**

Può:

* attivare una piastra;  
* bloccare una cella;  
* produrre rumore;  
* modificare una linea visiva.

## **19.8 Oggetto distruttibile**

Può invalidare un Goal quando viene eliminato.

## **19.9 Barriera visiva**

Blocca il cono visivo.

Può essere:

* fissa;  
* spostabile;  
* apribile;  
* disattivabile.

## **19.10 Chrono Spawn**

Definisce il punto di ingresso di una nuova timeline.

---

# **20\. Struttura della progressione**

## **Fase 1 — Fondamenti**

* movimento su griglia;  
* pathfinding;  
* interazioni;  
* obiettivo della mappa.

## **Fase 2 — Primo clone**

* rewind;  
* Main Path;  
* collaborazione con una run precedente.

## **Fase 3 — Stealth temporale**

* cono visivo;  
* paradosso player-clone;  
* indici temporali.

## **Fase 4 — Paradosso fra cloni**

* relazione asimmetrica;  
* passato e futuro;  
* protezione delle versioni più recenti.

## **Fase 5 — Rumore**

* distrazioni;  
* gerarchia temporale;  
* cambio di orientamento.

## **Fase 6 — Contesto**

* Goal invalidi;  
* alternative;  
* replanning.

## **Fase 7 — Emergenza**

* chain reaction;  
* interazioni fra rumore, visione e Goal.

## **Fase 8 — Maestria**

* mappe con più cloni;  
* più soluzioni valide;  
* analisi tramite pausa tattica.

---

# **21\. Vertical Slice**

## **21.1 Durata**

**10-15 minuti**

## **21.2 Contenuto**

Una singola mappa completa con:

* 3 Chrono Spawn;  
* massimo 3 cloni;  
* 1 Main Goal;  
* 2 porte;  
* 1 terminale;  
* 1 piastra;  
* 1 superficie rumorosa;  
* 1 emettitore acustico;  
* 1 oggetto distruttibile;  
* 1 alternativa contestuale;  
* 1 chain reaction controllata;  
* pausa tattica completa.

## **21.3 Sequenza**

### **Fase 1 — Movimento**

Il giocatore apprende:

* selezione delle celle;  
* anteprima del path;  
* movimento automatico;  
* interazioni.

### **Fase 2 — Primo rewind**

Il giocatore attiva una piastra e genera T0.

Nella nuova timeline sfrutta T0 per mantenere aperta una porta.

### **Fase 3 — Primo paradosso**

Il Player T1 deve evitare il cono di T0.

### **Fase 4 — Pausa tattica**

Il giocatore mette in pausa e seleziona il player.

T0 viene evidenziato in rosso.

Il sistema comunica che T0 può generare un paradosso vedendo il player.

### **Fase 5 — Secondo rewind**

Viene generato T1.

La timeline contiene:

* T0;  
* T1;  
* Player T2.

### **Fase 6 — Relazione asimmetrica**

Il giocatore seleziona T1 durante la pausa.

* T0 viene evidenziato in rosso;  
* T1 rimane neutro;  
* eventuali versioni successive vengono evidenziate in blu.

Il livello mostra che:

* T1 può vedere T0;  
* T0 non può vedere T1.

### **Fase 7 — Rumore**

T1 genera un rumore che distrae T0.

Il giocatore deve controllare il nuovo cono di T0 per evitare che intercetti:

* T1;  
* Player T2.

### **Fase 8 — Replanning**

Un elemento richiesto dal Main Path di T0 viene distrutto.

T0 sceglie un obiettivo alternativo.

### **Fase 9 — Chain reaction**

Il nuovo percorso di T0 cambia:

* orientamento;  
* cono visivo;  
* propagazione del rumore;  
* sicurezza del percorso di T1.

### **Fase 10 — Completamento**

Il Player T2 sfrutta i comportamenti dei due cloni per completare il Main Goal.

---

# **22\. Direzione artistica**

## **22.1 Stile**

Il gioco utilizza:

* asset prodotti in MagicaVoxel;  
* geometrie semplici;  
* ambienti modulari;  
* texture minime;  
* illuminazione stilizzata;  
* materiali leggibili.

## **22.2 Player e cloni**

Player e cloni condividono la stessa forma di base.

Il player viene distinto tramite:

* materiale più solido;  
* effetto meno trasparente;  
* indicatore `NOW`.

I cloni utilizzano:

* materiale ghost uniforme;  
* interferenza temporale;  
* dithering;  
* scia leggera.

Non vengono utilizzati colori differenti per separare T0, T1, T2 e Tn.

## **22.3 Animazioni**

Le animazioni sono realizzate tramite:

* static mesh swap;  
* trasformazioni;  
* rotazioni;  
* squash and stretch;  
* pochi frame voxel.

Animazioni necessarie:

* movimento;  
* interazione;  
* investigazione;  
* rilevamento;  
* rewind;  
* collasso temporale.

---

# **23\. Audio**

Non è previsto doppiaggio.

L'audio possiede una funzione sistemica.

Sono necessari segnali distinti per:

* passo normale;  
* passo su superficie rumorosa;  
* porta;  
* terminale;  
* oggetto distrutto;  
* emettitore acustico;  
* percezione del rumore;  
* cambio Goal;  
* rilevamento;  
* paradosso;  
* rewind;  
* pausa;  
* ripresa del tempo;  
* completamento.

I suoni estetici non devono essere confusi con i rumori percepibili dai cloni.

---

# **24\. Architettura tecnica indicativa**

## **24.1 Grid System**

Gestisce:

* celle;  
* pathfinding;  
* costi;  
* interazioni;  
* propagazione acustica;  
* raggiungibilità.

## **24.2 Temporal Run Recorder**

Registra:

* Goal;  
* target;  
* destinazioni;  
* interazioni;  
* attese;  
* rumori;  
* eventi contestuali.

## **24.3 Temporal Identity Component**

Contiene:

* Temporal Index;  
* tipo di entità;  
* relazione passato/futuro;  
* regole di visibilità;  
* regole di percezione acustica.

## **24.4 Temporal Clone Component**

Contiene:

* Main Path;  
* Goal corrente;  
* stato di distrazione;  
* stato di investigazione;  
* stato di replanning;  
* target corrente.

## **24.5 Context Goal System**

Valuta:

* validità del Goal;  
* alternative;  
* Main Goal;  
* stato della mappa;  
* raggiungibilità.

Può essere implementato tramite StateTree con:

* evaluator;  
* task;  
* condition;  
* transizioni custom.

## **24.6 Temporal Perception System**

Gestisce:

* coni visivi;  
* indici temporali;  
* rilevamento;  
* filtraggio dei rumori;  
* paradossi.

## **24.7 Tactical Pause Manager**

Gestisce:

* blocco della simulazione;  
* selezione degli attori;  
* evidenziazione contestuale;  
* visualizzazione dei path;  
* visualizzazione dei coni;  
* pannelli informativi;  
* propagazione del rumore.

## **24.8 World State Manager**

Gestisce:

* stato baseline;  
* reset della mappa;  
* dispositivi;  
* oggetti distrutti;  
* interazioni condivise.

---

# **25\. Scope produttivo preliminare**

## **Asset 3D**

* 1 personaggio voxel;  
* 1 variante materiale clone;  
* 20-30 moduli ambientali;  
* 2 porte;  
* 2 terminali;  
* 2 interruttori;  
* 2 piastre;  
* 2 emettitori acustici;  
* 3 oggetti trasportabili;  
* 2 elementi distruttibili;  
* 4-6 elementi decorativi.

## **Animazioni**

* locomozione;  
* interazione;  
* investigazione;  
* rilevamento;  
* rewind;  
* paradosso.

## **VFX**

* rewind;  
* materializzazione;  
* interferenza temporale;  
* cono visivo;  
* propagazione del rumore;  
* replanning;  
* selezione durante la pausa;  
* collasso temporale.

## **Audio**

Circa 20-30 effetti sonori riutilizzabili.

---

# **26\. Rischi principali**

## **26.1 Sovraccarico cognitivo**

La presenza di più cloni può rendere difficile comprendere le relazioni.

### **Mitigazione**

* pausa tattica;  
* Temporal Index;  
* selezione contestuale;  
* evidenziazione rossa e blu temporanea;  
* path visualizzabili;  
* pannelli sintetici.

## **26.2 Eccessivo rumore visivo**

Badge, coni e path potrebbero affollare lo schermo.

### **Mitigazione**

* informazioni complete solo durante la pausa;  
* evidenziazione del clone selezionato;  
* filtri;  
* possibilità di nascondere categorie;  
* badge ridotti durante il gameplay.

## **26.3 Imprevedibilità dell'AI**

Il replanning potrebbe produrre comportamenti incomprensibili.

### **Mitigazione**

* alternative authored;  
* comportamento deterministico;  
* motivazione del cambio Goal visibile;  
* numero limitato di alternative per elemento.

## **26.4 Desincronizzazione**

Le distrazioni possono modificare fortemente le tempistiche.

### **Mitigazione**

* registrazione per Goal;  
* pathfinding deterministico;  
* reset completo;  
* assenza di collisioni fisiche tra versioni.

## **26.5 Limitazione involontaria del numero di cloni**

Un sistema basato su colori permanenti imporrebbe un numero massimo di identità leggibili.

### **Mitigazione**

* indici numerici;  
* materiale uniforme;  
* colori utilizzati esclusivamente come overlay contestuale;  
* interfaccia scalabile fino a Tn.

## **26.6 Soluzioni emergenti non previste**

Le chain reaction possono generare scorciatoie.

### **Mitigazione**

Le soluzioni emergenti coerenti con le regole devono essere considerate valide, salvo quando:

* rompono la progressione;  
* causano instabilità;  
* rendono impossibile il reset;  
* dipendono da bug o comportamenti non deterministici.

---

# **27\. Fuori scope per la prima versione**

Non sono previsti:

* nemici tradizionali;  
* combattimento;  
* armi;  
* boss;  
* multiplayer;  
* doppiaggio;  
* narrativa come USP;  
* livelli procedurali;  
* skeletal mesh;  
* Animation Blueprint tradizionali;  
* planner AI completamente generico;  
* rewind parziale;  
* modifica manuale dei Goal dei cloni;  
* comandi impartiti durante la pausa;  
* editor libero delle timeline.

---

# **28\. Nice to Have futuri**

## **28.1 Nemici esterni**

In versioni future possono essere introdotti nemici che:

* vedono player e cloni;  
* reagiscono ai rumori;  
* possono essere ingannati dalle timeline;  
* modificano il contesto ambientale.

I nemici devono rimanere secondari rispetto ai cloni.

## **28.2 Nuove percezioni**

* distrazioni visive;  
* luci;  
* allarmi;  
* falsi segnali;  
* rumori persistenti;  
* porte che modificano l'acustica.

## **28.3 Pausa avanzata**

Possibili funzioni future:

* scorrimento simulato della timeline;  
* anteprima delle posizioni future;  
* confronto fra Main Path e path attuale;  
* queue di un singolo movimento;  
* timeline grafica degli eventi.

Queste funzioni non sono necessarie nella vertical slice.

## **28.4 Livelli avanzati**

* più Chrono Spawn;  
* maggior numero di cloni;  
* mappe verticali;  
* obiettivi multipli;  
* rewind opzionali;  
* percorsi alternativi;  
* più soluzioni emergenti.

---

# **29\. Regole fondamentali riassunte**

1. Ogni rewind genera un nuovo clone.  
2. Ogni clone possiede un Temporal Index.  
3. Un clone ricorda tutte le versioni con indice inferiore.  
4. Un clone non deve vedere versioni con indice maggiore.  
5. Il passato non può vedere il futuro.  
6. Una versione futura può distrarre tramite rumore una versione passata.  
7. I cloni seguono Goal, non registrazioni frame-by-frame.  
8. Un Goal invalido causa replanning contestuale.  
9. Il giocatore può mettere in pausa il tempo per analizzare il sistema.  
10. Selezionando un clone, gli osservatori pericolosi vengono evidenziati in rosso e quelli sicuri in blu.  
11. I colori sono contestuali e non identificano permanentemente i cloni.  
12. L'identità temporale è comunicata tramite gli indici T0, T1, T2… Tn.

