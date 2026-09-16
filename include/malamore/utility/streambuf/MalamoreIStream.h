//
// Created by mbda on 16/09/2026.
//

#ifndef MALAMORE_MALAMOREISTREAM_H
#define MALAMORE_MALAMOREISTREAM_H

//
// Created by Giacomo Bergami on 16/09/2026.
//

#pragma once
#include "malamore/utility/streambuf/KernelStreamBuf.h"
#include "malamore/utility/Path.h"
#include "malamore/utility/streambuf/MalamoreOStream.h" // Per agganciare StreamDisposition

#ifdef __cplusplus

class MalamoreIStream {
private:
    KernelStreamBuf m_StreamBuf;
    UNICODE_STRING  m_UnicodePath;
    WCHAR           m_SavedPath[MAX_PATH];
    BOOLEAN         m_IsOpen;

public:
    MalamoreIStream();
    ~MalamoreIStream();

    NTSTATUS Open(const MalamorePath& path, StreamDisposition disposition = StreamDisposition::OpenExisting);
    VOID Close();
    BOOLEAN IsOpen() const { return m_IsOpen; }

    // Operatori fluidi ed estrazioni di Ring 0
    MalamoreIStream& operator>>(char& c);
    MalamoreIStream& operator>>(ULONG& value);

    // Estrae una riga intera fino al terminatore (Sostituisce std::getline)
    size_t GetLine(char* buffer, size_t maxCount, char delimiter = '\n');
    int Get(); // Ritorna il singolo carattere corrente o -1 (EOF)
};

#endif


#endif //MALAMORE_MALAMOREISTREAM_H