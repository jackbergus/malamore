# MALAMORE: Windows Kernel Binder (Low-Latency IPC Architecture)

Questa documentazione descrive l'architettura **Windows Kernel Binder**, un pattern di comunicazione inter-processo (IPC) ad altissime prestazioni pensato per aggirare i limiti dello scheduler di Windows e garantire l'esecuzione deterministica di task dinamici da 10ms a basso jitter.

---

## 1. L'Architettura dei Ruoli

L'architettura suddivide le responsabilità su tre attori principali più un modulo dinamico. Questo è l'unico modo su Windows per unire **sicurezza di esecuzione (DLL)** e **prestazioni hardware (zero jitter)**.

```
[ Client .exe ]               [ Driver .sys ]               [ Server .exe ]
(Decide il task) ---------> (Condivide la RAM) ---------> (Priorità Realtime)
(Sveglia i processi)                                     (Esegue fisicamente la DLL)
```

### Perché serve il Driver?
Il Driver gestisce la **memoria fisica e la sincronizzazione hardware**. Solo il codice a livello kernel può allocare memoria non-pagabile (`NonPagedPool`), bloccarla nella RAM tramite un descrittore hardware (`MDL`) e mapparla simultaneamente dentro gli spazi di indirizzamento virtuale di due processi isolati. Inoltre, l'attivazione degli eventi nativi a livello kernel scavalca le normali code software dello scheduler Win32, riducendo la latenza di sveglia del Server al minimo assoluto.

### Perché serve il Server se c'è già il Driver?
Il Driver **non può caricare o eseguire le DLL**. Windows impedisce rigorosamente al kernel (Ring 0) di eseguire file `.dll` compilati per la modalità utente. Un tentativo di questo tipo attiverebbe le protezioni hardware della CPU (come il *SMEP - Supervisor Mode Execution Prevention*) causando un crash istantaneo del sistema operativo (**BSOD**).
Il Server funge da "guscio protetto": risiede in modalità utente (potendo caricare dinamicamente qualsiasi DLL in sicurezza), ma viene configurato nella classe speciale `REALTIME_PRIORITY_CLASS`, spingendo lo scheduler a trattarlo con la priorità massima disponibile sulla CPU.

### Perché non fare tutto solo tra Client e Server (senza Driver)?
Se si usassero le normali funzioni di Windows (come `CreateFileMapping` o le Pipe denominate), la memoria condivisa sarebbe soggetta al **Paging**. Se il sistema operativo finisse sotto stress, potrebbe spostare quelle pagine sul disco fisso, introducendo picchi di latenza enormi (jitter). Inoltre, la sincronizzazione standard Win32 tra processi utente è vincolata alle code dello scheduler ordinario, introducendo ritardi microscopici ma fluttuanti e imprevedibili. Il driver stabilizza la RAM a livello hardware ed elimina questi ritardi.

---

## 2. Guida di Configurazione e Compilazione

### Prerequisiti Hardware e Software
* **Sistema Operativo**: Windows 10 o Windows 11 (x64 compilazione consigliata).
* **IDE**: CLion (con Toolchain configurata su **Visual Studio MSVC x64**).
* **SDK**: **Windows Driver Kit (WDK)** installato localmente.
* **Controllo Versioni**: Git (per la gestione del sottomodulo).

### Passi per la Configurazione del Progetto
1. Crea la struttura delle cartelle come descritto nel progetto.
2. Inizializza la repository Git e aggiungi il sottomodulo per CMake ed il WDK:
   ```bash
   git init
   git submodule add https://github.com Driver/3rdparty/FindWDK
   ```
3. Configura la Toolchain di CLion:
    * Vai in `Settings -> Build, Execution, Deployment -> Toolchains`.
    * Cambia o aggiungi la Toolchain impostandola su **Visual Studio**.
    * Imposta il campo *Architecture* tassativamente su **`x64`** o **`amd64`**.
4. Esegui il **Reload del progetto CMake** in CLion. Il modulo `FindWDK` rileverà automaticamente l'installazione locale del tuo Windows Driver Kit.

---

## 3. Guida d'Esecuzione Passaggio per Passaggio

Poiché i driver del kernel interagiscono direttamente con l'hardware protetto del sistema operativo, Windows richiede passaggi specifici per autorizzarne l'esecuzione senza firma digitale commerciale.

### Fase 1: Abilitare la Modalità Test di Windows
1. Apri il **Prompt dei comandi (CMD)** o **PowerShell** come **Amministratore**.
2. Digita il comando per abilitare il caricamento di driver di test non firmati:
   ```cmd
   bcdedit /set testsigning on
   ```
3. **Riavvia il computer** per rendere effettive le modifiche. Sul desktop in basso a destra comparirà la scritta "Modalità test".

### Fase 2: Installare e Avviare il Driver (`.sys`)
1. Genera la build del target `KernelBinder` in CLion per produrre il file `KernelBinder.sys`.
2. Apri il Prompt dei comandi come **Amministratore** e crea il servizio di Windows associato al driver:
   ```cmd
   sc create KernelBinder type= kernel binPath= "C:\Percorso\Al\Tuo\Progetto\cmake-build-debug\Driver\KernelBinder.sys"
   ```
3. Avvia il driver a livello kernel:
   ```cmd
   sc start KernelBinder
   ```
   *(Per verificare che sia attivo, puoi usare il comando `sc query KernelBinder`)*.

### Fase 3: Avviare il Server Realtime
1. Esegui il file `KernelServer.exe`.
2. **Nota tassativa**: Il server **deve** essere eseguito con privilegi di **Amministratore** (*Esegui come Amministratore*), altrimenti Windows rifiuterà la chiamata a `SetPriorityClass(..., REALTIME_PRIORITY_CLASS)` declassando il processo e invalidando la protezione contro il jitter.
3. Il server allocherà l'evento e rimarrà in ascolto a consumo zero di CPU.

### Fase 4: Eseguire il Client ed avviare il Task
1. Assicurati che nel codice del Client il percorso della DLL (`MyTask.dll`) sia configurato correttamente come percorso assoluto.
2. Avvia `KernelClient.exe`.
3. Premi **INVIO** all'interno della console del Client.
4. **Il Flusso Hardware**: Il Client scrive i parametri nella memoria fisica condivisa non-swappabile protetta dal driver, e setta l'evento. Il kernel intercetta la richiesta e risveglia istantaneamente il Server Realtime bypassando lo scheduler ordinario. Il server, avendo bloccato le proprie pagine RAM tramite `VirtualLock`, esegue il codice della DLL in 10ms esatti senza subire alcun page fault o interruzione software, restituendo il controllo al Client.

### Fase 5: Spegnimento e Pulizia delle Risorse
Al termine dei test, per arrestare e rimuovere il driver dal sistema operativo in sicurezza senza generare BSOD:
1. Chiudi le console di Client e Server.
2. Dal Prompt dei comandi (Amministratore) arresta il servizio:
   ```cmd
   sc stop KernelBinder
   ```
3. Rimuovi la registrazione dal registro di sistema di Windows:
   ```cmd
   sc delete KernelBinder
   ```