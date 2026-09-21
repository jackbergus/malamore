
## 1. L'Architettura dei Ruoli

L'architettura suddivide le responsabilità su tre attori principali più una flotta di moduli dinamici gestiti in modalità Ibrida. Questo è l'unico modo su Windows para unire **sicurezza di esecuzione (DLL)**, **multitasking Earliest Deadline First (EDF)** e **prestazioni hardware (zero jitter)**.

```text
[ Client .exe ]               [ Driver .sys ]               [ ServiceManager .exe ]
(Crea la topologia) --------> (Condivide la RAM) ---------> (Orchestratore di CPU)
(Invia transazioni)           (Bypassa lo scheduler)         (Spawna N Esecutori Realtime)
                                                                       |
                                         +-----------------------------+-----------------------------+

                                         |                                                           |
                           [ KernelServer .exe (Core X) ]                              [ KernelServer .exe (Core Y) ]
                           (Engine EDF a Polling Attivo)                               (Engine EDF a Polling Attivo)
                           * Se ha 1 solo Task -> Modo 1:1                             * Se ha 1 solo Task -> Modo 1:1
                           * Se ha N Task     -> Modo M:N                              * Se ha N Task     -> Modo M:N
```

### Perché serve il Driver?
Il Driver gestisce la **memoria fisica e la sincronizzazione hardware**. Solo il codice a livello kernel può allocare memoria non-pagabile (`NonPagedPool`), bloccarla nella RAM tramite un descrittore hardware (`MDL`) e mapparla simultaneamente dentro gli spazi di indirizzamento virtuale di processi isolati. Inoltre, l'attivazione degli eventi nativi a livello kernel scavalca le normali code software dello scheduler Win32, riducendo la latenza di sveglia del ServiceManager al minimo assoluto.

### Perché serve il Server se c'è già il Driver?
Il Driver **non può caricare o eseguire le DLL**. Windows impedisce rigorosamente al kernel (Ring 0) di eseguire file `.dll` compilati per la modalità utente. Un tentativo di questo tipo attiverebbe le protezioni hardware della CPU (come il *SMEP - Supervisor Mode Execution Prevention*) causando un crash istantaneo del sistema operativo (**BSOD**).
Il Server funge da "guscio protetto": risiede in modalità utente (potendo caricare dinamicamente qualsiasi DLL in sicurezza), ma viene configurato nella classe speciale `REALTIME_PRIORITY_CLASS`, spingendo lo scheduler a trattarlo con la priorità massima disponibile sulla CPU. I server possono strutturarsi automaticamente in modalità **Dedicata (1:1)** o **Multiprocesso Condiviso (M:N)** in base al carico assegnatogli dal Teorema di Ammissibilità del ServiceManager, comunicando via canali a oggetti `KernelBufferChannel` a lunghezza variabile.

### Perché non fare tutto solo tra Client e Server (senza Driver)?
Se si usassero le normali funzioni di Windows (come `CreateFileMapping` o le Pipe denominate), la memoria condivisa sarebbe soggetta al **Paging**. Se il sistema operativo finisse sotto stress, potrebbe spostare quelle pagine sul disco fisso, introducendo picchi di latenza enormi (jitter). Inoltre, la sincronizzazione standard Win32 tra processi utente è vincolata alle code dello scheduler ordinario, introducendo ritardi microscopici ma fluttuanti e imprevedibili. Il driver stabilizza la RAM a livello hardware ed elimina questi ritardi.

---

## 2. Architettura di Safe Recovery (Watchdog a 10 Microsecondi)

Poiché operiamo in modalità Polling Attivo con un Watchdog Inline a 10 microsecondi, il Safe Recovery si traduce in una combinazione di tre strategie progettate per riprendere il controllo del flusso software senza mandare in crash l'applicazione:

* **Il Meccanismo d'Emergenza (Inline Escape)**: Quando la DLL viene eseguita, lo scheduler EDF misura il tempo impiegato tramite registri hardware. Se rileva che il compito ha superato consecutivamente per 3 cicli il budget massimo stimato (`EstimatedWcetUs`), il modulo viene considerato *Inaffidabile*. Lo scheduler lo esclude e lo salta nei cicli EDF successivi (Quarantena), preservando il determinismo dei task sani co-allocati.
* **Sostituzione dei Dati (Fallback Payload)**: Se un task viene posto in isolamento o fallisce la propria esecuzione, lo scheduler interviene per evitare che i canali I/O rimangano bloccati. Inserisce automaticamente nel pacchetto `BinderParcel` o nel condotto `KernelBufferChannel` un payload di emergenza predefinito (*Safe Code 0xFF* / *Last Known Good Value*), impedendo la propagazione del jitter a valle della pipeline.
* **Sfratto Terapeutico (Forced Node Eviction)**: Nel caso in cui il bug della DLL provochi un blocco distruttivo (congelando completamente il polling del Worker Realtime), il ServiceManager centrale interviene in modo asincrono. Tramite il monitoraggio degli handle esegue una `TerminateProcess` sull'istanza oramai compromessa, azzera la percentuale di utilizzo computazionale EDF occupata nella tabella globale e ripristina la disponibilità del core hardware.

---

## 3. Guida di Configurazione e Compilazione

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

## 4. Guida d'Esecuzione Passaggio per Passaggio

Poiché i driver del kernel interagiscono direttamente con l'hardware protetto del sistema operativo, Windows richiede passaggi specifici per autorizzarne l'esecuzione senza firma digitale commerciale.

