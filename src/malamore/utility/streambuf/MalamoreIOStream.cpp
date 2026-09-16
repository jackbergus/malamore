//
// Created by Giacomo Bergami on 16/09/2026.
//

#include "malamore/utility/streambuf/MalamoreIOStream.h"
// #include <ntstrsafe.h>


//
// Created by Giacomo Bergami on 16/09/2026.
//


MalamoreIOStream::MalamoreIOStream()
    : m_IsOpen(FALSE), m_CurrentBufferIndex(0), m_InputLookaheadPtr(0), m_InputLookaheadEnd(0)
{
    RtlZeroMemory(&m_UnicodePath, sizeof(UNICODE_STRING));
    RtlZeroMemory(m_SavedPath, sizeof(m_SavedPath));
    RtlZeroMemory(m_OutputBuffer, sizeof(m_OutputBuffer));
    RtlZeroMemory(m_InputLookaheadBuffer, sizeof(m_InputLookaheadBuffer));
}

MalamoreIOStream::~MalamoreIOStream() {
    if (m_IsOpen) Close();
}

NTSTATUS MalamoreIOStream::Open(const MalamorePath& path, ULONG flags) {
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

    bool isHW = path.is_hardware_device();
    ACCESS_MASK desiredAccess = isHW ? SYNCHRONIZE : 0;
    ULONG nativeDisposition = 0;

    if (flags & StreamFlags::Append) desiredAccess |= FILE_APPEND_DATA;
    else if (flags & StreamFlags::Write) desiredAccess |= isHW ? GENERIC_WRITE : FILE_WRITE_DATA;
    if (flags & StreamFlags::Read) desiredAccess |= isHW ? GENERIC_READ : FILE_READ_DATA;

    if (flags & StreamFlags::Trunc) nativeDisposition = FILE_SUPERSEDE; // FILE_SUPERSEDE
    else if (flags & StreamFlags::Creat) nativeDisposition = FILE_OPEN_IF; // FILE_OPEN_IF
    else nativeDisposition =FILE_OPEN; // FILE_OPEN

    status = ZwCreateFile(&hFile, desiredAccess, &objAttr, &ioStatus, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE, nativeDisposition, FILE_NON_DIRECTORY_FILE, NULL, 0);

    if (NT_SUCCESS(status)) {
        m_StreamBuf.AttachHandle(hFile);
        m_IsOpen = TRUE;
        m_CurrentBufferIndex = 0;
        m_InputLookaheadPtr = 0;
        m_InputLookaheadEnd = 0;
    }
    return status;
}

VOID MalamoreIOStream::Close() {
    if (!m_IsOpen) return;
    Flush();
    HANDLE hFile = m_StreamBuf.GetHandle();
    if (hFile != NULL && hFile != (HANDLE)-1) ZwClose(hFile);
    m_StreamBuf.AttachHandle(NULL);
    m_IsOpen = FALSE;
}

bool MalamoreIOStream::Flush() {
    if (m_CurrentBufferIndex == 0) return true;
    bool success = m_StreamBuf.Sputn(m_OutputBuffer, m_CurrentBufferIndex);
    if (success) {
        success = m_StreamBuf.FlushOutput();
        m_CurrentBufferIndex = 0;
        RtlZeroMemory(m_OutputBuffer, sizeof(m_OutputBuffer));
    }
    return success;
}

// ... Metodi operatore << ed helper di scrittura rimangono identici ...
void MalamoreIOStream::AppendString(const char* str) {
    if (str == nullptr) return; size_t len = 0; if (!NT_SUCCESS(RtlStringCchLengthA(str, STREAM_BUFFER_SIZE, &len))) return;
    for (size_t i = 0; i < len; ++i) { if (m_CurrentBufferIndex >= STREAM_BUFFER_SIZE) Flush(); m_OutputBuffer[m_CurrentBufferIndex++] = str[i]; }
}
MalamoreIOStream& MalamoreIOStream::operator<<(const char* str) { AppendString(str); return *this; }
MalamoreIOStream& MalamoreIOStream::operator<<(char c) { if (m_CurrentBufferIndex >= STREAM_BUFFER_SIZE) Flush(); m_OutputBuffer[m_CurrentBufferIndex++] = c; return *this; }
MalamoreIOStream& MalamoreIOStream::operator<<(ULONG value) {     char temp[32]; RtlZeroMemory(temp, sizeof(temp));
    if (NT_SUCCESS(RtlStringCchPrintfA(temp, 32, "%lu", value))) AppendString(temp);
    return *this; }
MalamoreIOStream& MalamoreIOStream::operator<<(MalamoreIOStream& (*manipulator)(MalamoreIOStream&)) { return manipulator(*this); }

// =========================================================================
// PRIMITIVE DI LETTURA ASINCRONE INTEGRATE CON RESET POST-TIMEOUT
// =========================================================================

