//
// Created by mbda on 16/09/2026.
//

//
// Created by Giacomo Bergami on 16/09/2026.
//

#include "malamore/utility/streambuf//KernelStreamBuf.h"

KernelStreamBuf::KernelStreamBuf()
    : m_FileHandle(NULL), m_InBase(0), m_InPtr(0), m_InEnd(0), m_OutPtr(0)
{
    RtlZeroMemory(m_InputBuffer, STREAM_BUFFER_SIZE);
    RtlZeroMemory(m_OutputBuffer, STREAM_BUFFER_SIZE);
}

KernelStreamBuf::~KernelStreamBuf() {
    FlushOutput();
}

void KernelStreamBuf::AttachHandle(HANDLE hFile) {
    m_FileHandle = hFile;
    m_InBase = 0; m_InPtr = 0; m_InEnd = 0;
    m_OutPtr = 0;
}

bool KernelStreamBuf::FlushOutput() {
    if (m_OutPtr == 0) return true;
    if (m_FileHandle == NULL || m_FileHandle == (HANDLE)-1) return false;

    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status = ZwWriteFile(m_FileHandle, NULL, NULL, NULL, &ioStatus, m_OutputBuffer, static_cast<ULONG>(m_OutPtr), NULL, NULL);

    if (NT_SUCCESS(status)) {
        m_OutPtr = 0;
        RtlZeroMemory(m_OutputBuffer, STREAM_BUFFER_SIZE);
        return true;
    }
    return false;
}

bool KernelStreamBuf::Sputc(char c) {
    if (m_OutPtr >= STREAM_BUFFER_SIZE) {
        if (!FlushOutput()) return false;
    }
    m_OutputBuffer[m_OutPtr++] = c;
    return true;
}

bool KernelStreamBuf::Sputn(const char* s, size_t count) {
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

    if (m_FileHandle == NULL || m_FileHandle == (HANDLE)-1) return -1;

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

NTSTATUS KernelStreamBuf::Sgetn_Timeout(char* s, size_t count, ULONG timeoutUs, ULONG* bytesActuallyRead) {
    if (m_FileHandle == NULL || m_FileHandle == (HANDLE)-1 || s == nullptr || bytesActuallyRead == nullptr) return STATUS_INVALID_PARAMETER;
    *bytesActuallyRead = 0;

    // Se ci sono già byte pronti nella get area della cache staticizzata, li svuotiamo subito
    size_t available = m_InEnd - m_InPtr;
    if (available > 0) {
        size_t toCopy = (count < available) ? count : available;
        RtlCopyMemory(s, m_InputBuffer + m_InPtr, toCopy);
        m_InPtr += toCopy; *bytesActuallyRead = static_cast<ULONG>(toCopy);
        return STATUS_SUCCESS;
    }

    IO_STATUS_BLOCK ioStatus; KEVENT ioEvent;
    KeInitializeEvent(&ioEvent, NotificationEvent, FALSE);

    NTSTATUS status = ZwReadFile(m_FileHandle, &ioEvent, NULL, NULL, &ioStatus, s, static_cast<ULONG>(count), NULL, NULL);
    if (status == STATUS_PENDING) {
        LARGE_INTEGER kernelTimeout; kernelTimeout.QuadPart = -(LONGLONG)(timeoutUs * 10);
        NTSTATUS waitStatus = KeWaitForSingleObject(&ioEvent, Executive, KernelMode, FALSE, &kernelTimeout);
        if (waitStatus == STATUS_TIMEOUT) {
            ZwCancelIoFile(m_FileHandle, &ioStatus); Clear(); // Svuota gli indici post-cancellazione
            return STATUS_TIMEOUT;
        }
        status = ioStatus.Status;
    }

    if (NT_SUCCESS(status)) *bytesActuallyRead = static_cast<ULONG>(ioStatus.Information);
    return status;
}

NTSTATUS KernelStreamBuf::Sgetn_Blocking(char* s, size_t count, ULONG* bytesActuallyRead) {
    if (m_FileHandle == NULL || m_FileHandle == (HANDLE)-1 || s == nullptr || bytesActuallyRead == nullptr) return STATUS_INVALID_PARAMETER;
    *bytesActuallyRead = 0;

    size_t available = m_InEnd - m_InPtr;
    if (available > 0) {
        size_t toCopy = (count < available) ? count : available;
        RtlCopyMemory(s, m_InputBuffer + m_InPtr, toCopy);
        m_InPtr += toCopy; *bytesActuallyRead = static_cast<ULONG>(toCopy);
        return STATUS_SUCCESS;
    }

    IO_STATUS_BLOCK ioStatus; KEVENT ioEvent;
    KeInitializeEvent(&ioEvent, NotificationEvent, FALSE);

    NTSTATUS status = ZwReadFile(m_FileHandle, &ioEvent, NULL, NULL, &ioStatus, s, static_cast<ULONG>(count), NULL, NULL);
    if (status == STATUS_PENDING) {
        status = KeWaitForSingleObject(&ioEvent, Executive, KernelMode, FALSE, NULL);
        if (NT_SUCCESS(status)) status = ioStatus.Status;
    }

    if (NT_SUCCESS(status)) *bytesActuallyRead = static_cast<ULONG>(ioStatus.Information);
    return status;
}
