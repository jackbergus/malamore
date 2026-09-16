//
// Created by Giacomo Bergami on 15/09/2026.
//

#ifndef MALAMORE_HARDWARE_READER_H
#define MALAMORE_HARDWARE_READER_H

#include <windows.h>
#include <malamore/structures/MessageMap.h>

#pragma once
// #include "malamore/ipc_manager.h"

#define HARDWARE_PAYLOAD_SIZE       (512)
#define HARDWARE_READER_ID          (ULONG_MAX-1)

#pragma pack(push, 1)
// Contesto operativo del Thread di Lettura Hardware a PASSIVE_LEVEL
struct HWTaskContext {
    ULONG64 ProcessId;
    ULONG64 SynchronizedStartTicks;
    ULONG64 PeriodTicks;

    ULONG64 PerfCounterFrequency;

    volatile LONG IsHighPriority;
    volatile LONG KeepRunning;

    KAFFINITY AffinityMask;

    // ULONG TargetCoreId;

    // Configurazione I/O Dinamica
    WCHAR DevicePath[MAX_PATH];    // Percorso dinamico passato come argomento (Driver o File)
    ULONG ReadTimeoutUs;           // Timeout di lettura espresso in microsecondi
    ULONG TargetThreadId;
};
#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif

    VOID HardwareReaderThreadRoutine(struct UnifiedThreadContext* Context, struct FutureResultParcel* OutResult);

#ifdef __cplusplus
}
#endif


#endif //MALAMORE_HARDWARE_READER_H