NTSTATUS MalamoreIOStream::ReadWithTimeout(VOID* outBuffer, ULONG bytesToRead, ULONG timeoutUs, ULONG* bytesActuallyRead) {
    if (!m_IsOpen || outBuffer == nullptr || bytesActuallyRead == nullptr || bytesToRead == 0) return STATUS_INVALID_PARAMETER;
    *bytesActuallyRead = 0;
    HANDLE hFile = m_StreamBuf.GetHandle();
    if (hFile == NULL || hFile == (HANDLE)-1) return STATUS_INVALID_DEVICE_STATE;

    NTSTATUS status; IO_STATUS_BLOCK ioStatus; KEVENT ioEvent;
    KeInitializeEvent(&ioEvent, NotificationEvent, FALSE);

    status = ZwReadFile(hFile, &ioEvent, NULL, NULL, &ioStatus, outBuffer, bytesToRead, NULL, NULL);

    if (status == STATUS_PENDING) {
        LARGE_INTEGER kernelTimeout; kernelTimeout.QuadPart = -(LONGLONG)(timeoutUs * 10);
        NTSTATUS waitStatus = KeWaitForSingleObject(&ioEvent, Executive, KernelMode, FALSE, &kernelTimeout);

        if (waitStatus == STATUS_TIMEOUT) {
            // SCATTO DEL TIMEOUT: Forza l'annullamento immediato dell'IRP/Overlapped orfano
            ZwCancelIoFile(hFile, &ioStatus);

            // SANIFICAZIONE HARDWARE VALIDA SIA IN RING 0 CHE IN RING 3:
            // Svuota e riallinea completamente i buffer logici dello StreamBuf per ripristinare il descrittore
            m_StreamBuf.AttachHandle(hFile);
            m_InputLookaheadPtr = 0;
            m_InputLookaheadEnd = 0;

            return STATUS_TIMEOUT; // Ritorna lasciando il condotto utilizzabile per i passi successivi
        } else {
            status = ioStatus.Status;
        }
    }

    if (NT_SUCCESS(status)) *bytesActuallyRead = static_cast<ULONG>(ioStatus.Information);
    return status;
}

NTSTATUS MalamoreIOStream::ReadBlocking(VOID* outBuffer, ULONG bytesToRead, ULONG* bytesActuallyRead) {
    if (!m_IsOpen || outBuffer == nullptr || bytesActuallyRead == nullptr || bytesToRead == 0) return STATUS_INVALID_PARAMETER;
    *bytesActuallyRead = 0;
    HANDLE hFile = m_StreamBuf.GetHandle();
    if (hFile == NULL || hFile == (HANDLE)-1) return STATUS_INVALID_DEVICE_STATE;

    NTSTATUS status; IO_STATUS_BLOCK ioStatus; KEVENT ioEvent;
    KeInitializeEvent(&ioEvent, NotificationEvent, FALSE);

    status = ZwReadFile(hFile, &ioEvent, NULL, NULL, &ioStatus, outBuffer, bytesToRead, NULL, NULL);

    if (status == STATUS_PENDING) {
        status = KeWaitForSingleObject(&ioEvent, Executive, KernelMode, FALSE, NULL); // Attesa infinita
        if (NT_SUCCESS(status)) status = ioStatus.Status;
    }

    if (NT_SUCCESS(status)) *bytesActuallyRead = static_cast<ULONG>(ioStatus.Information);
    return status;
}

// =========================================================================
// IMPLEMENTAZIONE DELLA GETLINE ASINCRONA PROTETTA CROSS-LAYER
// =========================================================================
size_t MalamoreIOStream::GetLine(char* buffer, size_t maxCount, char delimiter) {
    if (buffer == nullptr || maxCount == 0 || !m_IsOpen) return 0;
    size_t count = 0;

    while (count < (maxCount - 1)) {
        // Se la nostra get area locale è vuota, eseguiamo un riempimento asincrono protetto block-level
        if (m_InputLookaheadPtr >= m_InputLookaheadEnd) {
            ULONG fetchedBytes = 0;

            // Invocazione della ReadBlocking asincrona nativa (Pulisce lo stato post-timeout dell'I/O Manager)
            NTSTATUS status = this->ReadBlocking(m_InputLookaheadBuffer, STREAM_BUFFER_SIZE, &fetchedBytes);

            if (!NT_SUCCESS(status) || fetchedBytes == 0) {
                break; // EOF o interruzione fisica del canale hardware
            }
            m_InputLookaheadPtr = 0;
            m_InputLookaheadEnd = fetchedBytes;
        }

        char ch = m_InputLookaheadBuffer[m_InputLookaheadPtr++];

        if (ch == delimiter) {
            break; // Trovato il delimitatore di riga terminato
        }

        if (ch == '\r' && delimiter == '\n') continue; // Salta il carriage return standard

        buffer[count++] = ch;
    }

    buffer[count] = '\0'; // Terminazione stringa deterministica di Ring 0 / Ring 3
    return count;
}