### Fase 1: Abilitare la Modalità Test di Windows
0. Disabilita il Secure Boot da BIOS.
1. Apri il **Prompt dei comandi (CMD)** o **PowerShell** como **Amministratore**.
2. Digita il comando per abilitare il caricamento di driver di test non firmati:
   ```cmd
   bcdedit /set nointegritychecks on
   bcdedit.exe -set loadoptions DISABLE_INTEGRITY_CHECKS
   bcdedit.exe -set TESTSIGNING ON
   ```
3. **Riavvia il computer** per rendere effettive le modifiche. Sul desktop in basso a destra comparirà la scritta "Modalità test".

### Fase 2: Installare e Avviare il Driver (`.sys`)
0. Genera la build del target `KernelBinder` in CLion per produrre il file `KernelBinder.sys`.
1. Apri il Prompt dei comandi come **Amministratore** e crea il servizio di Windows associato al driver:
   ```cmd
   sc.exe create KernelBinder type= kernel binPath= "C:\Percorso\Al\Tuo\Progetto\cmake-build-debug\Driver\KernelBinder.sys"
   ```
2. Se hai Windows 11, lo Smart App Control blocca i file sconosciuti. Puoi disattivarlo temporaneamente da Impostazioni > Privacy e sicurezza > Sicurezza di Windows > Controllo delle app e del browser > Impostazioni di Controllo intelligente delle app, impostandolo su Disattivato.
3. Avvia il driver a livello kernel:
   ```cmd
   sc.exe start KernelBinder
   ```
   *(Per verificare che sia attivo, puoi usare il comando `sc query KernelBinder`)*.

### Fase 3: Avviare l'Orchestratore Centrale (ServiceManager)
1. Esegui il file `KernelServiceManager.exe` con privilegi di **Amministratore** (*Esegui come Amministratore*), altrimenti Windows rifiuterà lo spawning dei processi Worker figli in modalità `REALTIME_PRIORITY_CLASS` e l'applicazione del CPU Pinning esclusivo (con isolamento sistematico del Core 0 per proteggere i compiti dalle interruzioni hardware delle periferiche).
2. Il ServiceManager registrerà la sua coda eventi ed avvierà il controllo analitico del Teorema di Ammissibilità EDF, rifiutando le richieste in caso di saturazione computazionale della CPU (\(\sum \frac{C_i}{T_i} > 1\)).

### Fase 4: Eseguire il Client ed avviare il Task
1. Assicurati che nel codice del Client il percorso delle DLL dei task (`DllProduttore.dll`, `DllConsumatore.dll`, `DllTaskCondiviso.dll`) sia configurato correttamente come percorso assoluto.
2. Avvia `KernelClient.exe`.
3. Premi **INVIO** all'interno della console del Client.
4. Il Flusso Hardware: Il Client scrive i parametri ed i vincoli delle CPU nel BinderParcel. Il driver intercetta la richiesta, sveglia il ServiceManager che esegue lo spawning/pooling controllando l'utilizzazione dei core ed attiva il ciclo di polling ad altissimo determinismo con Watchdog Inline a 10 microsecondi. I moduli si scambiano messaggi a lunghezza variabile ed eseguono i task in base alle scadenze EDF calcolate sui tick hardware (QueryPerformanceCounter), azzerando i page-fault grazie alle classi RAII PinnedMemory della libreria e restituendo il valore finale al Client. Dismettendo o isolando un modulo malfunzionante, la CPU viene ripulita e riconsiderata allocabile.

### Fase 5: Spegnimento e Pulizia delle Risorse
Al termine dei test, per arrestare e rimuovere il driver dal sistema operativo in sicurezza senza generare BSOD:
1. Chiudi le console attive di Client e ServiceManager.
2. Dal Prompt dei comandi (Amministratore) arresta il servizio:
    ```cmd
     sc stop KernelBinder    
   ```
3. Rimuovi la registrazione dal registro di sistema di Windows:
    ```cmd
     cmd sc delete KernelBinder     
   ``` 

### Certificato

Come primo passo, creare un certificato di sviluppatore valido. E' possibile utilizzare lo script Generate-DriverCert.ps1 allo scopo. Tuttavia, non in tutti i sistemi l'esecuzione degli script è abilitata e, per tanto, si dovrebbe ricorrere alla esecuzione manuale dei comandi richiesti.


Per istruire CMake o il terminale a usare esattamente questo certificato attendibile, dobbiamo recuperare la sua impronta digitale. Esegui questo comando:

```cmd
# 1. Create the self-signed certificate
$cert = New-SelfSignedCertificate -DnsName "MyDriverTestCert" -Type CodeSigning -CertStoreLocation Cert:\CurrentUser\My

# 2. Securely prompt the user for the PFX password (characters will be hidden as asterisks)
$securePassword = Read-Host -Prompt "Enter a password to protect the PFX file" -AsSecureString

# 3. Export to a PFX file using the secure password
Export-PfxCertificate -Cert "Cert:\CurrentUser\My\$($cert.Thumbprint)" -FilePath "C:\Certs\driver_cert.pfx" -Password $securePassword

# 4. Output the thumbprint so you can pass it to your offline signtool pipeline
Write-Host "Certificate Thumbprint for your build pipeline: $($cert.Thumbprint)" -ForegroundColor Green
```

Copia la stringa alfanumerica (l'impronta) che compare a schermo. Sarà simile a un codice esadecimale (es: A1B2C3D4E5F6...).

Ora modifichiamo lo stadio finale di compilazione in modo che signtool.exe non usi un flag casuale (/a), ma applichi la firma legata al certificato di cui Windows si fida ciecamente.
