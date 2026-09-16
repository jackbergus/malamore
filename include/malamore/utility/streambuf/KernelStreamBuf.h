//
// Created by mbda on 16/09/2026.
//

#ifndef MALAMORE_KERNELSTREAMBUF_H
#define MALAMORE_KERNELSTREAMBUF_H
//
// Created by Giacomo Bergami on 16/09/2026.
//

#include "malamore/utility/kernel_compatibility.h"

#ifdef __cplusplus

#define STREAM_BUFFER_SIZE 1024

class KernelStreamBuf {
private:
    HANDLE m_FileHandle;

    // SPECIFICA 4: Array statici contigui dedicati per inibire ExAllocatePool2 a runtime
    char m_InputBuffer[STREAM_BUFFER_SIZE];
    char m_OutputBuffer[STREAM_BUFFER_SIZE];

    size_t m_InBase;    // Puntatore all'inizio dei dati letti (gbase)
    size_t m_InPtr;     // Posizione corrente di lettura (gptr)
    size_t m_InEnd;     // Fine dei dati validi nel buffer di lettura (egptr)

    size_t m_OutPtr;    // Posizione corrente di scrittura (pptr)

public:
    KernelStreamBuf();
    ~KernelStreamBuf();

    void AttachHandle(HANDLE hFile);
    HANDLE GetHandle() const { return m_FileHandle; }

    // Primitive di scrittura del buffer
    bool Sputc(char c);
    bool Sputn(const char* s, size_t count);
    bool FlushOutput();

    // Primitive di lettura del buffer
    int Sgetc(); // Ritorna il carattere corrente o -1 (EOF) senza avanzare
    int Sbumpc(); // Ritorna il carattere corrente e avanza di 1 posizione
    size_t Sgetn(char* s, size_t count);
};

#endif

#endif //MALAMORE_KERNELSTREAMBUF_H