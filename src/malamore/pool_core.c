//
// Created by Giacomo Bergami on 14/09/2026.
//

#include "malamore/pool_core.h"

// Definizione delle variabili globali del modulo
PCORE_ENTRY g_PoolCore = NULL;
ULONG g_TotaleCoreDisponibili = 0;
ERESOURCE g_PoolCoreLock;

VOID InizializzaPoolCore(VOID) {
    ExInitializeResourceLite(&g_PoolCoreLock);

    // Rileva quanti core logici sono attivi nel sistema
    g_TotaleCoreDisponibili = KeQueryActiveProcessorCountEx(
        ALL_PROCESSOR_GROUPS
    );

    g_PoolCore = (PCORE_ENTRY)ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(CORE_ENTRY) * g_TotaleCoreDisponibili,
        'CorP'
    );

    if (g_PoolCore == NULL) return;

    for (ULONG i = 0; i < g_TotaleCoreDisponibili; i++) {
        g_PoolCore[i].CoreIndex = i;
        // Proteggiamo il Core 0 lasciandolo al sistema operativo;
        // Gli altri core (1, 2, 3...) partono come liberi
        g_PoolCore[i].InUso = (i == 0) ? TRUE : FALSE;
    }
}

VOID DeinizializzaPoolCore(VOID) {
    ExDeleteResourceLite(&g_PoolCoreLock);
    if (g_PoolCore != NULL) {
        ExFreePoolWithTag(g_PoolCore, 'CorP');
        g_PoolCore = NULL;
    }
}

BOOLEAN AllocaCoppiaCore(PULONG OutCoreA, PULONG OutCoreB) {
    BOOLEAN allocati = FALSE;
    ULONG trovati = 0;
    ULONG indici[2] = { 0 };

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&g_PoolCoreLock, TRUE);

    for (ULONG i = 0; i < g_TotaleCoreDisponibili; i++) {
        if (!g_PoolCore[i].InUso) {
            indici[trovati] = g_PoolCore[i].CoreIndex;
            trovati++;
            if (trovati == 2) break;
        }
    }

    if (trovati == 2) {
        g_PoolCore[indici[0]].InUso = TRUE;
        g_PoolCore[indici[1]].InUso = TRUE;
        *OutCoreA = indici[0];
        *OutCoreB = indici[1];
        allocati = TRUE;
    }

    ExReleaseResourceLite(&g_PoolCoreLock);
    KeLeaveCriticalRegion();
    return allocati;
}

VOID LiberaCoppiaCore(ULONG CoreA, ULONG CoreB) {
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&g_PoolCoreLock, TRUE);

    if (CoreA < g_TotaleCoreDisponibili) {
        g_PoolCore[CoreA].InUso = FALSE;
    }
    if (CoreB < g_TotaleCoreDisponibili) {
        g_PoolCore[CoreB].InUso = FALSE;
    }

    ExReleaseResourceLite(&g_PoolCoreLock);
    KeLeaveCriticalRegion();
}
