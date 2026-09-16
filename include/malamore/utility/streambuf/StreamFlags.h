//
// Created by Giacomo Bergami on 16/09/2026.
//

#pragma once
#include "malamore/utility/kernel_compatibility.h"

#ifdef __cplusplus

// =========================================================================
// FLAG OPERATIVI IN STILE POSIX / LINUX (BITMASK COMPRIMIBILI)
// =========================================================================
class StreamFlags {
public:
    static const ULONG Read   = 0x0001; // O_RDONLY: Abilita l'accesso in lettura
    static const ULONG Write  = 0x0002; // O_WRONLY: Abilita l'accesso in scrittura classica
    static const ULONG Append = 0x0004; // O_APPEND: Forza la scrittura hardware atomica sempre in coda
    static const ULONG Creat  = 0x0008; // O_CREAT:  Crea il file se non esiste (Opzionale)
    static const ULONG Trunc  = 0x0010; // O_TRUNC:  Azzera il file se esiste (Equivalente a FILE_SUPERSEDE)
};

#endif
