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

    NTSTATUS Open(const MalamorePath& path, ULONG flags);
    VOID Close();
    BOOLEAN IsOpen() const { return m_IsOpen; }

    // Scrittura fluida Zero-STL
    MalamoreIOStream& operator<<(const char* str);
    MalamoreIOStream& operator<<(char c);
    MalamoreIOStream& operator<<(ULONG value);
    MalamoreIOStream& operator<<(MalamoreIOStream& (*manipulator)(MalamoreIOStream&));

    // Lettura asincrona temporizzata e bloccante nativa (Valida in Ring 0 e Ring 3)
    NTSTATUS ReadWithTimeout(VOID* outBuffer, ULONG bytesToRead, ULONG timeoutUs, ULONG* bytesActuallyRead);
    NTSTATUS ReadBlocking(VOID* outBuffer, ULONG bytesToRead, ULONG* bytesActuallyRead);

    // =========================================================================
    // METODO RISOLUTIVO: GetLine asincrona protetta post-timeout
    // =========================================================================
    size_t GetLine(char* buffer, size_t maxCount, char delimiter = '\n');
};

inline MalamoreIOStream& endl(MalamoreIOStream& os) { os << "\r\n"; return os; }
inline MalamoreIOStream& sflush(MalamoreIOStream& os) { os.Flush(); return os; }
#endif
