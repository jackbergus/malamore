#pragma once
#include "client.h"
#include "malamore/ipc_manager.h"
#include "malamore/thread_generic_polling.h"

#define MAX_POOL_CORES 32
#define TELEMETRY_QUEUE_SIZE 128

#pragma pack(push, 1)
struct TelemetryRecord {
    ULONG64 Timestamp;
    ULONG CoreId;
    ULONG64 ExecutionTimeUs;
    char StatusFlag;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct TelemetryQueue {
    volatile LONG ReadIndex;
    volatile LONG WriteIndex;
    volatile LONG Count;
    TelemetryRecord Records[TELEMETRY_QUEUE_SIZE];
};
#pragma pack(pop)

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

    // Infrastruttura del Disk File Sink asincrono
    HANDLE hDiskThread;
    volatile LONG TerminateDiskThread;
    TelemetryQueue* LogQueue;
    HANDLE hFileHandle;
};

#ifdef __cplusplus
extern "C" {
#endif

    NTSTATUS InitializeCorePool(ULONG PercentageToUse);
    NTSTATUS AllocateTaskToCore(const ServiceParams* params, const char* taskName, ULONG sourceNodeId, PVOID customContext);
    NTSTATUS StartAllConfiguredTandems(ULONG64 CurrentTicks, ULONG64 PerfFrequency);
    VOID StopAndFreeCorePool();

#ifdef __cplusplus
}
#endif
