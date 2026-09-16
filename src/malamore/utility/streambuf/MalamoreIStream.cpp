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

NTSTATUS MalamoreIStream::Open(const MalamorePath& path, StreamDisposition disposition) {
    if (m_IsOpen) return STATUS_SUCCESS;
    if (path.empty()) return STATUS_INVALID_PARAMETER;

    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    OBJECT_ATTRIBUTES objAttr;
    HANDLE hFile = NULL;

    status = RtlStringCchCopyW(m_SavedPath, MAX_PATH, path.ToKernelPath(m_SavedPath, MAX_PATH));
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&m_UnicodePath, m_SavedPath);
    InitializeObjectAttributes(&objAttr, &m_UnicodePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    ULONG nativeDisposition = 0x00000001; // Default FILE_OPEN
    if (disposition == StreamDisposition::CreateAlways) nativeDisposition = 0x00000002;
    else if (disposition == StreamDisposition::OpenOrCreate) nativeDisposition = 0x00000003;

    status = ZwCreateFile(&hFile, GENERIC_READ | SYNCHRONIZE, &objAttr, &ioStatus, NULL,
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
