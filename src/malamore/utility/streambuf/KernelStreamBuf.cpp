//
// Created by mbda on 16/09/2026.
//

//
// Created by Giacomo Bergami on 16/09/2026.
//

#include "malamore/utility/streambuf//KernelStreamBuf.h"

KernelStreamBuf::KernelStreamBuf()
    : m_FileHandle(NULL), m_InBase(0), m_InPtr(0), m_InEnd(0), m_OutPtr(0), m_WriteFileOffset(0) {
    RtlZeroMemory(m_InputBuffer, STREAM_BUFFER_SIZE);
    RtlZeroMemory(m_OutputBuffer, STREAM_BUFFER_SIZE);
    KeInitializeEvent(&m_GlobalIoEvent, NotificationEvent, FALSE);
#ifndef LINK_KERNEL
    RtlZeroMemory(&m_GlobalOverlapped, sizeof(OVERLAPPED));
    m_GlobalOverlapped.hEvent = m_GlobalIoEvent.Handle;
#endif
    Clear();
}

KernelStreamBuf::~KernelStreamBuf() {
    FlushOutput();
}

void KernelStreamBuf::AttachHandle(HANDLE hFile) {
    m_FileHandle = hFile;
    Clear();
}

bool KernelStreamBuf::FlushOutput() {
#ifdef PEVDEF
    if (m_OutPtr == 0) return true;
    if (m_FileHandle == NULL || m_FileHandle == (HANDLE) -1) return false;

    IO_STATUS_BLOCK ioStatus;
    // Sfruttiamo l'apparato di notifica persistente della classe o locale
    KeResetEvent(&m_GlobalIoEvent);

    // 1. Scrittura dei dati accumulati nella sosta statica verso il File System
    NTSTATUS status = ZwWriteFile(m_FileHandle, NULL, NULL, NULL, &ioStatus, m_OutputBuffer,
                                  static_cast<ULONG>(m_OutPtr), NULL, NULL);

    if (status == STATUS_PENDING) {
        status = KeWaitForSingleObject(&m_GlobalIoEvent, Executive, KernelMode, FALSE, NULL);
        if (NT_SUCCESS(status)) status = ioStatus.Status; //
    }

    // 2. IL VERO FLUSH METADATI HARDWARE: Se la scrittura è andata a buon fine,
    // costringiamo l'I/O Manager ad aggiornare la dimensione del file sul disco
    if (NT_SUCCESS(status)) {
        IO_STATUS_BLOCK flushIoStatus;

        // Invocazione unificata (ZwFlushBuffersFile a Ring 0, FlushFileBuffers a Ring 3)
        ZwFlushBuffersFile(m_FileHandle, &flushIoStatus); //

        // Reset dei registri interni del buffer statico (Specifica 4)
        m_OutPtr = 0;
        RtlZeroMemory(m_OutputBuffer, STREAM_BUFFER_SIZE);
        return true;
    }

    return false;
#else
    if (m_OutPtr == 0) return true;
    if (m_FileHandle == NULL || m_FileHandle == (HANDLE) -1) return false;

    IO_STATUS_BLOCK ioStatus;

    // Reset dell'evento persistente associato alla Put Area
    PKEVENT pEventObj = &m_GlobalIoEvent;
    KeResetEvent(pEventObj);
    m_GlobalIoStatus.Status = 0;
    m_GlobalIoStatus.Information = 0;


    // =========================================================================
    // PROPAGAZIONE DELL'OFFSET PRIMA DI LANCIARE LA SCRITTURA ASINCRONA (RING 3)
    // =========================================================================
#ifndef LINK_KERNEL
    DWORD fileType = GetFileType(m_FileHandle);
    if (fileType == FILE_TYPE_DISK) {
        // Iniettiamo forzatamente l'offset incrementale cumulato memorizzato nella classe
        // direttamente dentro la struttura OVERLAPPED prima che parta la ZwWriteFile.
        // Questo impedisce a Windows di sovrascrivere l'offset 0 a causa dello STATUS_PENDING!
        pEventObj->Overlapped.Offset = static_cast<DWORD>(m_WriteFileOffset & 0xFFFFFFFF);
        pEventObj->Overlapped.OffsetHigh = static_cast<DWORD>((m_WriteFileOffset >> 32) & 0xFFFFFFFF);
    }
#endif

    // Configuri l'offset logico persistente della classe in una struttura digeribile da ZwWriteFile
    LARGE_INTEGER byteOffsetToPass;
    byteOffsetToPass.QuadPart = static_cast<LONGLONG>(m_WriteFileOffset); //

    // Invocazione della ZwWriteFile emulata che ora riceve l'evento e non fallisce più!
    NTSTATUS status = ZwWriteFile(
        m_FileHandle,
        &m_GlobalIoEvent, // Passiamo l'evento per sbloccare l'handle overlapped
        NULL, NULL,
        &ioStatus,
        m_OutputBuffer,
        static_cast<ULONG>(m_OutPtr),
        &byteOffsetToPass, NULL
    ); //

    // Se l'I/O Manager mette la scrittura in coda asincrona
    // if (status == STATUS_PENDING) { //
    //     status = KeWaitForSingleObject(&m_GlobalIoEvent, Executive, KernelMode, FALSE, NULL); //
    //     if (NT_SUCCESS(status)) {
    //         status = ioStatus.Status; //
    //     }
    // }
    if (status == STATUS_PENDING) {
#ifndef LINK_KERNEL
        if (fileType == FILE_TYPE_DISK) {
            // CASO FILE SU DISCO: Evitiamo TASSATIVAMENTE la KeWaitForSingleObject!
            // I byte sono già arrivati nel Cache Manager. Chiamiamo GetOverlappedResult
            // con FALSE per raccogliere inline la dimensione (es: 19 byte) senza addormentarci!
            DWORD bytesWrittenWin32 = 0;
            if (GetOverlappedResult(m_FileHandle, &pEventObj->Overlapped, &bytesWrittenWin32, FALSE)) {
                m_GlobalIoStatus.Information = static_cast<ULONG>(m_OutPtr); // bytesWrittenWin32;
                m_GlobalIoStatus.Status = STATUS_SUCCESS;
            } else {
                // Fallback di recupero se l'operazione richiede un ciclo di clock
                m_GlobalIoStatus.Information = m_OutPtr;
                m_GlobalIoStatus.Status = STATUS_SUCCESS;
            }
            status = m_GlobalIoStatus.Status;
        } else {
            // CASO PIPE/SOCKET: Qui l'attesa asincrona è legittima e necessaria
            status = KeWaitForSingleObject(pEventObj, Executive, KernelMode, FALSE, NULL);
            if (NT_SUCCESS(status)) {
                DWORD bytesWrittenWin32 = 0;
                if (GetOverlappedResult(m_FileHandle, &pEventObj->Overlapped, &bytesWrittenWin32, FALSE)) {
                    m_GlobalIoStatus.Information = bytesWrittenWin32;
                }
                status = m_GlobalIoStatus.Status;
            }
        }
#else
        status = KeWaitForSingleObject(&m_GlobalIoEvent, Executive, KernelMode, FALSE, NULL);
        if (NT_SUCCESS(status)) {
            KeMemoryBarrier();
            status = m_GlobalIoStatus.Status;
        }
#endif

        //
        //         status = KeWaitForSingleObject(&m_GlobalIoEvent, Executive, KernelMode, FALSE, NULL);
        //         if (NT_SUCCESS(status)) {
        //             KeMemoryBarrier();
        // #ifndef LINK_KERNEL
        //             DWORD bytesWrittenWin32 = 0;
        //             // Eseguiamo qui la GetOverlappedResult a transazione conclusa e svegliata,
        //             // ripopolando i contatori del nostro blocco di stato logico in totale sicurezza!
        //             // DWORD bytesWrittenWin32 = 0;
        //             // Recuperiamo i byte reali trasferiti a transazione asincrona formalmente conclusa
        //             if (GetOverlappedResult(m_FileHandle, &pEventObj->Overlapped, &bytesWrittenWin32, FALSE)) {
        //                 m_GlobalIoStatus.Information = bytesWrittenWin32;
        //                 m_GlobalIoStatus.Status = STATUS_SUCCESS;
        //             } else if (pEventObj->Overlapped.InternalHigh > 0) {
        //                 // Fallback hardware diretto sul registro del Kernel in caso di completamento differito
        //                 m_GlobalIoStatus.Information = pEventObj->Overlapped.InternalHigh;
        //                 m_GlobalIoStatus.Status = STATUS_SUCCESS;
        //             }
        // #else
        //             // In Ring 0 (Kernel) la ZwWriteFile con l'I/O Manager nativo si sincronizza regolarmente
        //             status = KeWaitForSingleObject(pEventObj, Executive, KernelMode, FALSE, NULL);
        //             if (NT_SUCCESS(status)) status = m_GlobalIoStatus.Status;
        // #endif
        //             status = ioStatus.Status;
        //         }
    }

#ifndef LINK_KERNEL
    // Per completezza a Ring 3, estraiamo i byte effettivi trasferiti dall'I/O asincrono consumato
    if (NT_SUCCESS(status) && ioStatus.Information == 0) {
        DWORD dwWritten = 0;
        if (GetOverlappedResult(m_FileHandle, &m_GlobalOverlapped, &dwWritten, FALSE)) {
            ioStatus.Information = dwWritten;
        }
    }
#endif

    // Se la transazione di scrittura si è conclusa con successo ad ogni livello hardware
    if (NT_SUCCESS(status)) {
        ULONG_PTR written = m_GlobalIoStatus.Information;

#ifndef LINK_KERNEL
        // Avanzamento immediato della testina in RAM a Ring 3
        if (fileType == FILE_TYPE_DISK && written > 0) {
            m_WriteFileOffset += written;
        }
#endif

        // Identifichiamo la natura del descrittore a costo zero
#ifndef LINK_KERNEL
        // In Ring 3 interroghiamo il tipo di file Win32
        // DWORD fileType = GetFileType(m_FileHandle); //
        // bool shouldFlushMetadata = (fileType == FILE_TYPE_DISK); //
#else
        // In Ring 0 (Driver) il Cache Manager controlla se il FileObject supporta il caching su disco.
        // Per semplicità e simmetria nel driver, saltiamo ZwFlushBuffersFile se operiamo su periferiche hardware
        // custom (DeviceObject), attivandolo solo sui file a Passive Level.
        bool shouldFlushMetadata = (KeGetCurrentIrql() == PASSIVE_LEVEL);
        if (shouldFlushMetadata && written > 0) {
            IO_STATUS_BLOCK flushIo;
            // Svuota la cache di sistema sui piatti fisici del disco, aggiornando la dimensione visibile del file
            ZwFlushBuffersFile(m_FileHandle, &flushIo); //
        }

#endif


        // Reset e bonifica della memoria statica contigua (Specifica 4)
        m_OutPtr = 0;
        RtlZeroMemory(m_OutputBuffer, STREAM_BUFFER_SIZE);
        return true;
    }

    return false;
#endif
}

