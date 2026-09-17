//
// Created by mbda on 16/09/2026.
//

//
// Created by Giacomo Bergami on 16/09/2026.
//

#include "malamore/utility/streambuf/MalamoreIStream.h"

MalamoreIStream::MalamoreIStream() {

}

MalamoreIStream::~MalamoreIStream() {
    original.Close();
}

NTSTATUS MalamoreIStream::Open(const MalamorePath& path, ULONG flags) {
    return original.Open(path, flags);
    // if (m_IsOpen) return STATUS_SUCCESS;
    // if (path.empty()) return STATUS_INVALID_PARAMETER;
    //
    // NTSTATUS status;
    // IO_STATUS_BLOCK ioStatus;
    // OBJECT_ATTRIBUTES objAttr;
    // HANDLE hFile = NULL;
    //
    // status = RtlStringCchCopyW(m_SavedPath, MAX_PATH, path.ToPath(m_SavedPath, MAX_PATH));
    // if (!NT_SUCCESS(status)) return status;
    //
    // RtlInitUnicodeString(&m_UnicodePath, m_SavedPath);
    // InitializeObjectAttributes(&objAttr, &m_UnicodePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    //
    // // =========================================================================
    // // MOTORE DI TRADUZIONE: DA LOGICA BITMASK POSIX A STRATO HARDWARE WINDOWS NT
    // // =========================================================================
    //
    // ULONG nativeDisposition = 0;
    // bool isAppend = (flags & StreamFlags::Append) == StreamFlags::Append;
    // bool isHW = path.is_hardware_device();
    // ACCESS_MASK desiredAccess = isHW ? SYNCHRONIZE : 0;
    //
    // // 1. Risoluzione dei flag di Accesso (Read, Write, Append)
    // if (flags & StreamFlags::Append) {
    //     desiredAccess |= FILE_APPEND_DATA;
    // } else if (flags & StreamFlags::Write) {
    //     desiredAccess |= isHW ? GENERIC_WRITE : FILE_WRITE_DATA;
    // }
    // if (flags & StreamFlags::Read) {
    //     desiredAccess |= isHW ? GENERIC_READ : FILE_READ_DATA;
    // }
    //
    // // 2. Risoluzione ortogonale delle regole di Creazione e Troncamento (Creat, Trunc)
    // if (flags & StreamFlags::Trunc) {
    //     // Se richiede troncamento, equivale a forzare la sovrascrittura totale (FILE_SUPERSEDE)
    //     nativeDisposition = FILE_SUPERSEDE; //0x00000000; // FILE_SUPERSEDE nel WDK
    // } else if (flags & StreamFlags::Creat) {
    //     // Se la creazione è attiva ma non si vuole troncare, equivale ad aprire o creare (FILE_OPEN_IF)
    //     nativeDisposition = FILE_OPEN_IF; // FILE_OPEN_IF nel WDK
    // } else {
    //     // Se la creazione non è richiesta, il file deve tassativamente già esistere (FILE_OPEN)
    //     nativeDisposition = FILE_OPEN; // FILE_OPEN nel WDK
    // }
    //
    // status = ZwCreateFile(&hFile, desiredAccess, &objAttr, &ioStatus, NULL,
    //                       FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, nativeDisposition, FILE_NON_DIRECTORY_FILE, NULL, 0);
    //
    // if (NT_SUCCESS(status)) {
    //     m_StreamBuf.AttachHandle(hFile);
    //     m_IsOpen = TRUE;
    // }
    // return status;
}

VOID MalamoreIStream::Close() {
    original.Close();
}

int MalamoreIStream::Get() {
    return  original.Get();
}

MalamoreIStream& MalamoreIStream::operator>>(char& c) {
    original >> c;
    return *this;
}

MalamoreIStream& MalamoreIStream::operator>>(ULONG& value) {
    original >> value;
    return *this;
}

size_t MalamoreIStream::GetLine(char* buffer, size_t maxCount, char delimiter) {
    return original.GetLine(buffer, maxCount, delimiter);
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
    return original.ReadWithTimeout(outBuffer, bytesToRead, timeoutUs, bytesActuallyRead);

}


/**
 * LETTURA ASINCRONA CON ATTESA BLOCCANTE INFINITA (GIRA A PASSIVE_LEVEL)
 */
NTSTATUS MalamoreIStream::ReadBlocking(
    VOID* outBuffer,
    ULONG bytesToRead,
    ULONG* bytesActuallyRead
) {
    return original.ReadBlocking(outBuffer, bytesToRead, bytesActuallyRead);
    // if (!m_IsOpen || outBuffer == nullptr || bytesActuallyRead == nullptr || bytesToRead == 0) {
    //     return STATUS_INVALID_PARAMETER;
    // }
    //
    // *bytesActuallyRead = 0;
    // HANDLE hFile = m_StreamBuf.GetHandle();
    // if (hFile == NULL || hFile == (HANDLE)-1) return STATUS_INVALID_DEVICE_STATE;
    //
    // NTSTATUS status;
    // IO_STATUS_BLOCK ioStatus;
    // KEVENT ioEvent;
    //
    // // 1. Inizializzazione dell'evento per la segnalazione asincrona
    // KeInitializeEvent(&ioEvent, NotificationEvent, FALSE);
    //
    // // 2. Lancio della lettura asincrona non bloccante legata all'evento
    // status = ZwReadFile(
    //     hFile,
    //     &ioEvent,                   // L'I/O Manager sveglierà questo evento al termine
    //     NULL, NULL,
    //     &ioStatus,
    //     outBuffer,
    //     bytesToRead,
    //     NULL,
    //     NULL
    // );
    //
    // // 3. SE L'OPERAZIONE VA IN CODA (STATUS_PENDING)
    // if (status == STATUS_PENDING) {
    //     // SCELTA ARCHITETTURALE: Passiamo NULL come quinto parametro (Timeout).
    //     // Questo dice al processore di congelare il thread in modo indefinito finché
    //     // il bus dati hardware non ha completato il travaso dei byte nella cache.
    //     status = KeWaitForSingleObject(&ioEvent, Executive, KernelMode, FALSE, NULL); // <--- NULL = ATTESA INFINITA
    //
    //     if (NT_SUCCESS(status)) {
    //         // Estraiamo il codice finale immesso dall'hardware nel blocco di stato
    //         status = ioStatus.Status;
    //     }
    // }
    //
    // // 4. CONTEGGIO FINALE DEI BYTE ACQUISITI
    // if (NT_SUCCESS(status)) {
    //     *bytesActuallyRead = static_cast<ULONG>(ioStatus.Information);
    //     return STATUS_SUCCESS;
    // }
    //
    // return status;
}
