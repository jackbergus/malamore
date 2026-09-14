#pragma once
#include "malamore/ipc_manager.h"

typedef void (*PFN_EXECUTE_ROUTINE_IO)(ULONG CoreId, MessageInputMap* inMap, MessageOutputMap* outMap, PVOID CustomContext);

struct CorePollingContext {
    ULONG64 PhaseOffsetTicks;
    ULONG64 PeriodTicks;
    ULONG MaxWcetUs;
    double TicksPerUs;

    // Variabili atomiche volatili
    volatile LONG IsHighPriority; // 1 = HIGH_LEVEL, 0 = DISPATCH_LEVEL / LOWER
    volatile LONG KeepRunning;    // Condizione del ciclo del System Thread

    // Riferimenti ai Ring Buffer nel NonPagedPoolNx
    InterProcessChannelData* pInChannelData;
    InterProcessChannelData* pOutChannelData;

    PFN_EXECUTE_ROUTINE_IO TaskFunction;
    PVOID CustomContext;
    KAFFINITY AffinityMask;
};

// Lettura atomica hardware a Ring 0 tramite bitwise OR neutro
static __inline LONG KernelAtomicRead(volatile LONG* Target) {
    return InterlockedOr(const_cast<LONG*>(Target), 0);
}

#ifdef __cplusplus
extern "C" {
#endif

    VOID CorePollingRoutineGeneric(_In_ PVOID Context);

#ifdef __cplusplus
}
#endif