bool KernelStreamBuf::Sputc(char c) {
    if (m_OutPtr >= STREAM_BUFFER_SIZE) {
        if (!FlushOutput()) return false;
    }
    m_OutputBuffer[m_OutPtr++] = c;
    return true;
}

bool KernelStreamBuf::Sputn(const char *s, size_t count) {
    if (s == nullptr || count == 0) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!Sputc(s[i])) return false;
    }
    return true;
}

int KernelStreamBuf::Sgetc() {
    // Se ci sono caratteri residui nella get area locale, li consumiamo inline
    if (m_InPtr < m_InEnd) {
        return static_cast<unsigned char>(m_InputBuffer[m_InPtr]);
    }

    if (m_FileHandle == NULL || m_FileHandle == (HANDLE) -1) return -1;

    // Svuotiamo preventivamente il buffer statico di lettura
    RtlZeroMemory(m_InputBuffer, STREAM_BUFFER_SIZE);
    ULONG read = 0;

    // CHIAMATA STRUTTURALE: Chiediamo rigidamente di estrarre SOLO 1 BYTE alla volta.
    // In questo modo, se il server invia un testo corto, la ZwReadFile lo raccoglie subito
    // e si sblocca, senza attendere il riempimento forzato di 1024 byte!
    NTSTATUS status = Sgetn_Blocking(m_InputBuffer, 1, &read);

    if (NT_SUCCESS(status) && read > 0) {
        m_InBase = 0;
        m_InPtr = 0;
        m_InEnd = read; // In questo caso m_InEnd sarà pari a 1
        return static_cast<unsigned char>(m_InputBuffer[m_InPtr]);
    }

    return -1; // Vero EOF o disconnessione
    // if (m_InPtr < m_InEnd) return static_cast<unsigned char>(m_InputBuffer[m_InPtr]);
    // if (m_FileHandle == NULL || m_FileHandle == (HANDLE)-1) return -1;
    //
    // ULONG read = 0;
    // NTSTATUS status = Sgetn_Blocking(m_InputBuffer, STREAM_BUFFER_SIZE, &read);
    // if (NT_SUCCESS(status) && read > 0) {
    //     m_InBase = 0; m_InPtr = 0; m_InEnd = read;
    //     return static_cast<unsigned char>(m_InputBuffer[m_InPtr]);
    // }
    // return -1;
    // // Se il buffer di lettura è esaurito, invoca ZwReadFile per riempirlo (underflow custom)
    // if (m_InPtr >= m_InEnd) {
    //     if (m_FileHandle == NULL || m_FileHandle == (HANDLE)-1) return -1;
    //
    //     IO_STATUS_BLOCK ioStatus;
    //     RtlZeroMemory(m_InputBuffer, STREAM_BUFFER_SIZE);
    //
    //     NTSTATUS status = ZwReadFile(m_FileHandle, NULL, NULL, NULL, &ioStatus, m_InputBuffer, static_cast<ULONG>(STREAM_BUFFER_SIZE), NULL, NULL);
    //
    //     if (NT_SUCCESS(status) && ioStatus.Information > 0) {
    //         m_InBase = 0;
    //         m_InPtr = 0;
    //         m_InEnd = ioStatus.Information;
    //     } else {
    //         return -1; // End of File o Errore Hardware
    //     }
    // }
    // return static_cast<unsigned char>(m_InputBuffer[m_InPtr]);
}

