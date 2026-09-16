//
// Created by Giacomo Bergami on 16/09/2026.
//

//
// Created by Giacomo Bergami on 16/09/2026.
//

#include "malamore/utility/KernelStream.h"
// #include <ntstrsafe.h>

// =========================================================================
// INTERFACCIA STREAM BUFFER (KernelStreamBuf)
// =========================================================================

KernelStreamBuf::KernelStreamBuf() : m_FileHandle(NULL) {
    // Configurazione iniziale dell'area di scrittura (Put Area)
    setp(m_OutputBuffer, m_OutputBuffer + BUFFER_SIZE);

    // Forza il primo underflow della get area impostando i puntatori a zero
    setg(m_InputBuffer, m_InputBuffer, m_InputBuffer);
}

KernelStreamBuf::~KernelStreamBuf() {
    FlushOutput();
}

void KernelStreamBuf::AttachHandle(HANDLE hFile) {
    m_FileHandle = hFile;
    setp(m_OutputBuffer, m_OutputBuffer + BUFFER_SIZE);
    setg(m_InputBuffer, m_InputBuffer, m_InputBuffer);
}

bool KernelStreamBuf::FlushOutput() {
    ptrdiff_t numBytes = pptr() - pbase();
    if (numBytes <= 0) return true;

    if (m_FileHandle == NULL || m_FileHandle == (HANDLE)-1) return false;

    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status = ZwWriteFile(m_FileHandle, NULL, NULL, NULL, &ioStatus, m_OutputBuffer, static_cast<ULONG>(numBytes), NULL, NULL);

    if (NT_SUCCESS(status)) {
        setp(m_OutputBuffer, m_OutputBuffer + BUFFER_SIZE);
        return true;
    }
    return false;
}

std::streambuf::int_type KernelStreamBuf::overflow(int_type ch) {
    if (ch != traits_type::eof()) {
        *pptr() = traits_type::to_char_type(ch);
        pbump(1);
    }
    return FlushOutput() ? traits_type::not_eof(ch) : traits_type::eof();
}

std::streambuf::int_type KernelStreamBuf::underflow() {
    if (gptr() < egptr()) {
        return traits_type::to_int_type(*gptr());
    }

    if (m_FileHandle == NULL || m_FileHandle == (HANDLE)-1) return traits_type::eof();

    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status = ZwReadFile(m_FileHandle, NULL, NULL, NULL, &ioStatus, m_InputBuffer, static_cast<ULONG>(BUFFER_SIZE), NULL, NULL);

    if (NT_SUCCESS(status) && ioStatus.Information > 0) {
        setg(m_InputBuffer, m_InputBuffer, m_InputBuffer + ioStatus.Information);
        return traits_type::to_int_type(*gptr());
    }
    return traits_type::eof();
}

int KernelStreamBuf::sync() {
    return FlushOutput() ? 0 : -1;
}

// =========================================================================
// INTERFACCIA INPUT STREAM (KernelIStream)
// =========================================================================

KernelIStream::KernelIStream() : std::istream(&m_StreamBuf), m_IsOpen(FALSE) {
    RtlZeroMemory(&m_UnicodePath, sizeof(UNICODE_STRING));
    RtlZeroMemory(m_SavedPath, sizeof(m_SavedPath));
}

KernelIStream::~KernelIStream() {
    if (m_IsOpen) Close();
}

NTSTATUS KernelIStream::Open(const WCHAR* kernelDevicePath, ULONG createDisposition) {
    if (m_IsOpen) return STATUS_SUCCESS;
    if (kernelDevicePath == nullptr) return STATUS_INVALID_PARAMETER;

    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    OBJECT_ATTRIBUTES objAttr;
    HANDLE hFile = NULL;

    status = RtlStringCchCopyW(m_SavedPath, MAX_PATH, kernelDevicePath);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&m_UnicodePath, m_SavedPath);
    InitializeObjectAttributes(&objAttr, &m_UnicodePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(&hFile, GENERIC_READ | SYNCHRONIZE, &objAttr, &ioStatus, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, createDisposition, FILE_NON_DIRECTORY_FILE, NULL, 0);

    if (NT_SUCCESS(status)) {
        m_StreamBuf.AttachHandle(hFile);
        m_IsOpen = TRUE;
    }
    return status;
}

VOID KernelIStream::Close() {
    if (!m_IsOpen) return;
    HANDLE hFile = m_StreamBuf.GetHandle();
    if (hFile != NULL && hFile != (HANDLE)-1) ZwClose(hFile);
    m_StreamBuf.AttachHandle(NULL);
    m_IsOpen = FALSE;
}

// =========================================================================
// INTERFACCIA OUTPUT STREAM (KernelOStream)
// =========================================================================

KernelOStream::KernelOStream() : std::ostream(&m_StreamBuf), m_IsOpen(FALSE) {
    RtlZeroMemory(&m_UnicodePath, sizeof(UNICODE_STRING));
    RtlZeroMemory(m_SavedPath, sizeof(m_SavedPath));
}

KernelOStream::~KernelOStream() {
    if (m_IsOpen) Close();
}

NTSTATUS KernelOStream::Open(const WCHAR* kernelDevicePath, ULONG createDisposition) {
    if (m_IsOpen) return STATUS_SUCCESS;
    if (kernelDevicePath == nullptr) return STATUS_INVALID_PARAMETER;

    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    OBJECT_ATTRIBUTES objAttr;
    HANDLE hFile = NULL;

    status = RtlStringCchCopyW(m_SavedPath, MAX_PATH, kernelDevicePath);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&m_UnicodePath, m_SavedPath);
    InitializeObjectAttributes(&objAttr, &m_UnicodePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(&hFile, FILE_APPEND_DATA | SYNCHRONIZE, &objAttr, &ioStatus, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, createDisposition, FILE_NON_DIRECTORY_FILE, NULL, 0);

    if (NT_SUCCESS(status)) {
        m_StreamBuf.AttachHandle(hFile);
        m_IsOpen = TRUE;
    }
    return status;
}

VOID KernelOStream::Close() {
    if (!m_IsOpen) return;
    m_StreamBuf.pubsync(); // Svuota l'output residuo
    HANDLE hFile = m_StreamBuf.GetHandle();
    if (hFile != NULL && hFile != (HANDLE)-1) ZwClose(hFile);
    m_StreamBuf.AttachHandle(NULL);
    m_IsOpen = FALSE;
}
