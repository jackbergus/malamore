//
// Created by Giacomo Bergami on 15/09/2026.
//

#ifndef MALAMORE_THREAD_POOL_H
#define MALAMORE_THREAD_POOL_H

#include <malamore/threads/thread_shared.h>
#include "malamore/structures/InterProcessChannelData.h"

// =========================================================================
// CORE POOL REGISTRY CENTRALIZZATA (ARRAY UNICO OMOGENEO STATICO)
// =========================================================================
#pragma pack(push, 4)
struct CorePoolRegistry {
    struct MalamoreThreadSlot ThreadPool[MAX_TOTAL_SLOTS];
    InterProcessChannelData IpcChannels[MAX_GENERIC_TASKS];

    ULONG CurrentHwThreadsCount;
    ULONG CurrentGenericTasksCount;
    ULONG TotalAllocatedSlots;

    ULONG64 GlobalSynchronizedStartTicks;
    ULONG64 GlobalPerfCounterFrequency;
    volatile LONG IsRegistryActive;

    // Telemetry Management
    HANDLE                  hFileHandle;
    HANDLE                  hDiskThread;
    volatile LONG           TerminateDiskThread;
    InterProcessChannelData TelemetryChannel;
    MalamoreThreadSlot      TelemetryThread;
};
#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif

#ifdef INCLUDE_UNTESTED_FEATURES
    NTSTATUS CorePool_InitializeRegistry();
    NTSTATUS CorePool_RegisterHwReader(ULONG64 processId, const WCHAR* devicePath, ULONG readTimeoutUs, ULONG samplingPeriodUs, KAFFINITY coreAffinityMask, ULONG targetTaskId);
    NTSTATUS CorePool_RegisterGenericTask(ULONG64 processId, const char* taskName, ULONG executionPeriodUs, ULONG estimatedWcetUs, KAFFINITY coreAffinityMask, ULONG targetNodeId, BOOLEAN runRead, BOOLEAN runWrite, BOOLEAN saveDisk, PFN_EXECUTE_ROUTINE_IO executionFunction);
    NTSTATUS CorePool_StartPoolExecution();
    VOID CorePool_ShutdownAndClear();
#endif

    NTSTATUS CorePool_RouteMessageToDest(ULONG targetNodeId, const struct DataChannelParcel* packet);
    NTSTATUS CorePool_RouteMessageSrcToDest(LONG Timestamp, ULONG SourceNodeId, ULONG targetNodeId,  const UCHAR* PayloadData ,ULONG DataLength);

#ifdef __cplusplus
}
#endif



#endif //MALAMORE_THREAD_POOL_H