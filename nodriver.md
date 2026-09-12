# MALAMORE: Windows Kernel Binder (Low-Latency IPC Architecture)

Questa documentazione descrive l'architettura **Windows Kernel Binder**, un pattern di comunicazione inter-processo (IPC) ad altissime prestazioni pensato per garantire l'esecuzione deterministica di task dinamici da 10ms a basso jitter distribuibili su N nodi concorrenti in modalità protetta User-Mode (Ring 3) con granularità a 10 microsecondi.

---

## 1. L'Architettura dei Ruoli

L'architettura suddivide le responsabilità su tre attori principali più una flotta di moduli dinamici gestiti in modalità Ibrida. Questo è l'unico modo su Windows per unire **sicurezza di esecuzione (DLL)**, **multitasking Earliest Deadline First (EDF)** ed **eseguibilità immediata in Ring 3** aggirando i blocchi di sicurezza del kernel.

```text
[ Client .exe ]               [ KernelBinder .dll ]               [ ServiceManager .exe ]
(Crea la topologia) --------> (Canale Virtuale RAM) ------------> (Orchestratore di CPU)
(Invia transazioni)           (Simula il Driver Kernel)            (Spawna N Esecutori Realtime)
                                                                             |
                                               +-----------------------------+-----------------------------+

                                               |                                                           |
                                 [ KernelServer .exe (Core X) ]                              [ KernelServer .exe (Core Y) ]
                                 (Engine EDF a Polling Attivo)                               (Engine EDF a Polling Attivo)
                                 * Se ha 1 solo Task -> Modo 1:1                             * Se ha 1 solo Task -> Modo 1:1
                                 * Se ha N Task     -> Modo M:N                              * Se ha N Task     -> Modo M:N
```

### Perché serve il Canale Virtuale (`KernelBinder.dll`)?
La DLL simula il comportamento di un driver di Ring 0 gestendo la sincronizzazione e la mappatura logica tra processi isolati. L'attivazione dei flag atomici a consumo zero di CPU scavalca le normali code lente dei timer di Windows, mantenendo il ritardo software al minimo assoluto senza innescare le protezioni WDAC o AppLocker del sistema operativo.

### Perché serve il Server?
Il Server funge da "guscio protetto": risiede in modalità utente ed è configurato nella classe speciale `REALTIME_PRIORITY_CLASS` combinata con l'affinità hardware rigida (**CPU Pinning**), spingendo lo scheduler a trattarlo con la priorità massima disponibile sulla CPU (Livello 31). I server possono strutturarsi automaticamente in modalità **Dedicata (1:1)** o **Multiprocesso Condiviso (M:N)** in base al carico assegnatogli dal Teorema di Ammissibilità del ServiceManager, comunicando via canali a oggetti `KernelBufferChannel` a lunghezza variabile.

---

## 2. Architettura di Safe Recovery (Watchdog a 10 Microsecondi)

Poiché operiamo in modalità Polling Attivo con un Watchdog Inline a 10 microsecondi, il Safe Recovery si traduce in una combinazione di tre strategie progettate per riprendere il controllo del flusso software senza mandare in crash l'applicazione:

* **Il Meccanismo d'Emergenza (Inline Escape)**: Quando la DLL viene eseguita, lo scheduler EDF misura il tempo impiegato tramite registri hardware. Se rileva che il compito ha superato consecutivamente per 3 cicli il budget massimo stimato (`EstimatedWcetUs`), il modulo viene considerato *Inaffidabile*. Lo scheduler lo esclude e lo salta nei cicli EDF successivi (Quarantena), preservando il determinismo dei task sani co-allocati.
* **Sostituzione dei Dati (Fallback Payload)**: Se un task viene posto in isolamento o fallisce la propria esecuzione, lo scheduler intervenga per evitare che i canali I/O rimangano bloccati. Inserisce automaticamente nel pacchetto `BinderParcel` o nel condotto `KernelBufferChannel` un payload di emergenza predefinito (*Safe Code 0xFF* / *Last Known Good Value*), impedendo la propagazione del jitter a valle della pipeline.
* **Sfratto Terapeutico (Forced Node Eviction)**: Nel caso in cui il bug della DLL provochi un blocco distruttivo (congelando completamente il polling del Worker Realtime), il ServiceManager centrale interviene in modo asincrono. Tramite il monitoraggio degli handle esegue una `TerminateProcess` sull'istanza oramai compromessa, azzera la percentuale di utilizzo computazionale EDF occupata nella tabella globale e ripristina la disponibilità del core hardware.

---

## 3. Guida di Configurazione ed Esecuzione Passaggio per Passaggio

### Fase 1: Configurazione dell'Ambiente (CLion / MSVC)
1. Inizializza il progetto in CLion impostando la toolchain **Visual Studio MSVC x64**.
2. Ricarica il progetto CMake. Il modulo autogenererà i target per i client, i server, il manager, il finto driver `KernelBinder.dll` e le tre DLL dei task (`DllProduttore`, `DllConsumatore` e `DllTaskCondiviso`).

### Fase 2: Avvio dell'Orchestratore Centrale (ServiceManager)
1. Esegui il file `KernelServiceManager.exe` come **Amministratore** (indispensabile per permettergli di configurare le politiche `REALTIME_PRIORITY_CLASS` ed il CPU Pinning sui processi figli isolando il Core 0).
2. Il ServiceManager registrerà la sua coda eventi ed avvierà il controllo analitico del Teorema di Ammissibilità EDF, rifiutando le richieste in caso di saturazione computazionale della CPU (\(\sum \frac{C_i}{T_i} > 1\)).

### Fase 3: Eseguire il Client ed avviare il Task
1. Assicurati che nel codice del Client il percorso delle DLL dei task sia configurato correttamente come percorso assoluto.
2. Avvia `KernelClient.exe`.
3. Premi **INVIO** all'interno della console del Client.
4. **Il Flusso**: Il Client scrive i parametri ed i vincoli delle CPU nel `BinderParcel`. Il canale virtuale smista la transazione, sveglia il ServiceManager che esegue lo spawning/pooling controllando l'utilizzazione dei core ed attiva il ciclo di polling ad altissimo determinismo con **Watchdog Inline a 10 microsecondi**. I moduli si scambiano messaggi a lunghezza variabile ed eseguono i task in base alle scadenze EDF calcolate sui tick hardware (`QueryPerformanceCounter`), azzerando i page-fault grazie alle classi RAII `PinnedMemory` della libreria e restituendo il valore finale stringa al Client. Dismettendo o isolando un modulo malfunzionante, la CPU viene ripulita e riconsiderata allocabile.
