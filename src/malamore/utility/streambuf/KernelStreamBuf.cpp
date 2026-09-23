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
    // ... [Ramo PREVDEF originario immutato] ...
    return STATUS_SUCCESS;
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

    ULONG asyncContextKey = ASYNC_READ_TAG;
    ULONG *keyptr = nullptr;
#ifndef LINK_KERNEL
    keyptr = &asyncContextKey;
#endif

#ifndef LINK_KERNEL
    // BONIFICA PREVENTIVA: Elimina i fantasmi del passato azzerando i registri prima dell'I/O
    m_GlobalIoEvent.Overlapped.Internal = STATUS_PENDING;
    m_GlobalIoEvent.Overlapped.InternalHigh = 0;
#endif

    // LANCIO DELL'I/O ASINCRONO OVERLAPPED
    NTSTATUS status = ZwReadFile(m_FileHandle, &m_GlobalIoEvent, NULL, NULL, &m_GlobalIoStatus, s,
                                 static_cast<ULONG>(count), NULL, keyptr);

    if (status == STATUS_PENDING) {
        DWORD bytesReadWin32 = 0;
        PKEVENT pEventObj = &m_GlobalIoEvent;

#ifndef LINK_KERNEL
        // CONTROLLO PRE-ATTESA VELOCE (FAST I/O INLINE): Esce subito se i byte sono già pronti
        if (GetOverlappedResult(m_FileHandle, &pEventObj->Overlapped, &bytesReadWin32, FALSE)) { // Forzato FALSE!
            m_GlobalIoStatus.Information = bytesReadWin32;
            m_GlobalIoStatus.Status = STATUS_SUCCESS;
            status = STATUS_SUCCESS;
        } else if (pEventObj->Overlapped.InternalHigh > 0 && pEventObj->Overlapped.InternalHigh <= count) {
            m_GlobalIoStatus.Information = static_cast<ULONG_PTR>(pEventObj->Overlapped.InternalHigh);
            m_GlobalIoStatus.Status = STATUS_SUCCESS;
            status = STATUS_SUCCESS;
        }
#endif

        if (status == STATUS_PENDING) {
            LARGE_INTEGER kernelTimeout;
            // Configurazione della sbarra oraria (es: 1 secondo = 1.000.000 Us)
            kernelTimeout.QuadPart = -(LONGLONG)(timeoutUs * 10);

            // Attesa non attiva guidata dallo scheduler dell'OS
            NTSTATUS waitStatus = KeWaitForSingleObject(&m_GlobalIoEvent, Executive, KernelMode, FALSE, &kernelTimeout);

            if (waitStatus == STATUS_TIMEOUT) {
                KeMemoryBarrier(); // Sincronizzazione atomica dei core logici della CPU

#ifndef LINK_KERNEL
                // SBARRA TRANSAZIONALE DI EMERGENZA PRE-TIMEOUT: Ispezione non bloccante (FALSE)
                // Se il Kernel ha scritto i 7 byte un istante prima della scadenza, salviamo il pacchetto!
                DWORD bytesCancelledRead = 0;
                if (GetOverlappedResult(m_FileHandle, &pEventObj->Overlapped, &bytesCancelledRead, FALSE)) { // RIGOROSAMENTE FALSE!
                    if (bytesCancelledRead > 0 && bytesCancelledRead <= count) {
                        m_GlobalIoStatus.Information = bytesCancelledRead;
                        m_GlobalIoStatus.Status = STATUS_SUCCESS;
                        status = STATUS_SUCCESS;
                    }
                }

                // Fallback diretto sul registro di stato hardware in RAM
                if (status != STATUS_SUCCESS && pEventObj->Overlapped.InternalHigh > 0 && pEventObj->Overlapped.InternalHigh <= count) {
                    m_GlobalIoStatus.Information = static_cast<ULONG_PTR>(pEventObj->Overlapped.InternalHigh);
                    m_GlobalIoStatus.Status = STATUS_SUCCESS;
                    status = STATUS_SUCCESS;
                }
#endif

                // SE IL TIMEOUT È REALE: Evacuazione protetta della sessione per mantenere la coerenza
                if (status != STATUS_SUCCESS) {
                    ZwCancelIoFile(m_FileHandle, &m_GlobalIoStatus); // Abort dell'IRP nel Kernel
                    Clear(); // Azzera indici e Put/Get Area logica per i cicli futuri della staffetta
                    return STATUS_TIMEOUT; // Ritorna il timeout in sicurezza senza blocchi!
                }
            }

            // SE L'EVENTO SI È SVEGLIATO REGOLARMENTE PRIMA DELLA SCADENZA
            if (status == STATUS_PENDING) {
                KeMemoryBarrier();
#ifndef LINK_KERNEL
                DWORD fileType = GetFileType(m_FileHandle);
                bytesReadWin32 = 0;

                // RIPRISTINO DELL'HANDSHAKING CORRETTO: Interroghiamo la struttura pEventObj->Overlapped inviata al Kernel
                // ed imponiamo il flag FALSE per escludere qualsiasi blocco infinito parassita!
                if (GetOverlappedResult(m_FileHandle, &pEventObj->Overlapped, &bytesReadWin32, FALSE)) { // CORRETTO DA m_GlobalOverlapped A pEventObj->Overlapped!
                    m_GlobalIoStatus.Information = bytesReadWin32;
                    m_GlobalIoStatus.Status = STATUS_SUCCESS;

                    // Avanzamento offset se operiamo su file regolari su disco
                    if (fileType == FILE_TYPE_DISK && bytesReadWin32 > 0) {
                        ULONG64 currentOffset = (static_cast<ULONG64>(pEventObj->Overlapped.OffsetHigh) << 32) | pEventObj->Overlapped.Offset;
                        currentOffset += bytesReadWin32;
                        pEventObj->Overlapped.Offset = static_cast<DWORD>(currentOffset & 0xFFFFFFFF);
                        pEventObj->Overlapped.OffsetHigh = static_cast<DWORD>((currentOffset >> 32) & 0xFFFFFFFF);
                    }
                } else {
                    // Fallback sul registro hardware in RAM per le Named Pipe asincrone
                    if (pEventObj->Overlapped.InternalHigh > 0 && pEventObj->Overlapped.InternalHigh <= count) {
                        m_GlobalIoStatus.Information = static_cast<ULONG_PTR>(pEventObj->Overlapped.InternalHigh);
                        m_GlobalIoStatus.Status = STATUS_SUCCESS;
                    } else if (GetLastError() == ERROR_MORE_DATA && fileType == FILE_TYPE_PIPE) {
                        m_GlobalIoStatus.Status = STATUS_SUCCESS; // Gestione pacchetti parziali
                    }
                }
#endif
                status = m_GlobalIoStatus.Status;
            }
        }
    }

    if (NT_SUCCESS(status)) {
        *bytesActuallyRead = static_cast<ULONG>(m_GlobalIoStatus.Information); //
    }
    return status; //
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
        *bytesActuallyRead = static_cast<ULONG>(m_GlobalIoStatus.Information);
    }
    return status;


#endif
}
