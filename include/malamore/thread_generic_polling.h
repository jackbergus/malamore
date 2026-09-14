#pragma once
#include "malamore/ipc_manager.h"

typedef void (*PFN_EXECUTE_ROUTINE_IO)(ULONG CoreId, MessageInputMap* inMap, MessageOutputMap* outMap, PVOID CustomContext);

struct CorePollingContext {
    ULONG64 SynchronizedStartTicks;
    ULONG64 PhaseOffsetTicks;
    ULONG64 PeriodTicks;
    ULONG MaxWcetUs;
    ULONG64 PerfCounterFrequency;   // <--- NUOVO: Sostituisce il double (freq.QuadPart)

    volatile LONG IsHighPriority;
    volatile LONG KeepRunning;

    InterProcessChannelData* pInChannelData;
    InterProcessChannelData* pOutChannelData;

    PFN_EXECUTE_ROUTINE_IO TaskFunction;
    PVOID CustomContext;
    KAFFINITY AffinityMask;
};

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
