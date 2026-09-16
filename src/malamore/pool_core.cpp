#include <malamore.h>
#include "malamore/pool_core.h"

#include "malamore/driver_utility.h"
#include "malamore/service_manager.h"

/**
 * This ensures that the g_PoolRegistry will be still there across all the points of execution of the service
 */
static CorePoolRegistry g_PoolRegistry;

VOID ScriviRecordSuFileSink(ULONG CoreId, ULONG64 ElapsedUs, char Status) {
     if (/*!g_PoolRegistry.LogQueue ||*/ g_PoolRegistry.IsActive == 0) return;

     // TelemetryQueue* queue = g_PoolRegistry.LogQueue;
     if (g_PoolRegistry.LogQueue.Count >= TELEMETRY_QUEUE_SIZE) return; // Buffer pieno, scarta per non generare jitter

     LONG currentWrite = g_PoolRegistry.LogQueue.WriteIndex;
     TelemetryRecord* record = &g_PoolRegistry.LogQueue.Records[currentWrite];

     LARGE_INTEGER ticks;
     ticks = KeQueryPerformanceCounter(NULL);
     record->Timestamp = ticks.QuadPart;
     record->CoreId = CoreId;
     record->ExecutionTimeUs = ElapsedUs;
     record->StatusFlag = Status;

     InterlockedExchange(&g_PoolRegistry.LogQueue.WriteIndex, (currentWrite + 1) % TELEMETRY_QUEUE_SIZE);
     InterlockedIncrement(&g_PoolRegistry.LogQueue.Count);
 }