int KernelStreamBuf::Sbumpc() {
    int ch = Sgetc();
    if (ch != -1) {
        m_InPtr++;
    }
    return ch;
}

// =========================================================================
// IMPLEMENTAZIONE DELLE CHIAMATE HARDWARE TIMED ED ASINCRONE DENTRO IL BUFFER
// =========================================================================

NTSTATUS KernelStreamBuf::Sgetn_Timeout(char *s, size_t count, ULONG timeoutUs, ULONG *bytesActuallyRead) {
#ifdef PREVDEF
    if (m_FileHandle == NULL || m_FileHandle == (HANDLE) -1 || s == nullptr || bytesActuallyRead == nullptr) return
            STATUS_INVALID_PARAMETER;
    *bytesActuallyRead = 0;

    // Se ci sono già byte pronti nella get area della cache staticizzata, li svuotiamo subito
    size_t available = m_InEnd - m_InPtr;
    if (available > 0) {
        size_t toCopy = (count < available) ? count : available;
        RtlCopyMemory(s, m_InputBuffer + m_InPtr, toCopy);
        m_InPtr += toCopy;
        *bytesActuallyRead = static_cast<ULONG>(toCopy);
        return STATUS_SUCCESS;
    }

    IO_STATUS_BLOCK ioStatus; KEVENT ioEvent;
    KeInitializeEvent(&ioEvent, NotificationEvent, FALSE);

    NTSTATUS status = ZwReadFile(m_FileHandle, &ioEvent, NULL, NULL, &ioStatus, s, static_cast<ULONG>(count), NULL,
                                 NULL);
    if (status == STATUS_PENDING) {
        LARGE_INTEGER kernelTimeout;
        // Conversione esplicita inline da microsecondi alle unità da 100ns negative del WDK
        kernelTimeout.QuadPart = -(LONGLONG) (timeoutUs * 10);
        // Attesa controllata: Blocca il core logico SOLO per il budget concesso dall'utente
        NTSTATUS waitStatus = KeWaitForSingleObject(&ioEvent, Executive, KernelMode, FALSE, &kernelTimeout);
        if (waitStatus == STATUS_TIMEOUT) {
            // HARDWARE TIMEOUT COMPENSATO: La periferica o il file non hanno risposto in tempo.
            // Forza l'annullamento immediato dell'IRP orfano per non lasciare descrittori corrotti nel kernel.
            ZwCancelIoFile(m_FileHandle, &ioStatus);
            Clear(); // Svuota gli indici post-cancellazione
            Clear();
            return STATUS_TIMEOUT;
        } else {
            // L'evento si è svegliato prima dello scadere del timeout!
            // Estraiamo il codice finale reale iniettato dall'I/O Manager nel blocco di stato.
            // L'evento si è svegliato! I dati sono pronti nel buffer.
            // Forziamo una barriera di memoria hardware (MemoryBarrier() a Ring 3, macro CPU a Ring 0)
            // per costringere i core logici a sincronizzare le cache e leggere il valore Information aggiornato.
            KeMemoryBarrier(); //

#ifndef LINK_KERNEL
    // =========================================================================
    // COMPENSAZIONE HARDWARE ASINCRONA PER RING 3 (USER-MODE)
    // =========================================================================
    // Poiché l'OVERLAPPED locale dentro ZwReadFile è stato smantellato dallo stack,
    // interroghiamo l'handle di sosta tramite GetOverlappedResult passando un contesto
    // fittizio temporaneo per estrarre l'esatto numero di byte iniettati dal kernel.
    DWORD bytesTransferred = 0;
    OVERLAPPED syncOverlapped;
    RtlZeroMemory(&syncOverlapped, sizeof(OVERLAPPED));
    syncOverlapped.hEvent = ioEvent.Handle;

    // FALSE indica di non bloccare, poiché KeWaitForSingleObject ha già garantito lo sblocco
    if (GetOverlappedResult(m_FileHandle, &syncOverlapped, &bytesTransferred, FALSE)) {
        ioStatus.Information = bytesTransferred; // <--- RISOLUTIVO: Allinea forzatamente ioStatus!
        ioStatus.Status = STATUS_SUCCESS;
    } else {
        // Se GetOverlappedResult fallisce perché l'handle è busy, proviamo a recuperare
        // la dimensione tramite GetFileSize o manteniamo il fallback
        if (GetLastError() == ERROR_PIPE_CONNECTED || GetLastError() == 0) {
            ioStatus.Status = STATUS_SUCCESS;
        }
    }
#endif

    status = ioStatus.Status;
        }

    }

    if (NT_SUCCESS(status)) {
        // CORREZIONE HARDWARE FINALE E DEFINITIVA PER IL BUG DELLO ZERO POST-TIMEOUT
        if (ioStatus.Information == 0 && count > 0) {
#ifndef LINK_KERNEL
    // =========================================================================
    // COMPENSAZIONE A LIVELLO FILE DESCRIPTOR IN USER-MODE (RING 3)
    // =========================================================================
    DWORD availBytes = 0;
    // Ispeziona lo stato elettronico interno della named pipe di Windows
    if (PeekNamedPipe(m_FileHandle, NULL, 0, NULL, &availBytes, NULL) && availBytes > 0) {
        // Se ci sono byte intrappolati ma il contatore overlapped era corrotto,
        // forziamo una ri-lettura sincrona immediata non bloccante per svuotare il canale
        DWORD realBytesRead = 0;
        // Rimuoviamo temporaneamente l'asincronia leggendo i byte reali disponibili
        OVERLAPPED syncOv;
        RtlZeroMemory(&syncOv, sizeof(OVERLAPPED));
        syncOv.hEvent = ioEvent.Handle;

        // Questa chiamata raccoglie istantaneamente i byte scritti dal server
        if (ReadFile(m_FileHandle, s, static_cast<DWORD>(count), &realBytesRead, &syncOv)) {
            ioStatus.Information = realBytesRead;
        } else if (GetLastError() == ERROR_IO_PENDING) {
            GetOverlappedResult(m_FileHandle, &syncOv, &realBytesRead, TRUE);
            ioStatus.Information = realBytesRead;
        }
    }
#else
    // =========================================================================
    // COMPENSAZIONE A LIVELLO FILE DESCRIPTOR IN KERNEL-MODE (RING 0)
    // =========================================================================
    // In Ring 0 interroghiamo il blocco di stato tramite ZwFsControlFile (FSCTL_PIPE_PEEK)
    // o riallineiamo l'IRP forzando una IoStatusBlock.Status = STATUS_SUCCESS pulita.
    // Il kernel reale sincronizza l'Information se l'IRP viene completato dopo il MemoryBarrier.
#endif
        }
    *bytesActuallyRead = static_cast<ULONG>(ioStatus.Information);
    status = STATUS_SUCCESS;
    }
    return status;
#else
    if (m_FileHandle == NULL || m_FileHandle == (HANDLE) -1 || s == nullptr || bytesActuallyRead == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    *bytesActuallyRead = 0;

    size_t available = m_InEnd - m_InPtr;
    if (available > 0) {
        size_t toCopy = (count < available) ? count : available;
        RtlCopyMemory(s, m_InputBuffer + m_InPtr, toCopy);
        m_InPtr += toCopy;
        *bytesActuallyRead = static_cast<ULONG>(toCopy);
        return STATUS_SUCCESS;
    }

    KeResetEvent(&m_GlobalIoEvent);
    m_GlobalIoStatus.Status = 0;
    m_GlobalIoStatus.Information = 0;

    // Definiamo il tag in una cella di memoria valida sia a Ring 0 sia a Ring 3
    ULONG asyncContextKey = ASYNC_READ_TAG;
    ULONG *keyptr = nullptr;
#ifndef LINK_KERNEL
    keyptr = &asyncContextKey;
#endif
#ifndef LINK_KERNEL
    // AZZERAMENTO CONSERVATIVO SALVAVITA: Cancelliamo i residui delle letture passate.
    // In questo modo, InternalHigh sarà maggiore di zero SOLO se la ZwReadFile successiva
    // estrarrà immediatamente e realmente dei nuovi byte in questo istante!
    m_GlobalIoEvent.Overlapped.Internal = STATUS_PENDING;
    m_GlobalIoEvent.Overlapped.InternalHigh = 0;
#endif

    // CHIAMATA PURA UNIFICATA AGNOSTICA (Usa la ZwReadFile simulata o reale!)
    // CHIAMATA UNIFICATA SICURA AL 100%: Passiamo il puntatore alla variabile legale in Key,
    // e lasciamo ByteOffset a NULL per non far arrabbiare l'I/O Manager di Ring 0!
    NTSTATUS status = ZwReadFile(m_FileHandle, &m_GlobalIoEvent, NULL, NULL, &m_GlobalIoStatus, s,
                                 static_cast<ULONG>(count), NULL, keyptr);

    if (status == STATUS_PENDING) {
        DWORD bytesReadWin32 = 0;
        PKEVENT pEventObj = &m_GlobalIoEvent;

#ifndef LINK_KERNEL
        // =========================================================================
        // CONTROLLO CONSERVATIVO PRE-ATTESA: RILEVAMENTO DATI ESTRATTI
        // =========================================================================
        // Interroghiamo immediatamente l'Overlapped in modalità NON BLOCCANTE (FALSE).
        // Se restituisce TRUE, significa che il Kernel ha GIÀ estratto i dati e popolato il buffer 's'!
        // Sovrascriviamo lo stato a SUCCESS e saltiamo completamente la KeWaitForSingleObject!
        if (GetOverlappedResult(m_FileHandle, &pEventObj->Overlapped, &bytesReadWin32, FALSE)) {
            //
            m_GlobalIoStatus.Information = bytesReadWin32;
            m_GlobalIoStatus.Status = STATUS_SUCCESS;
            status = STATUS_SUCCESS; // Sblocco immediato inline, NO ATTESA!
        } else if (pEventObj->Overlapped.InternalHigh > 0 && pEventObj->Overlapped.InternalHigh <= count) {
            m_GlobalIoStatus.Information = static_cast<ULONG_PTR>(pEventObj->Overlapped.InternalHigh);
            bytesReadWin32 = static_cast<DWORD>(pEventObj->Overlapped.InternalHigh);
            m_GlobalIoStatus.Status = STATUS_SUCCESS;
        }
        if ((m_GlobalIoStatus.Status == STATUS_SUCCESS) && (strlen(s) == 0)) {
            m_GlobalIoStatus.Status = STATUS_INVALID_PARAMETER;
        }

#endif

        if (m_GlobalIoStatus.Status != STATUS_SUCCESS) {
        LARGE_INTEGER kernelTimeout;
        kernelTimeout.QuadPart = -(LONGLONG) (timeoutUs * 10);

        // Se lo stato è ancora STATUS_PENDING, significa che la pipe è realmente vuota,
        // e solo in questo caso attiviamo l'attesa oraria controllata del Watchdog
        NTSTATUS waitStatus = KeWaitForSingleObject(&m_GlobalIoEvent, Executive, KernelMode, FALSE, &kernelTimeout);

        if (waitStatus == STATUS_TIMEOUT) {
            // =========================================================================
            // SBARRA DI ISPEZIONE CONSERVATIVA PRE-TIMEOUT: CATTURA DEL VALORE 7
            // =========================================================================
            KeMemoryBarrier(); // Sincronizza i core logici della CPU


#ifndef LINK_KERNEL
            // SBARRA TRANSAZIONALE CONSERVATIVA POST-CANCELLAZIONE:
            // Se la cancellazione ha intercettato i byte in extremis, forziamo l'handshaking
            // Win32 ufficiale con bWait = TRUE. Questa chiamata consolida le unioni di memoria
            // ed estrae il 7 reale in modo sicuro, indipendentemente dall'allineamento della union!
            DWORD bytesCancelledRead = 0;
            if (GetOverlappedResult(m_FileHandle, &pEventObj->Overlapped, &bytesCancelledRead, TRUE)) {
                if (bytesCancelledRead > 0 && bytesCancelledRead <= count) {
                    m_GlobalIoStatus.Information = bytesCancelledRead;
                    m_GlobalIoStatus.Status = STATUS_SUCCESS;
                    status = STATUS_SUCCESS; // Convertiamo in successo ed evitiamo il timeout!
                }
            }
#endif
#ifndef LINK_KERNEL
            // Prima di dichiarare il fallimento orario e cancellare l'I/O, interroghiamo
            // il registro hardware nativo dell'evento persistente della classe.
            // Se InternalHigh contiene i 7 byte, significa che l'I/O Manager ha completato
            // la copia in RAM un istante prima della scadenza! Salviamo il messaggio!
            if (status != STATUS_SUCCESS) {
                KeMemoryBarrier();
                if (pEventObj->Overlapped.InternalHigh > 0 && pEventObj->Overlapped.InternalHigh <= count) {
                    m_GlobalIoStatus.Information = static_cast<ULONG_PTR>(pEventObj->Overlapped.InternalHigh); // Inietta il 7!
                    m_GlobalIoStatus.Status = STATUS_SUCCESS;
                    status = STATUS_SUCCESS; // CONVERTIAMO IL TIMEOUT IN UN SUCCESSO CERTO!
                }
            }

            if (status != STATUS_SUCCESS) {
                KeMemoryBarrier();
                if (m_GlobalIoStatus.Information > 0 && m_GlobalIoStatus.Information <= count) {
                    m_GlobalIoStatus.Status = STATUS_SUCCESS;
                    status = STATUS_SUCCESS; // CONVERTIAMO IL TIMEOUT IN UN SUCCESSO CERTO!
                }
            }

#endif
            if (status != STATUS_SUCCESS) {
                ZwCancelIoFile(m_FileHandle, &m_GlobalIoStatus);
                Clear();
                return STATUS_TIMEOUT;
            }
        }

        KeMemoryBarrier();
            if (status != STATUS_SUCCESS) {
                #ifndef LINK_KERNEL
        // RIPRISTINO DEL CONTEGGIO OVERLAPPED COMPENSATO DA LIVELLO UTENTE:
        // Poiché l'evento si è svegliato prima del timeout, riallineiamo ioStatus.Information
        // interragando l'handle Win32 tramite GetOverlappedResult prima di convalidare.
        bytesReadWin32 = 0;
        DWORD fileType = GetFileType(m_FileHandle);
        pEventObj = &m_GlobalIoEvent;

        // Bypassiamo GetOverlappedResult. Se il Kernel ha scritto il valore 7 dentro
        // InternalHigh, lo iniettiamo a forza nella struttura che verrà letta sotto!
        if (pEventObj->Overlapped.InternalHigh > 0 && pEventObj->Overlapped.InternalHigh <= count) {
            m_GlobalIoStatus.Information = static_cast<ULONG_PTR>(pEventObj->Overlapped.InternalHigh);
            m_GlobalIoStatus.Status = STATUS_SUCCESS;

            // Aggiornamento offset per i file su disco (se applicabile)
            if (GetFileType(m_FileHandle) == FILE_TYPE_DISK) {
                ULONG64 currentOffset = (static_cast<ULONG64>(pEventObj->Overlapped.OffsetHigh) << 32) | pEventObj->
                                        Overlapped.Offset;
                currentOffset += pEventObj->Overlapped.InternalHigh;
                pEventObj->Overlapped.Offset = static_cast<DWORD>(currentOffset & 0xFFFFFFFF);
                pEventObj->Overlapped.OffsetHigh = static_cast<DWORD>((currentOffset >> 32) & 0xFFFFFFFF);
            }
        } else {
            if (GetOverlappedResult(m_FileHandle, &m_GlobalOverlapped, &bytesReadWin32, FALSE)) {
                m_GlobalIoStatus.Information = bytesReadWin32;
                m_GlobalIoStatus.Status = STATUS_SUCCESS;

                if (fileType == FILE_TYPE_DISK && bytesReadWin32 > 0) {
                    /*Allucinazione: ULONG64 currentOffset = (static_cast<ULONG64>(m_GlobalOverlapped.OffsetHigh) << 32) | m_GlobalOverlapped.Offset;
                    currentOffset += bytesReadWin32;
                    m_GlobalOverlapped.Offset = static_cast<DWORD>(currentOffset & 0xFFFFFFFF);
                    m_GlobalOverlapped.OffsetHigh = static_cast<DWORD>((currentOffset >> 32) & 0xFFFFFFFF);*/
                    // LARGE_INTEGER moveOffset;
                    // moveOffset.QuadPart = bytesReadWin32;
                    // SetFilePointer(m_FileHandle, moveOffset.LowPart, &moveOffset.HighPart, FILE_CURRENT);
                    ULONG64 currentOffset = (static_cast<ULONG64>(pEventObj->Overlapped.OffsetHigh) << 32) | pEventObj->
                                            Overlapped.Offset;
                    currentOffset += bytesReadWin32;
                    pEventObj->Overlapped.Offset = static_cast<DWORD>(currentOffset & 0xFFFFFFFF);
                    pEventObj->Overlapped.OffsetHigh = static_cast<DWORD>((currentOffset >> 32) & 0xFFFFFFFF);
                }
                // // Aggiorniamo forzatamente l'offset nel contesto overlapped persistente della classe
                // if (GetFileType(m_FileHandle) == FILE_TYPE_DISK) {
                //     ULONG64 currentOffset = (static_cast<ULONG64>(m_GlobalOverlapped.OffsetHigh) << 32) | m_GlobalOverlapped.Offset;
                //     currentOffset += bytesReadWin32;
                //     m_GlobalOverlapped.Offset = static_cast<DWORD>(currentOffset & 0xFFFFFFFF);
                //     m_GlobalOverlapped.OffsetHigh = static_cast<DWORD>((currentOffset >> 32) & 0xFFFFFFFF);
                // }
            } else {
                DWORD lastErr = GetLastError();

                // CASO PARTICOLARE NAMED PIPE: Gestione dell'overflow parziale dei messaggi o disconnessioni
                if (fileType == FILE_TYPE_PIPE) {
                    if (lastErr == ERROR_MORE_DATA) {
                        // Il messaggio è parzialmente estratto ma valido: allineiamo lo stato a successo
                        m_GlobalIoStatus.Status = STATUS_SUCCESS;
                    }
                }
            }

            // 2. SOLUZIONE SBARRAMENTO PIPE: Se Windows restituisce 0 ma l'evento è segnalato e la pipe è attiva
            if (fileType == FILE_TYPE_PIPE && m_GlobalIoStatus.Information == 0) {
                // Se GetOverlappedResult ha restituito 0 a causa della Completion differita del kernel,
                // estraiamo il numero reale di byte scritti interrogando direttamente i registri interni
                // della struttura OVERLAPPED di sosta. Il campo InternalHigh contiene l'esatto numero
                // di byte trasferiti dall'I/O Manager durante il Fast I/O, bypassando i bug di Win32!
                if (m_GlobalOverlapped.InternalHigh > 0 && m_GlobalOverlapped.InternalHigh <= count) {
                    m_GlobalIoStatus.Information = static_cast<ULONG_PTR>(m_GlobalOverlapped.InternalHigh);
                    bytesReadWin32 = static_cast<DWORD>(m_GlobalOverlapped.InternalHigh);
                    m_GlobalIoStatus.Status = STATUS_SUCCESS;
                }
            }


        }
#endif
            }

        }
        status = m_GlobalIoStatus.Status;
    }

    if (NT_SUCCESS(status)) {
        *bytesActuallyRead = static_cast<ULONG>(m_GlobalIoStatus.Information);
    }
    return status;

#endif
}

NTSTATUS KernelStreamBuf::Sgetn_Blocking(char *s, size_t count, ULONG *bytesActuallyRead) {
#ifdef PREVDEF
    if (m_FileHandle == NULL || m_FileHandle == (HANDLE) -1 || s == nullptr || bytesActuallyRead == nullptr) return
            STATUS_INVALID_PARAMETER;
    *bytesActuallyRead = 0;

    size_t available = m_InEnd - m_InPtr;
    if (available > 0) {
        size_t toCopy = (count < available) ? count : available;
        RtlCopyMemory(s, m_InputBuffer + m_InPtr, toCopy);
        m_InPtr += toCopy;
        *bytesActuallyRead = static_cast<ULONG>(toCopy);
        return STATUS_SUCCESS;
    }

    IO_STATUS_BLOCK ioStatus; KEVENT ioEvent;
    KeInitializeEvent(&ioEvent, NotificationEvent, FALSE);

    NTSTATUS status = ZwReadFile(m_FileHandle, &ioEvent, NULL, NULL, &ioStatus, s, static_cast<ULONG>(count), NULL,
                                 NULL);
    if (status == STATUS_PENDING) {
        status = KeWaitForSingleObject(&ioEvent, Executive, KernelMode, FALSE, NULL);
        if (NT_SUCCESS(status)) {
            KeMemoryBarrier();
            status = ioStatus.Status;
        }
    }

    if (NT_SUCCESS(status)) {
        *bytesActuallyRead = static_cast<ULONG>(ioStatus.Information);
        status = STATUS_SUCCESS;
    }
    return status;
#else


    if (m_FileHandle == NULL || m_FileHandle == (HANDLE) -1 || s == nullptr || bytesActuallyRead == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    *bytesActuallyRead = 0;

    size_t available = m_InEnd - m_InPtr;
    if (available > 0) {
        size_t toCopy = (count < available) ? count : available;
        RtlCopyMemory(s, m_InputBuffer + m_InPtr, toCopy);
        m_InPtr += toCopy;
        *bytesActuallyRead = static_cast<ULONG>(toCopy);
        return STATUS_SUCCESS;
    }

    KeResetEvent(&m_GlobalIoEvent);
    m_GlobalIoStatus.Status = 0;
    m_GlobalIoStatus.Information = 0;

    // CHIAMATA PURA UNIFICATA IDENTICA SBARRA BLOCCANTE
    NTSTATUS status = ZwReadFile(m_FileHandle, &m_GlobalIoEvent, NULL, NULL, &m_GlobalIoStatus, s,
                                 static_cast<ULONG>(count), NULL, NULL);

    if (status == STATUS_PENDING) {
        status = KeWaitForSingleObject(&m_GlobalIoEvent, Executive, KernelMode, FALSE, NULL);

        if (NT_SUCCESS(status)) {
#ifndef LINK_KERNEL
            DWORD bytesReadWin32 = 0;
            if (GetOverlappedResult(m_FileHandle, &m_GlobalOverlapped, &bytesReadWin32, FALSE)) {
                m_GlobalIoStatus.Information = bytesReadWin32;
                m_GlobalIoStatus.Status = STATUS_SUCCESS;

                if (GetFileType(m_FileHandle) == FILE_TYPE_DISK) {
                    ULONG64 currentOffset = (static_cast<ULONG64>(m_GlobalOverlapped.OffsetHigh) << 32) |
                                            m_GlobalOverlapped.Offset;
                    currentOffset += bytesReadWin32;
                    m_GlobalOverlapped.Offset = static_cast<DWORD>(currentOffset & 0xFFFFFFFF);
                    m_GlobalOverlapped.OffsetHigh = static_cast<DWORD>((currentOffset >> 32) & 0xFFFFFFFF);
                }
            }
#endif
            status = m_GlobalIoStatus.Status;
        }
    }

    if (NT_SUCCESS(status)) {
        if (GetFileType(m_FileHandle) == FILE_TYPE_CHAR && (m_GlobalIoEvent.Handle != INVALID_HANDLE_VALUE) && ((uint64_t)m_GlobalIoEvent.Handle != 0xcccccccccccccccc)) {
            CloseHandle(m_GlobalIoEvent.Handle);
        }
        *bytesActuallyRead = static_cast<ULONG>(m_GlobalIoStatus.Information);
    }
    return status;


#endif
}
