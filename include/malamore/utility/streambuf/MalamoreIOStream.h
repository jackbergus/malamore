//
// Created by Giacomo Bergami on 16/09/2026.
//

#pragma once
#include "malamore/utility/streambuf/KernelStreamBuf.h"
#include "malamore/utility/Path.h"
#include "malamore/utility/streambuf/StreamFlags.h"

//
// Created by Giacomo Bergami on 16/09/2026.
//


#ifdef __cplusplus
class MalamoreIOStream {
private:
    KernelStreamBuf m_StreamBuf;
    UNICODE_STRING  m_UnicodePath;
    WCHAR           m_SavedPath[MAX_PATH];
    BOOLEAN         m_IsOpen;

    char   m_OutputBuffer[STREAM_BUFFER_SIZE];
    size_t m_CurrentBufferIndex;

    // Buffer statico di lettura locale per sanare i residui del timeout (Specifica 4)
    char   m_InputLookaheadBuffer[STREAM_BUFFER_SIZE];
    size_t m_InputLookaheadPtr;
    size_t m_InputLookaheadEnd;


    void AppendString(const char* str);

public:
    bool Flush();
    MalamoreIOStream();
    ~MalamoreIOStream();

    HANDLE GetHandle() {
        return m_StreamBuf.GetHandle();
    }

    NTSTATUS Open(const MalamorePath& path, ULONG flags);
    VOID Close();
    BOOLEAN IsOpen() const { return m_IsOpen; }
    // Overload degli operatori fluidi esenti da STL
    bool Sputn(const char* s, size_t count) {
        return m_StreamBuf.Sputn(s, count);
    }

    // Scrittura fluida Zero-STL
    MalamoreIOStream& operator<<(const char* str);
    MalamoreIOStream& operator<<(char c);
    MalamoreIOStream& operator<<(ULONG value);
    MalamoreIOStream& operator<<(MalamoreIOStream& (*manipulator)(MalamoreIOStream&));
    MalamoreIOStream& operator<<(ULONG64 value) {
        char temp[64]; RtlZeroMemory(temp, sizeof(temp));
        if (NT_SUCCESS(RtlStringCchPrintfA(temp, 64, "%I64u", value))) AppendString(temp);
        return *this;
    }
    MalamoreIOStream& operator<<(const WCHAR* wstr) {
        if (wstr == nullptr) return *this;
        size_t len = 0;
        if (NT_SUCCESS(RtlStringCchLengthW(wstr, STREAM_BUFFER_SIZE, &len))) {
            for (size_t i = 0; i < len; ++i) {
                m_StreamBuf.Sputc(static_cast<char>(wstr[i]));
            }
        }
        return *this;
    }
    MalamoreIOStream& operator>>(char& c) {
        int ch = m_StreamBuf.Sbumpc();
        c = (ch != -1) ? static_cast<char>(ch) : '\0';
        return *this;
    }

    MalamoreIOStream& operator>>(ULONG& value) {
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

    // Lettura asincrona temporizzata e bloccante nativa (Valida in Ring 0 e Ring 3)
    NTSTATUS ReadWithTimeout(VOID* outBuffer, ULONG bytesToRead, ULONG timeoutUs, ULONG* bytesActuallyRead);
    NTSTATUS ReadBlocking(VOID* outBuffer, ULONG bytesToRead, ULONG* bytesActuallyRead);

    // =========================================================================
    // METODO RISOLUTIVO: GetLine asincrona protetta post-timeout
    // =========================================================================
    size_t GetLine(char* buffer, size_t maxCount, char delimiter = '\n');
    int Get() {
        return m_StreamBuf.Sbumpc();
    }
};

inline MalamoreIOStream& endl(MalamoreIOStream& os) { os << "\r\n"; return os; }
inline MalamoreIOStream& sflush(MalamoreIOStream& os) { os.Flush(); return os; }
#endif
