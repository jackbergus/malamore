#pragma once
#include "malamore/ipc_manager.h"

typedef void (*PFN_EXECUTE_ROUTINE_IO)(ULONG CoreId, MessageInputMap* inMap, MessageOutputMap* outMap, PVOID CustomContext);

#define LOCAL_TELEMETRY_SIZE 10240 // Buffer locale capiente per singolo Task

struct LocalTelemetryRecord {
    ULONG64 Timestamp;
    ULONG64 ExecutionTimeUs;
    char StatusFlag;
};

// Coda circolare locale al singolo thread
struct LocalTelemetryBuffer {
    volatile LONG ReadIndex;
    volatile LONG WriteIndex;
    volatile LONG Count;
    LocalTelemetryRecord Records[LOCAL_TELEMETRY_SIZE];
};


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


    // Connessione all'infrastruttura di telemetria locale e globale del pool
    LocalTelemetryBuffer LocalLog;
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
