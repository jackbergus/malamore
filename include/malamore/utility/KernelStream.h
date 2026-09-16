//
// Created by Giacomo Bergami on 16/09/2026.
//

#ifndef MALAMORE_KERNELSTREAM_H
#define MALAMORE_KERNELSTREAM_H
//
// Created by Giacomo Bergami on 16/09/2026.
//


#include "malamore/utility/kernel_compatibility.h"
#include <istream>
#include <ostream>
#include <streambuf>

// Includiamo std::filesystem ESCLUSIVAMENTE se siamo in modalità test a Ring 3
#ifndef LINK_KERNEL
#include <filesystem>
#endif


#ifdef __cplusplus

// =========================================================================
// SPECIFICA 4: STREAM BUFFER UNIFICATO E BIDIREZIONALE (ZERO EXALLOCATEPOOL)
// =========================================================================
class KernelStreamBuf : public std::streambuf {
private:
    HANDLE m_FileHandle;
    static const size_t BUFFER_SIZE = 1024;

    // Buffer di sosta fisso e contiguo allocati staticamente nello slot dello stack
    char m_InputBuffer[BUFFER_SIZE];
    char m_OutputBuffer[BUFFER_SIZE];

    bool FlushOutput();

protected:
    // Sovrascrittura dei metodi virtuali dello standard C++ per la gestione dei flussi
    virtual int_type overflow(int_type ch) override;  // Scrittura (Ostream Manager)
    virtual int_type underflow() override;             // Lettura (Istream Manager)
    virtual int sync() override;                      // Sincronizzazione (Flush/Endl)

public:
    KernelStreamBuf();
    virtual ~KernelStreamBuf();

    void AttachHandle(HANDLE hFile);
    HANDLE GetHandle() const { return m_FileHandle; }
};

// =========================================================================
// COPIA DI INPUT STREAM RAII (KernelIStream)
// =========================================================================
class KernelIStream : public std::istream {
private:
    KernelStreamBuf m_StreamBuf;
    UNICODE_STRING   m_UnicodePath;
    WCHAR            m_SavedPath[MAX_PATH];
    BOOLEAN          m_IsOpen;

    KernelIStream(const KernelIStream&) = delete;
    KernelIStream& operator=(const KernelIStream&) = delete;

public:
    KernelIStream();
    virtual ~KernelIStream();

    NTSTATUS Open(const WCHAR* kernelDevicePath, ULONG createDisposition);
    VOID Close();
    BOOLEAN IsOpen() const { return m_IsOpen; }
};

// =========================================================================
// COPIA DI OUTPUT STREAM RAII (KernelOStream)
// =========================================================================
class KernelOStream : public std::ostream {
private:
    KernelStreamBuf m_StreamBuf;
    UNICODE_STRING   m_UnicodePath;
    WCHAR            m_SavedPath[MAX_PATH];
    BOOLEAN          m_IsOpen;

    KernelOStream(const KernelOStream&) = delete;
    KernelOStream& operator=(const KernelOStream&) = delete;

public:
    KernelOStream();
    virtual ~KernelOStream();

    NTSTATUS Open(const WCHAR* kernelDevicePath, ULONG createDisposition);
    VOID Close();
    BOOLEAN IsOpen() const { return m_IsOpen; }
};

#endif

#endif //MALAMORE_KERNELSTREAM_H