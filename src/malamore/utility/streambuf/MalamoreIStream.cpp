//
// Created by mbda on 16/09/2026.
//

//
// Created by Giacomo Bergami on 16/09/2026.
//

#include "malamore/utility/streambuf/MalamoreIStream.h"

MalamoreIStream::MalamoreIStream() : m_IsOpen(FALSE) {
    RtlZeroMemory(&m_UnicodePath, sizeof(UNICODE_STRING));
    RtlZeroMemory(m_SavedPath, sizeof(m_SavedPath));
}

MalamoreIStream::~MalamoreIStream() {
    if (m_IsOpen) Close();
}

NTSTATUS MalamoreIStream::Open(const MalamorePath& path, ULONG flags) {
    if (m_IsOpen) return STATUS_SUCCESS;
    if (path.empty()) return STATUS_INVALID_PARAMETER;

    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    OBJECT_ATTRIBUTES objAttr;
    HANDLE hFile = NULL;

    status = RtlStringCchCopyW(m_SavedPath, MAX_PATH, path.ToPath(m_SavedPath, MAX_PATH));
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&m_UnicodePath, m_SavedPath);
    InitializeObjectAttributes(&objAttr, &m_UnicodePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    // =========================================================================
    // MOTORE DI TRADUZIONE: DA LOGICA BITMASK POSIX A STRATO HARDWARE WINDOWS NT
    // =========================================================================
    ACCESS_MASK desiredAccess = SYNCHRONIZE;
    ULONG nativeDisposition = 0;

    // 1. Risoluzione dei flag di Accesso (Read, Write, Append)
    if (flags & StreamFlags::Append) {
        desiredAccess |= FILE_APPEND_DATA;
    } else if (flags & StreamFlags::Write) {
        desiredAccess |= FILE_WRITE_DATA;
    }
    if (flags & StreamFlags::Read) {
        desiredAccess |= GENERIC_READ;
    }

    // 2. Risoluzione ortogonale delle regole di Creazione e Troncamento (Creat, Trunc)
    if (flags & StreamFlags::Trunc) {
        // Se richiede troncamento, equivale a forzare la sovrascrittura totale (FILE_SUPERSEDE)
        nativeDisposition = FILE_SUPERSEDE; //0x00000000; // FILE_SUPERSEDE nel WDK
    } else if (flags & StreamFlags::Creat) {
        // Se la creazione è attiva ma non si vuole troncare, equivale ad aprire o creare (FILE_OPEN_IF)
        nativeDisposition = FILE_OPEN_IF; // FILE_OPEN_IF nel WDK
    } else {
        // Se la creazione non è richiesta, il file deve tassativamente già esistere (FILE_OPEN)
        nativeDisposition = FILE_OPEN; // FILE_OPEN nel WDK
    }

    status = ZwCreateFile(&hFile, desiredAccess, &objAttr, &ioStatus, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, nativeDisposition, FILE_NON_DIRECTORY_FILE, NULL, 0);

    if (NT_SUCCESS(status)) {
        m_StreamBuf.AttachHandle(hFile);
        m_IsOpen = TRUE;
    }
    return status;
}

VOID MalamoreIStream::Close() {
    if (!m_IsOpen) return;
    HANDLE hFile = m_StreamBuf.GetHandle();
    if (hFile != NULL && hFile != (HANDLE)-1) ZwClose(hFile);
    m_StreamBuf.AttachHandle(NULL);
    m_IsOpen = FALSE;
}

int MalamoreIStream::Get() {
    return m_StreamBuf.Sbumpc();
}

MalamoreIStream& MalamoreIStream::operator>>(char& c) {
    int ch = m_StreamBuf.Sbumpc();
    c = (ch != -1) ? static_cast<char>(ch) : '\0';
    return *this;
}

MalamoreIStream& MalamoreIStream::operator>>(ULONG& value) {
    value = 0;
    int ch = m_StreamBuf.Sgetc();

    // Salta eventuali spazi o ritorni a capo iniziali
    while (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
        m_StreamBuf.Sbumpc();
        ch = m_StreamBuf.Sgetc();
    }

    // Accumula i caratteri numerici validi
    while (ch >= '0' && ch <= '9') {
        value = (value * 10) + (ch - '0');
        m_StreamBuf.Sbumpc();
        ch = m_StreamBuf.Sgetc();
    }
    return *this;
}

size_t MalamoreIStream::GetLine(char* buffer, size_t maxCount, char delimiter) {
    if (buffer == nullptr || maxCount == 0) return 0;
    size_t count = 0;

    while (count < (maxCount - 1)) {
        int ch = m_StreamBuf.Sbumpc();
        if (ch == -1) break; // EOF raggiunto

        if (static_cast<char>(ch) == delimiter) {
            break; // Trovato il delimitatore di fine riga
        }

        // Ignora il carriage return se stiamo cercando il salto riga ordinario
        if (static_cast<char>(ch) == '\r' && delimiter == '\n') continue;

        buffer[count++] = static_cast<char>(ch);
    }

    buffer[count] = '\0'; // Terminazione della stringa di Ring 0
    return count;
}


/**
 * LETTURA PERIODICA TIMED CON ANNULLAMENTO COMPENSATO (RING 0 / RING 3)
 * Nota: Deve essere eseguita esclusivamente a PASSIVE_LEVEL
 */