VOID DiskWriterThreadRoutine(_In_ PVOID Context) {
    UNREFERENCED_PARAMETER(Context);
    // TelemetryQueue* queue = g_PoolRegistry.LogQueue;
    IO_STATUS_BLOCK ioStatus;

    // Il thread gira fiso a PASSIVE_LEVEL: può effettuare I/O sincrono su file senza bloccare i core
    while (!InterlockedOr(&g_PoolRegistry.TerminateDiskThread, 0) || g_PoolRegistry.LogQueue.Count > 0) {

        if (g_PoolRegistry.LogQueue.Count == 0) {
            // Se la coda è vuota, rilascia temporaneamente la CPU con un piccolo stallo preventivo
            LARGE_INTEGER interval;
            interval.QuadPart = -100 * 1000; // 10 millisecondi di sleep relativo
            KeDelayExecutionThread(KernelMode, FALSE, &interval);
            continue;
        }

        LONG currentRead = g_PoolRegistry.LogQueue.ReadIndex;
        TelemetryRecord* record = &g_PoolRegistry.LogQueue.Records[currentRead];

        if (g_PoolRegistry.hFileHandle != NULL) {
            // Scrittura sincrona e lineare del record binario sul File Sink di Ring 0
            ZwWriteFile(g_PoolRegistry.hFileHandle, NULL, NULL, NULL, &ioStatus, record, sizeof(TelemetryRecord), NULL, NULL);
        }

        InterlockedExchange(&g_PoolRegistry.LogQueue.ReadIndex, (currentRead + 1) % TELEMETRY_QUEUE_SIZE);
        InterlockedDecrement(&g_PoolRegistry.LogQueue.Count);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

#if 0
NTSTATUS InitializeCorePool(ULONG PercentageToUse) {
    NTSTATUS status;
    RtlZeroMemory(&g_PoolRegistry, sizeof(CorePoolRegistry));

    // UNICORE_STRING wName;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    UNICODE_STRING fileName;

    // Apertura o creazione sicura del file log sink sul disco fisso di Windows
    RtlInitUnicodeString(&fileName, L"\\SystemRoot\\Malamore_Telemetry_Sink.bin");
    InitializeObjectAttributes(&objAttr, &fileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(&g_PoolRegistry.hFileHandle, FILE_APPEND_DATA | SYNCHRONIZE, &objAttr, &ioStatus,
                          NULL, FILE_ATTRIBUTE_NORMAL, 0, FILE_SUPERSEDE, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

    if (!NT_SUCCESS(status)) {
        g_PoolRegistry.hFileHandle = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Allocazione della coda circolare di telemetria nel pool
    // g_PoolRegistry.LogQueue = nullptr;
    // g_PoolRegistry.LogQueue = (TelemetryQueue*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(TelemetryQueue), 'LogQ');
    // if (g_PoolRegistry.LogQueue) {
        RtlZeroMemory(&g_PoolRegistry.LogQueue, sizeof(TelemetryQueue));
    // } else {
    //     g_PoolRegistry.LogQueue = nullptr;
    //     return STATUS_INSUFFICIENT_RESOURCES;
    // }

    ULONG activeProcessors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    ULONG coresToTarget = (activeProcessors * PercentageToUse) / 100;
    if (coresToTarget == 0) coresToTarget = 1;

    ULONG configured = 0;
    for (ULONG i = 1; i < activeProcessors && configured < coresToTarget && configured < MAX_POOL_CORES; ++i) {
        KAFFINITY mask = (KAFFINITY)1 << i;
        g_PoolRegistry.Cores[configured].CoreId = i;
        g_PoolRegistry.Cores[configured].AffinityMask = mask;
        g_PoolRegistry.Cores[configured].IsAllocated = FALSE;

        // ULONG ChIn = MakeDynamicTag('C', 'I', static_cast<int>(i/10UL), static_cast<int>(i%10UL));
        // ULONG ChOut = MakeDynamicTag('C', 'O', static_cast<int>(i/10UL), static_cast<int>(i%10UL));
        // g_PoolRegistry.Cores[configured].InChannel = (InterProcessChannelData*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(InterProcessChannelData), ChIn);
        // g_PoolRegistry.Cores[configured].OutChannel = (InterProcessChannelData*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(InterProcessChannelData), ChOut);

        // if (g_PoolRegistry.Cores[configured].InChannel && g_PoolRegistry.Cores[configured].OutChannel) {
            RtlZeroMemory(&g_PoolRegistry.Cores[configured].InChannel, sizeof(InterProcessChannelData));
            // RtlZeroMemory(&g_PoolRegistry.Cores[configured].OutChannel, sizeof(InterProcessChannelData));
            configured++;
        // }
    }

    g_PoolRegistry.TotalConfiguredCores = configured;
    g_PoolRegistry.IsActive = TRUE;
    g_PoolRegistry.TerminateDiskThread = 0;

    // Avvio del thread parallelo a priorità passiva del Disk Writer
    // if (g_PoolRegistry.LogQueue) {
        PsCreateSystemThread(&g_PoolRegistry.hDiskThread, THREAD_ALL_ACCESS, NULL, NULL, NULL, DiskWriterThreadRoutine, NULL);
    // }

    return (configured > 0) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

/**
 *
 * @param params I parametri del task che si vuole eseguire, tra cui il nome del task
 * @param taskName Ridondante (questo è già contenuto all'interno di params)
 * @param sourceNodeId
 * @param customContext
 * @return
 */
NTSTATUS AllocateTaskToCore(const ServiceParams* params, const char* taskName) {
    // UNREFERENCED_PARAMETER(sourceNodeId);
    if (g_PoolRegistry.IsActive == FALSE) return STATUS_INVALID_DEVICE_STATE;

    // 1. TRADUZIONE DEL PUNTATORE A FUNZIONE DAL REGISTRO COMPITI
    PFN_EXECUTE_ROUTINE_IO realTaskFunction = CercaTaskInTabella(taskName);
    if (!realTaskFunction) {
        return STATUS_NOT_FOUND;
    }

    // 2. SELEZIONE AUTOMATICA: Trova il primo core disponibile gestito dal pool
    CorePoolNoHWTaskEntry* targetSlot = nullptr;
    for (ULONG i = 0; i < g_PoolRegistry.TotalConfiguredCores; ++i) {
        if (!g_PoolRegistry.Cores[i].IsAllocated) {
            targetSlot = &g_PoolRegistry.Cores[i]; // Aggancia il primo core libero
            break;
        }
    }

    // Se la percentuale di core di sicurezza allocata al boot è satura, respinge la richiesta
    if (!targetSlot)
        return STATUS_INSUFFICIENT_RESOURCES;

    // =========================================================================
    // INIZIALIZZAZIONE RISOLUTIVA DEL CUSTOM CONTEXT REALE DI RING 0
    // =========================================================================
    // Allochiamo un buffer statico persistente di 2048 byte nel NonPagedPoolNx
    // dedicato esclusivamente a fare da lavagna dati per questo specifico core logico.
    // ULONG CtxC = MakeDynamicTag('C', 'X', static_cast<int>(targetSlot->CoreId/10UL), static_cast<int>(targetSlot->CoreId%10UL));
    // PVOID allocatedCoreContext = ExAllocatePool2(POOL_FLAG_NON_PAGED, 2048, CtxC);
    // if (!allocatedCoreContext) {
        // return STATUS_INSUFFICIENT_RESOURCES;
    // }
    // RtlZeroMemory(allocatedCoreContext, 2048);

    // 3. ALLOCAZIONE DEI CONTESTI DEL TANDEM NEL POOL NON-PAGATO
    // ULONG TskA = MakeDynamicTag('T', 'A', static_cast<int>(targetSlot->CoreId/10UL), static_cast<int>(targetSlot->CoreId%10UL));
    // ULONG TskB = MakeDynamicTag('T', 'B', static_cast<int>(targetSlot->CoreId/10UL), static_cast<int>(targetSlot->CoreId%10UL));
    // targetSlot->TaskA = (CorePollingContext*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CorePollingContext), TskA);
    // targetSlot->TaskATag = TskA;
    // targetSlot->TaskB = (CorePollingContext*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CorePollingContext), TskB);
    // targetSlot->TaskBTag = TskB;

    // if (!targetSlot->TaskA || !targetSlot->TaskB) {
        // if (targetSlot->TaskA)
            // ExFreePoolWithTag(targetSlot->TaskA, TskA);
        // if (targetSlot->TaskB)
            // ExFreePoolWithTag(targetSlot->TaskB, TskB);
        // return STATUS_INSUFFICIENT_RESOURCES;
    // }

    RtlZeroMemory(&targetSlot->TaskA, sizeof(targetSlot->TaskA));
    RtlZeroMemory(&targetSlot->TaskB, sizeof(targetSlot->TaskB));

    // Il driver inietta autonomamente targetSlot->AffinityMask (Calcolata al boot dal pool_core)
    targetSlot->TaskA.MaxWcetUs = params->EstimatedWcetUs;
    targetSlot->TaskA.AffinityMask = targetSlot->AffinityMask; // <--- ASSEGNATA DA DRIVER!
    targetSlot->TaskA.TaskFunction = realTaskFunction;
    targetSlot->TaskA.CustomContext = customContext;
    targetSlot->TaskA.inChannelSelf = &targetSlot->InChannel;
    targetSlot->TaskA.outChannelSelf = &targetSlot->OutChannel;
    targetSlot->TaskA.IsHighPriority = 1;
    targetSlot->TaskA.runReadOperation = params->runReadOperation;
    targetSlot->TaskA.runWriteOperation = params->runWriteOperation;
    targetSlot->TaskA.saveFileToDisk = params->saveFileToDisk;

    targetSlot->TaskB.MaxWcetUs = params->EstimatedWcetUs;
    targetSlot->TaskB.AffinityMask = targetSlot->AffinityMask; // <--- ASSEGNATA DA DRIVER!
    targetSlot->TaskB.TaskFunction = realTaskFunction;
    targetSlot->TaskB.CustomContext = customContext;
    targetSlot->TaskB.inChannelSelf = &targetSlot->InChannel;
    targetSlot->TaskB.outChannelSelf = &targetSlot->OutChannel;
    targetSlot->TaskB.IsHighPriority = 0;
    targetSlot->TaskB.runReadOperation = params->runReadOperation;
    targetSlot->TaskB.runWriteOperation = params->runWriteOperation;
    targetSlot->TaskB.saveFileToDisk = params->saveFileToDisk;

    targetSlot->IsAllocated = TRUE;
    g_PoolRegistry.ActiveAllocatedCores++;

    KdPrint(("[MALAMORE] Assegnato automaticamente il task '%s' al Core hardware logico %lu\n", taskName, targetSlot->CoreId));
    return STATUS_SUCCESS;
}


NTSTATUS StartAllConfiguredTandems(ULONG64 CurrentTicks, ULONG64 PerfFrequency) {
    if (g_PoolRegistry.ActiveAllocatedCores == 0) return STATUS_INVALID_DEVICE_STATE;

    ULONG64 synchronizedStart = CurrentTicks + (PerfFrequency / 1000ull);

    for (ULONG i = 0; i < g_PoolRegistry.TotalConfiguredCores; ++i) {
        CorePoolNoHWTaskEntry* slot = &g_PoolRegistry.Cores[i];
        if (!slot->IsAllocated || slot->hThreadA != NULL) continue;

        ULONG64 periodTicks = (static_cast<ULONG64>(slot->TaskA.MaxWcetUs * 50) * PerfFrequency) / 1000000ull;

        slot->TaskA.SynchronizedStartTicks = synchronizedStart;
        slot->TaskA.PeriodTicks = periodTicks;
        slot->TaskA.PhaseOffsetTicks = 0;
        slot->TaskA.PerfCounterFrequency = PerfFrequency;
        slot->TaskA.KeepRunning = 1;

        slot->TaskB.SynchronizedStartTicks = synchronizedStart;
        slot->TaskB.PeriodTicks = periodTicks;
        slot->TaskB.PhaseOffsetTicks = periodTicks / 2;
        slot->TaskB.PerfCounterFrequency = PerfFrequency;
        slot->TaskB.KeepRunning = 1;

        PsCreateSystemThread(&slot->hThreadA, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, &slot->TaskA);
        PsCreateSystemThread(&slot->hThreadB, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, &slot->TaskB);
    }
    return STATUS_SUCCESS;
}

VOID StopAndFreeCorePool() {
    g_PoolRegistry.IsActive = FALSE;

    for (ULONG i = 0; i < g_PoolRegistry.TotalConfiguredCores; ++i) {
        CorePoolNoHWTaskEntry* slot = &g_PoolRegistry.Cores[i];

        InterlockedExchange(&slot->TaskA.KeepRunning, 0); // Stopping the execution of the thread, while stopping the cyclic loop
        InterlockedExchange(&slot->TaskB.KeepRunning, 0); // Stopping the execution of the thread, while stopping the cyclic loop

        // Attesa del rientro sicuro ed abbattimento degli handle dei thread. Spegnimento lato OS
        if (slot->hThreadA) {
            PVOID obj;
            if (NT_SUCCESS(ObReferenceObjectByHandle(slot->hThreadA, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &obj, NULL))) {
                KeWaitForSingleObject(obj, Executive, KernelMode, FALSE, NULL);
                ObDereferenceObject(obj);
            }
            ZwClose(slot->hThreadA);
            slot->hThreadA = NULL;
        }

        if (slot->hThreadB) {
            PVOID obj;
            if (NT_SUCCESS(ObReferenceObjectByHandle(slot->hThreadB, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &obj, NULL))) {
                KeWaitForSingleObject(obj, Executive, KernelMode, FALSE, NULL);
                ObDereferenceObject(obj);
            }
            ZwClose(slot->hThreadB);
            slot->hThreadB = NULL;
        }

        // Deallocazione totale della RAM non-pagata
        // if (slot->TaskA) { ExFreePoolWithTag(slot->TaskA, 'TskA'); slot->TaskA = nullptr; }
        // if (slot->TaskB) { ExFreePoolWithTag(slot->TaskB, 'TskB'); slot->TaskB = nullptr; }
        // if (slot->InChannel) { ExFreePoolWithTag(slot->InChannel, 'ChIn'); slot->InChannel = nullptr; }
        // if (slot->OutChannel) { ExFreePoolWithTag(slot->OutChannel, 'ChOu'); slot->OutChannel = nullptr; }

        slot->IsAllocated = FALSE;
    }
    g_PoolRegistry.ActiveAllocatedCores = 0;
}
#endif