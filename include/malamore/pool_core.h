//
// Created by Giacomo Bergami on 14/09/2026.
//

#ifndef CORE_POOL_H
#define CORE_POOL_H

#include <ntddk.h>

// Struttura per tracciare lo stato di allocazione di ciascun core
typedef struct _CORE_ENTRY {
    ULONG CoreIndex;
    BOOLEAN InUso;
} CORE_ENTRY, *PCORE_ENTRY;

#ifdef __cplusplus
extern "C" {
#endif

    // Variabili globali gestite dal modulo dei core
    extern PCORE_ENTRY g_PoolCore;
    extern ULONG g_TotaleCoreDisponibili;
    extern ERESOURCE g_PoolCoreLock;

    // Funzioni pubbliche del modulo
    VOID InizializzaPoolCore(VOID);
    VOID DeinizializzaPoolCore(VOID);
    BOOLEAN AllocaCoppiaCore(PULONG OutCoreA, PULONG OutCoreB);
    VOID LiberaCoppiaCore(ULONG CoreA, ULONG CoreB);

#ifdef __cplusplus
}
#endif

#endif // CORE_POOL_H
