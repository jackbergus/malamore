#pragma once
#include "malamore/ipc_manager.h"
#include "malamore/thread_generic_polling.h"

#define MAX_POOL_CORES 32

// Struttura dati dello slot hardware associato a un singolo core logico
struct CorePoolEntry {
    ULONG CoreId;
    KAFFINITY AffinityMask;
    BOOLEAN IsAllocated;

    // Il Tandem cooperativo assegnato a questo specifico Core hardware
   struct  CorePollingContext* TaskA;
    struct CorePollingContext* TaskB;

    HANDLE hThreadA;
    HANDLE hThreadB;

    // Condotti IPC dedicati residenti nel NonPagedPoolNx
    struct InterProcessChannelData* InChannel;
    struct InterProcessChannelData* OutChannel;
};

// L'orchestratore centrale del pool
struct CorePoolRegistry {
    struct CorePoolEntry Cores[MAX_POOL_CORES];
    ULONG TotalConfiguredCores;
    ULONG ActiveAllocatedCores;
    ULONG64 SynchronizedStartTicks;
    volatile LONG IsActive;
};

#ifdef __cplusplus
extern "C" {
#endif

    NTSTATUS InitializeCorePool(ULONG PercentageToUse);
    NTSTATUS AllocateTaskToCore(const struct ServiceParams* params, ULONG sourceNodeId, PFN_EXECUTE_ROUTINE_IO taskFunc, PVOID customContext);
    NTSTATUS StartAllConfiguredTandems(ULONG64 CurrentTicks, ULONG64 PerfFrequency);
    VOID StopAndFreeCorePool();

#ifdef __cplusplus
}
#endif
