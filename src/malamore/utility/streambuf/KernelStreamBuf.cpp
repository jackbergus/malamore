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
    // Se il buffer di lettura è esaurito, invoca ZwReadFile per riempirlo (underflow custom)
    if (m_InPtr >= m_InEnd) {
        if (m_FileHandle == NULL || m_FileHandle == (HANDLE)-1) return -1;

        IO_STATUS_BLOCK ioStatus;
        RtlZeroMemory(m_InputBuffer, STREAM_BUFFER_SIZE);

        NTSTATUS status = ZwReadFile(m_FileHandle, NULL, NULL, NULL, &ioStatus, m_InputBuffer, static_cast<ULONG>(STREAM_BUFFER_SIZE), NULL, NULL);

        if (NT_SUCCESS(status) && ioStatus.Information > 0) {
            m_InBase = 0;
            m_InPtr = 0;
            m_InEnd = ioStatus.Information;
        } else {
            return -1; // End of File o Errore Hardware
        }
    }
    return static_cast<unsigned char>(m_InputBuffer[m_InPtr]);
}

int KernelStreamBuf::Sbumpc() {
    int ch = Sgetc();
    if (ch != -1) {
        m_InPtr++;
    }
    return ch;
}

size_t KernelStreamBuf::Sgetn(char* s, size_t count) {
    if (s == nullptr || count == 0) return 0;
    size_t bytesRead = 0;

    for (size_t i = 0; i < count; ++i) {
        int ch = Sbumpc();
        if (ch == -1) break;
        s[i] = static_cast<char>(ch);
        bytesRead++;
    }
    return bytesRead;
}
