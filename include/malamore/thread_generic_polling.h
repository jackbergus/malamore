#pragma once
#include <ntdef.h>
#include <sal.h>


#if 0

#include "malamore/ipc_manager.h"
typedef void (*PFN_EXECUTE_ROUTINE_IO)(ULONG CoreId,
                                        MessageMap* inMap,
                                        MessageMap* outMap,
                                        PVOID corePollingContext);

// #define LOCAL_TELEMETRY_SIZE 10240 // Buffer locale capiente per singolo Task
//
// /**
//  * Record di telemetria, che rappresenta un messaggio da inviare al thread lento di scrittura su disco.
//  */
// struct LocalTelemetryRecord {
//     ULONG64 Timestamp;
//     ULONG64 ExecutionTimeUs;
//     char StatusFlag;
// };
//
// /**
//  * Coda circolare di telemetria locale al singolo thread
//  */
// struct LocalTelemetryBuffer {
//     volatile LONG ReadIndex;
//     volatile LONG WriteIndex;
//     volatile LONG Count;
//     LocalTelemetryRecord Records[LOCAL_TELEMETRY_SIZE];
// };

/**
 * Contesto locale legato al singolo elemento, TaskA o TaskB, associato alla isntanziatione dello stesso task.
 */
struct CorePollingContext {
    ULONG64 ProcessId;
    ULONG64 SynchronizedStartTicks;

    /**
     * Per il task generico è il periodo di esecuzione (es. 10 ms), per l'Hardware Reader è l'esatto intervallo di
     * campionamento (che configurerai a tempo di inizializzazione, sia esso 3.33 ms, 1 ms o 5 ms), eliminando i valori
     * cablati (hardcoded).
     */
    ULONG64 PeriodTicks;
    /**
     * Per il task generico del tandem serve a sfasare il Task B a T/2. Per l'Hardware Reader, questo campo verrà
     * semplicemente inizializzato a 0, poiché la lettura hardware deve scattare immediatamente all'inizio della
     * timeline senza alcun ritardo di fase.
     */
    ULONG64 PhaseOffsetTicks;
    /**
     * Per il task generico rappresenta il budget massimo per il Watchdog Inline. Per l'Hardware Reader, possiamo
     * riutilizzare questo identico campo a 32 bit per memorizzare il Timeout di Lettura Hardware (ReadTimeoutUs).
     * In fondo, entrambi rappresentano un "budget temporale massimo di esecuzione concesso" prima che scatti una
     * contromisura (lo sfratto per il task, o l'annullamento dell'IRP per l'hardware).
     */
    ULONG   MaxWcetUs;
    ULONG64 PerfCounterFrequency;   // <--- NUOVO: Sostituisce il double (freq.QuadPart)

    volatile LONG IsHighPriority;
    volatile LONG KeepRunning;

    KAFFINITY AffinityMask;

    PFN_EXECUTE_ROUTINE_IO TaskFunction;
    // PVOID CustomContext;

    BOOLEAN runReadOperation;
    BOOLEAN runWriteOperation;
    BOOLEAN saveFileToDisk;
};

static __inline LONG KernelAtomicRead(volatile LONG* Target) {
    return InterlockedOr(const_cast<LONG*>(Target), 0);
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

    VOID CorePollingRoutineGeneric(_In_ PVOID Context);

#ifdef __cplusplus
}
#endif
