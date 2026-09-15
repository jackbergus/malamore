//
// Created by Giacomo Bergami on 15/09/2026.
//

#ifndef MALAMORE_THREAD_SHARED_H
#define MALAMORE_THREAD_SHARED_H

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
    THREAD_TYPE_GENERIC_TASK
};


// =========================================================================
// CORREZIONE DELLA FIRMA: Il Task riceve il contesto UNIFICATO del thread
// =========================================================================
typedef void (*PFN_EXECUTE_ROUTINE_IO)(
    ULONG CoreId,
    struct MessageMap* InMap,
    struct MessageMap* OutMap,
    PVOID ThreadContext // <--- CORRETTO E TIPIZZATO!
);

#pragma pack(push, 4)
// La struttura del contesto unificato in microsecondi
struct UnifiedThreadContext {
    ULONG64 ProcessId;
    ULONG64 SynchronizedStartTicks;
    ULONG64 PhaseOffsetTicks;

    ULONG   PeriodUs;
    ULONG   MaxBudgetUs;
    ULONG64 PerfCounterFrequency;

    volatile LONG IsHighPriority;
    volatile LONG KeepRunning;
    KAFFINITY AffinityMask;

    PFN_EXECUTE_ROUTINE_IO TaskFunctionPointer; // Puntatore allineato alla nuova firma

    WCHAR DevicePath[MAX_PATH];
    ULONG TargetThreadId;

    // Flag operativi nativi per il controllo del flusso del core
    BOOLEAN runReadOperation;
    BOOLEAN runWriteOperation;
    BOOLEAN saveFileToDisk;

    // Area Scratchpad / Lavagna interna inglobata DIRETTAMENTE nel contesto unico
    UCHAR ScratchBuffer[2048];
};

// Lo Slot Omogeneo dell'array unico del pool statico
struct MalamoreThreadSlot {
    ULONG SlotId;
    enum MalamoreThreadType Type;
    KAFFINITY BaseAffinityMask;

    HANDLE hThreadHandleA;
    HANDLE hThreadHandleB;

    struct UnifiedThreadContext ContextA; // Gestisce HW Reader o Task A del tandem
    struct UnifiedThreadContext ContextB; // Gestisce esclusivamente il Task B del tandem
};
#pragma pack(pop)

#endif //MALAMORE_THREAD_SHARED_H