NTSTATUS MalamoreIStream::ReadWithTimeout(
    VOID* outBuffer,
    ULONG bytesToRead,
    ULONG timeoutUs,
    ULONG* bytesActuallyRead
) {
    if (!m_IsOpen || outBuffer == nullptr || bytesActuallyRead == nullptr || bytesToRead == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    *bytesActuallyRead = 0;
    HANDLE hFile = m_StreamBuf.GetHandle();
    if (hFile == NULL || hFile == (HANDLE)-1) return STATUS_INVALID_DEVICE_STATE;

    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    KEVENT ioEvent;

    // 1. Inizializzazione dell'evento hardware di sincronizzazione asincrona
    KeInitializeEvent(&ioEvent, NotificationEvent, FALSE);

    // 2. Lancio della lettura asincrona non bloccante
    // Passiamo l'indirizzo di &ioEvent. L'I/O Manager di WindowsNT metterà in coda l'IRP
    // e sveglierà l'evento non appena l'hardware caricherà i dati nel buffer.
    status = ZwReadFile(
        hFile,
        &ioEvent,                   // Handle dell'evento da segnalare al completamento (Ring 0)
        NULL, NULL,                 // ApcRoutine, ApcContext
        &ioStatus,
        outBuffer,                  // Buffer lineare di ricezione passato dall'utente
        bytesToRead,                // Dimensione massima richiesta
        NULL,                       // Legge dalla posizione corrente del puntatore file/stream
        NULL
    );

    // 3. SE L'I/O ENTRA IN CODA ASINCRONA (STATUS_PENDING)
    if (status == STATUS_PENDING) {
        LARGE_INTEGER kernelTimeout;
        // Conversione esplicita inline da microsecondi alle unità da 100ns negative del WDK
        kernelTimeout.QuadPart = -(LONGLONG)(timeoutUs * 10);

        // Attesa controllata: Blocca il core logico SOLO per il budget concesso dall'utente
        NTSTATUS waitStatus = KeWaitForSingleObject(&ioEvent, Executive, KernelMode, FALSE, &kernelTimeout);

        if (waitStatus == STATUS_TIMEOUT) {
            // HARDWARE TIMEOUT COMPENSATO: La periferica o il file non hanno risposto in tempo.
            // Forza l'annullamento immediato dell'IRP orfano per non lasciare descrittori corrotti nel kernel.
            ZwCancelIoFile(hFile, &ioStatus);
            return STATUS_TIMEOUT;
        } else {
            // L'evento si è svegliato prima dello scadere del timeout!
            // Estraiamo il codice finale reale iniettato dall'I/O Manager nel blocco di stato.
            status = ioStatus.Status;
        }
    }

    // 4. VERIFICA E CONTEGGIO DEI BYTE EFFETTIVAMENTE EXTRARE
    if (NT_SUCCESS(status)) {
        *bytesActuallyRead = static_cast<ULONG>(ioStatus.Information);
        return STATUS_SUCCESS;
    }

    return status;
}


/**
 * LETTURA ASINCRONA CON ATTESA BLOCCANTE INFINITA (GIRA A PASSIVE_LEVEL)
 */
NTSTATUS MalamoreIStream::ReadBlocking(
    VOID* outBuffer,
    ULONG bytesToRead,
    ULONG* bytesActuallyRead
) {
    if (!m_IsOpen || outBuffer == nullptr || bytesActuallyRead == nullptr || bytesToRead == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    *bytesActuallyRead = 0;
    HANDLE hFile = m_StreamBuf.GetHandle();
    if (hFile == NULL || hFile == (HANDLE)-1) return STATUS_INVALID_DEVICE_STATE;

    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    KEVENT ioEvent;

    // 1. Inizializzazione dell'evento per la segnalazione asincrona
    KeInitializeEvent(&ioEvent, NotificationEvent, FALSE);

    // 2. Lancio della lettura asincrona non bloccante legata all'evento
    status = ZwReadFile(
        hFile,
        &ioEvent,                   // L'I/O Manager sveglierà questo evento al termine
        NULL, NULL,
        &ioStatus,
        outBuffer,
        bytesToRead,
        NULL,
        NULL
    );

    // 3. SE L'OPERAZIONE VA IN CODA (STATUS_PENDING)
    if (status == STATUS_PENDING) {
        // SCELTA ARCHITETTURALE: Passiamo NULL come quinto parametro (Timeout).
        // Questo dice al processore di congelare il thread in modo indefinito finché
        // il bus dati hardware non ha completato il travaso dei byte nella cache.
        status = KeWaitForSingleObject(&ioEvent, Executive, KernelMode, FALSE, NULL); // <--- NULL = ATTESA INFINITA

        if (NT_SUCCESS(status)) {
            // Estraiamo il codice finale immesso dall'hardware nel blocco di stato
            status = ioStatus.Status;
        }
    }

    // 4. CONTEGGIO FINALE DEI BYTE ACQUISITI
    if (NT_SUCCESS(status)) {
        *bytesActuallyRead = static_cast<ULONG>(ioStatus.Information);
        return STATUS_SUCCESS;
    }

    return status;
}
