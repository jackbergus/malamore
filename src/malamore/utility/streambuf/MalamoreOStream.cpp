//
// Created by mbda on 16/09/2026.
//

//
// Created by Giacomo Bergami on 16/09/2026.
//

#include "malamore/utility/streambuf/MalamoreOStream.h"

MalamoreOStream::MalamoreOStream() : m_IsOpen(FALSE) {
    RtlZeroMemory(&m_UnicodePath, sizeof(UNICODE_STRING));
    RtlZeroMemory(m_SavedPath, sizeof(m_SavedPath));
}

MalamoreOStream::~MalamoreOStream() {
    if (m_IsOpen) Close();
}

NTSTATUS MalamoreOStream::Open(const MalamorePath& path, StreamDisposition disposition) {
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

    ULONG nativeDisposition = 0x00000002; // Default FILE_SUPERSEDE
    if (disposition == StreamDisposition::OpenExisting) nativeDisposition = 0x00000001;
    else if (disposition == StreamDisposition::OpenOrCreate) nativeDisposition = 0x00000003;

    status = ZwCreateFile(&hFile, FILE_APPEND_DATA | SYNCHRONIZE, &objAttr, &ioStatus, NULL,
                          FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, nativeDisposition, FILE_NON_DIRECTORY_FILE, NULL, 0);

    if (NT_SUCCESS(status)) {
        m_StreamBuf.AttachHandle(hFile);
        m_IsOpen = TRUE;
    }
    return status;
}

VOID MalamoreOStream::Close() {
    if (!m_IsOpen) return;
    m_StreamBuf.FlushOutput();
    HANDLE hFile = m_StreamBuf.GetHandle();
    if (hFile != NULL && hFile != (HANDLE)-1) ZwClose(hFile);
    m_StreamBuf.AttachHandle(NULL);
    m_IsOpen = FALSE;
}

void MalamoreOStream::AppendString(const char* str) {
    if (str == nullptr) return;
    size_t len = 0;
    if (NT_SUCCESS(RtlStringCchLengthA(str, STREAM_BUFFER_SIZE, &len))) {
        m_StreamBuf.Sputn(str, len);
    }
}

MalamoreOStream& MalamoreOStream::operator<<(const char* str) {
    AppendString(str); return *this;
}

MalamoreOStream& MalamoreOStream::operator<<(char c) {
    m_StreamBuf.Sputc(c); return *this;
}

MalamoreOStream& MalamoreOStream::operator<<(const WCHAR* wstr) {
    if (wstr == nullptr) return *this;
    size_t len = 0;
    if (NT_SUCCESS(RtlStringCchLengthW(wstr, STREAM_BUFFER_SIZE, &len))) {
        for (size_t i = 0; i < len; ++i) {
            m_StreamBuf.Sputc(static_cast<char>(wstr[i]));
        }
    }
    return *this;
}

MalamoreOStream& MalamoreOStream::operator<<(ULONG value) {
    char temp[32]; RtlZeroMemory(temp, sizeof(temp));
    if (NT_SUCCESS(RtlStringCchPrintfA(temp, 32, "%lu", value))) AppendString(temp);
    return *this;
}

MalamoreOStream& MalamoreOStream::operator<<(ULONG64 value) {
    char temp[64]; RtlZeroMemory(temp, sizeof(temp));
    if (NT_SUCCESS(RtlStringCchPrintfA(temp, 64, "%I64u", value))) AppendString(temp);
    return *this;
}

MalamoreOStream& MalamoreOStream::operator<<(MalamoreOStream& (*manipulator)(MalamoreOStream&)) {
    return manipulator(*this);
}
