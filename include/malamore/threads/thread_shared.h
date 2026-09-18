//
// Created by Giacomo Bergami on 15/09/2026.
//

#ifndef MALAMORE_THREAD_SHARED_H
#define MALAMORE_THREAD_SHARED_H
#include "malamore/utility/streambuf/MalamoreIStream.h"


#ifdef LINK_KERNEL
#include <ntddk.h>
#else
#include <windows.h>
#endif

#include <malamore/structures/MessageMap.h>

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef MAX_TOTAL_SLOTS
#define MAX_TOTAL_SLOTS        16
#endif

#ifndef MAX_GENERIC_TASKS
#define MAX_GENERIC_TASKS       8
#endif


enum MalamoreThreadType {
    THREAD_TYPE_FREE = 0,
    THREAD_TYPE_HW_READER,
    THREAD_TYPE_GENERIC_TASK,
    THREAD_TYPE_TELEMETRY_WRITER,
};

#define HARDWARE_PAYLOAD_SIZE       (512)
#define HARDWARE_READER_ID          (ULONG_MAX-1)
#define TELEMETRY_WRITER_ID         (ULONG_MAX-2)

// =========================================================================
// CORREZIONE DELLA FIRMA: Il Task riceve il contesto UNIFICATO del thread
// =========================================================================
typedef void (*PFN_EXECUTE_ROUTINE_IO)(
    ULONG CoreId,
    struct MessageMap* InMap,
    struct MessageMap* OutMap,
    PVOID ThreadContext // <--- CORRETTO E TIPIZZATO!
);

#define FUTURE_BUFFER_SIZE 512

#pragma pack(push, 4)
struct FutureResultParcel {
    UCHAR DataBuffer[FUTURE_BUFFER_SIZE]; // <--- REQUISITO: Buffer statico nativo di sosta
    ULONG DataLength;                     // Dimensione reale espressa in byte dei dati validi
};
#pragma pack(pop)

struct UnifiedThreadContext;

// Firma aggiornata della routine computazionale Real-Time del tandem o dell'I/O
typedef void (*PFN_FUTURE_THREAD_ROUTINE)(struct UnifiedThreadContext* Context, struct FutureResultParcel* OutResult);

#pragma pack(push, 4)
// La struttura del contesto unificato in microsecondi
struct UnifiedThreadContext {
    ULONG64 ProcessId;
    ULONG64 SynchronizedStartTicks;
    ULONG64 PhaseOffsetTicks;

    ULONG   PeriodUs;
    ULONG   MaxBudgetUs;                // This works as a ReadTimeout for the Hardware Reader
    ULONG64 PerfCounterFrequency;

    volatile LONG IsHighPriority;
    volatile LONG KeepRunning;
    KAFFINITY AffinityMask;

    PFN_FUTURE_THREAD_ROUTINE TaskFunctionPointer; // Puntatore allineato alla nuova firma

    MalamoreIStream* DevicePath{nullptr};
    // WCHAR DevicePath[MAX_PATH];
    ULONG TargetThreadId;

    // Flag operativi nativi per il controllo del flusso del core
    BOOLEAN runReadOperation;
    BOOLEAN runWriteOperation;
    BOOLEAN saveFileToDisk;
    // Area Scratchpad / Lavagna interna inglobata DIRETTAMENTE nel contesto unico
    // UCHAR ScratchBuffer[2048];
};


#pragma pack(pop)


#define INIT_SYNCH_FROM_CONTEXT(nextPeriodTime, Context, periodTicks)                                                                                                                       \
        if ((nextPeriodTime) && (Context))                                                                                                                                                  \
            ((LARGE_INTEGER*)nextPeriodTime)->QuadPart = ((struct UnifiedThreadContext*)Context)->SynchronizedStartTicks;                                                                   \
        const ULONG64 periodTicks = (static_cast<ULONG64>(((struct UnifiedThreadContext*)Context)->PeriodUs) * ((struct UnifiedThreadContext*)Context)->PerfCounterFrequency) / 1000000ULL;


#define THREAD_PERIOD_LOOP_UNTIL_ALIVE(nextPeriodTime, currentTime, Context, Timestamp)                                                                                                            \
while ((nextPeriodTime) && (currentTime) && (Context) && KernelAtomicRead(&((struct UnifiedThreadContext*)Context)->KeepRunning) == 1) {                                                \
    do {                                                                                                                                                                                \
        YieldProcessor();                                                                                                                                                               \
        *((LARGE_INTEGER*)currentTime) = KeQueryPerformanceCounter(NULL);                                                                                                               \
    } while ((((LARGE_INTEGER*)currentTime)->QuadPart < ((LARGE_INTEGER*)nextPeriodTime)->QuadPart) && (KernelAtomicRead(&((struct UnifiedThreadContext*)Context)->KeepRunning) == 1)); \
    if (KernelAtomicRead(&((struct UnifiedThreadContext*)Context)->KeepRunning) == 0) {                                                                                                 \
        break;                                                                                                                                                                          \
    }                                                                                                                                                                                   \
    const ULONG64 Timestamp = (((LARGE_INTEGER*)currentTime)->QuadPart * 1000000ULL) / ((struct UnifiedThreadContext*)Context)->PerfCounterFrequency;


#define THREAD_NEXT_PERIOD_AFTER_TICKS(nextPeriodTime, periodTicks) if (nextPeriodTime) ((LARGE_INTEGER*)nextPeriodTime)->QuadPart += ((ULONG64)periodTicks); }


#endif //MALAMORE_THREAD_SHARED_H