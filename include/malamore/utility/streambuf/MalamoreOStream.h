//
// Created by mbda on 16/09/2026.
//

#ifndef MALAMORE_MALAMOREOSTREAM_H
#define MALAMORE_MALAMOREOSTREAM_H
//
// Created by Giacomo Bergami on 16/09/2026.
//

// #pragma once
#include "malamore/utility/streambuf/KernelStreamBuf.h"
#include "malamore/utility/Path.h"

#ifdef __cplusplus

enum class StreamDisposition : ULONG {
    OpenExisting = 1,  // FILE_OPEN
    CreateAlways = 2,  // FILE_SUPERSEDE
    OpenOrCreate = 3   // FILE_OPEN_IF
};

class MalamoreOStream {
private:
    KernelStreamBuf m_StreamBuf;
    UNICODE_STRING  m_UnicodePath;
    WCHAR           m_SavedPath[MAX_PATH];
    BOOLEAN         m_IsOpen;

    void AppendString(const char* str);
    void AppendInt(ULONG value);
    void AppendInt64(ULONG64 value);

public:
    MalamoreOStream();
    ~MalamoreOStream();

    NTSTATUS Open(const MalamorePath& path, StreamDisposition disposition);
    VOID Close();
    BOOLEAN IsOpen() const { return m_IsOpen; }

    // Overload degli operatori fluidi esenti da STL
    MalamoreOStream& operator<<(const char* str);
    MalamoreOStream& operator<<(const WCHAR* wstr);
    MalamoreOStream& operator<<(char c);
    MalamoreOStream& operator<<(ULONG value);
    MalamoreOStream& operator<<(ULONG64 value);

    // Supporto per i manipolatori di riga (es. endl)
    MalamoreOStream& operator<<(MalamoreOStream& (*manipulator)(MalamoreOStream&));
};

// Manipolatore di fine riga personalizzato per Ring 0 e Ring 3
inline MalamoreOStream& endl(MalamoreOStream& os) {
    os << "\r\n";
    return os;
}

#endif

#endif //MALAMORE_MALAMOREOSTREAM_H