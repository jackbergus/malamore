#include <malamore.h>
#include "malamore/pool_core.h"

#include "malamore/service_manager.h"

static CorePoolRegistry g_PoolRegistry;

VOID ScriviRecordSuFileSink(ULONG CoreId, ULONG64 ElapsedUs, char Status) {
     if (!g_PoolRegistry.LogQueue || g_PoolRegistry.IsActive == 0) return;

     TelemetryQueue* queue = g_PoolRegistry.LogQueue;
     if (queue->Count >= TELEMETRY_QUEUE_SIZE) return; // Buffer pieno, scarta per non generare jitter

     LONG currentWrite = queue->WriteIndex;
     TelemetryRecord* record = &queue->Records[currentWrite];

     LARGE_INTEGER ticks;
     ticks = KeQueryPerformanceCounter(NULL);
     record->Timestamp = ticks.QuadPart;
     record->CoreId = CoreId;
     record->ExecutionTimeUs = ElapsedUs;
     record->StatusFlag = Status;

     InterlockedExchange(&queue->WriteIndex, (currentWrite + 1) % TELEMETRY_QUEUE_SIZE);
     InterlockedIncrement(&queue->Count);
 }

// static VOID DiskWriterThreadRoutine(_In_ PVOID Context) {
//     PCONTESTO_SERVIZIO contesto = (PCONTESTO_SERVIZIO)Context;
//     PCODA_CIRCOLARE coda = contesto->CodaFile; IO_STATUS_BLOCK ioStatus;
//     while (!contesto->TerminateThreads) {
//         KeWaitForSingleObject(&contesto->CodaDatiEvent, Executive, KernelMode, FALSE, NULL);
//         while (coda->Tail != coda->Head) {
//             LONG attualeTail = coda->Tail;
//             ZwWriteFile(contesto->FileHandle, NULL, NULL, NULL, &ioStatus,
//                         &coda->Messaggi[attualeTail], sizeof(RECORD_FILE), NULL, NULL);
//             InterlockedExchange(&coda->Tail, (attualeTail + 1) % CODA_MAX_MESSAGGI);
//         }
//         KeResetEvent(&contesto->CodaDatiEvent);
//     }
//     PsTerminateSystemThread(STATUS_SUCCESS);
// }

VOID DiskWriterThreadRoutine(_In_ PVOID Context) {
    UNREFERENCED_PARAMETER(Context);
    TelemetryQueue* queue = g_PoolRegistry.LogQueue;
    IO_STATUS_BLOCK ioStatus;

    // Il thread gira fiso a PASSIVE_LEVEL: può effettuare I/O sincrono su file senza bloccare i core
    while (!InterlockedOr(&g_PoolRegistry.TerminateDiskThread, 0) || queue->Count > 0) {

        if (queue->Count == 0) {
            // Se la coda è vuota, rilascia temporaneamente la CPU con un piccolo stallo preventivo
            LARGE_INTEGER interval;
            interval.QuadPart = -100 * 1000; // 10 millisecondi di sleep relativo
            KeDelayExecutionThread(KernelMode, FALSE, &interval);
            continue;
        }

        LONG currentRead = queue->ReadIndex;
        TelemetryRecord* record = &queue->Records[currentRead];

        if (g_PoolRegistry.hFileHandle != NULL) {
            // Scrittura sincrona e lineare del record binario sul File Sink di Ring 0
            ZwWriteFile(g_PoolRegistry.hFileHandle, NULL, NULL, NULL, &ioStatus, record, sizeof(TelemetryRecord), NULL, NULL);
        }

        InterlockedExchange(&queue->ReadIndex, (currentRead + 1) % TELEMETRY_QUEUE_SIZE);
        InterlockedDecrement(&queue->Count);
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

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

    if (!NT_SUCCESS(status)) g_PoolRegistry.hFileHandle = NULL;

    // Allocazione della coda circolare di telemetria nel pool non-pagato
    g_PoolRegistry.LogQueue = (TelemetryQueue*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(TelemetryQueue), 'LogQ');
    if (g_PoolRegistry.LogQueue) RtlZeroMemory(g_PoolRegistry.LogQueue, sizeof(TelemetryQueue));

    ULONG activeProcessors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    ULONG coresToTarget = (activeProcessors * PercentageToUse) / 100;
    if (coresToTarget == 0) coresToTarget = 1;

    ULONG configured = 0;
    for (ULONG i = 1; i < activeProcessors && configured < coresToTarget && configured < MAX_POOL_CORES; ++i) {
        KAFFINITY mask = (KAFFINITY)1 << i;
        g_PoolRegistry.Cores[configured].CoreId = i;
        g_PoolRegistry.Cores[configured].AffinityMask = mask;
        g_PoolRegistry.Cores[configured].IsAllocated = FALSE;

        g_PoolRegistry.Cores[configured].InChannel = (InterProcessChannelData*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(InterProcessChannelData), 'ChIn');
        g_PoolRegistry.Cores[configured].OutChannel = (InterProcessChannelData*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(InterProcessChannelData), 'ChOu');

        if (g_PoolRegistry.Cores[configured].InChannel && g_PoolRegistry.Cores[configured].OutChannel) {
            RtlZeroMemory(g_PoolRegistry.Cores[configured].InChannel, sizeof(InterProcessChannelData));
            RtlZeroMemory(g_PoolRegistry.Cores[configured].OutChannel, sizeof(InterProcessChannelData));
            configured++;
        }
    }

    g_PoolRegistry.TotalConfiguredCores = configured;
    g_PoolRegistry.IsActive = 1;
    g_PoolRegistry.TerminateDiskThread = 0;

    // Avvio del thread parallelo a priorità passiva del Disk Writer
    if (g_PoolRegistry.LogQueue) {
        PsCreateSystemThread(&g_PoolRegistry.hDiskThread, THREAD_ALL_ACCESS, NULL, NULL, NULL, DiskWriterThreadRoutine, NULL);
    }

    return (configured > 0) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

// NTSTATUS InitializeCorePool(ULONG PercentageToUse) {
//     RtlZeroMemory(&g_PoolRegistry, sizeof(CorePoolRegistry));
//
//     if (PercentageToUse == 0 || PercentageToUse > 100) {
//         PercentageToUse = 50; // Fallback di sicurezza: usa la metà dei core
//     }
//
//     // 1. SCANSIONE DELLA TOPOLOGIA REALE DI WINDOWS NT
//     ULONG activeProcessors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
//
//     // Calcolo della percentuale conservativa dei core da blindare
//     ULONG coresToTarget = (activeProcessors * PercentageToUse) / 100;
//     if (coresToTarget == 0) coresToTarget = 1;
//
//     ULONG configured = 0;
//
//     // 2. ALLOCAZIONE CONSERVATIVA (Escludiamo sempre il Core 0 di sistema)
//     for (ULONG i = 1; i < activeProcessors && configured < coresToTarget && configured < MAX_POOL_CORES; ++i) {
//
//         // Verifichiamo la maschera hardware del core corrente
//         KAFFINITY mask = (KAFFINITY)1 << i;
//
//         g_PoolRegistry.Cores[configured].CoreId = i;
//         g_PoolRegistry.Cores[configured].AffinityMask = mask;
//         g_PoolRegistry.Cores[configured].IsAllocated = FALSE;
//
//         // Allocazione preliminare dei canali di Ring 0 nel NonPagedPoolNx
//         g_PoolRegistry.Cores[configured].InChannel = (InterProcessChannelData*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(InterProcessChannelData), 'ChIn');
//         g_PoolRegistry.Cores[configured].OutChannel = (InterProcessChannelData*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(InterProcessChannelData), 'ChOu');
//
//         if (g_PoolRegistry.Cores[configured].InChannel && g_PoolRegistry.Cores[configured].OutChannel) {
//             RtlZeroMemory(g_PoolRegistry.Cores[configured].InChannel, sizeof(InterProcessChannelData));
//             RtlZeroMemory(g_PoolRegistry.Cores[configured].OutChannel, sizeof(InterProcessChannelData));
//             configured++;
//         }
//     }
//
//     g_PoolRegistry.TotalConfiguredCores = configured;
//     g_PoolRegistry.IsActive = 1;
//
//     return (configured > 0) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
// }

// All'interno di src/malamore/pool_core.cpp

NTSTATUS AllocateTaskToCore(const ServiceParams* params, const char* taskName, ULONG sourceNodeId, PVOID customContext) {
    UNREFERENCED_PARAMETER(sourceNodeId);
    if (g_PoolRegistry.IsActive == 0) return STATUS_INVALID_DEVICE_STATE;

    // 1. TRADUZIONE DEL PUNTATORE A FUNZIONE DAL REGISTRO COMPITI
    PFN_EXECUTE_ROUTINE_IO realTaskFunction = CercaTaskInTabella(taskName);
    if (!realTaskFunction) {
        return STATUS_NOT_FOUND;
    }

    // 2. SELEZIONE AUTOMATICA: Trova il primo core disponibile gestito dal pool
    CorePoolEntry* targetSlot = nullptr;
    for (ULONG i = 0; i < g_PoolRegistry.TotalConfiguredCores; ++i) {
        if (!g_PoolRegistry.Cores[i].IsAllocated) {
            targetSlot = &g_PoolRegistry.Cores[i]; // Aggancia il primo core libero
            break;
        }
    }

    // Se la percentuale di core di sicurezza allocata al boot è satura, respinge la richiesta
    if (!targetSlot) return STATUS_INSUFFICIENT_RESOURCES;

    // 3. ALLOCAZIONE DEI CONTESTI DEL TANDEM NEL POOL NON-PAGATO
    targetSlot->TaskA = (CorePollingContext*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CorePollingContext), 'TskA');
    targetSlot->TaskB = (CorePollingContext*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CorePollingContext), 'TskB');

    if (!targetSlot->TaskA || !targetSlot->TaskB) {
        if (targetSlot->TaskA) ExFreePoolWithTag(targetSlot->TaskA, 'TskA');
        if (targetSlot->TaskB) ExFreePoolWithTag(targetSlot->TaskB, 'TskB');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(targetSlot->TaskA, sizeof(CorePollingContext));
    RtlZeroMemory(targetSlot->TaskB, sizeof(CorePollingContext));

    // Il driver inietta autonomamente targetSlot->AffinityMask (Calcolata al boot dal pool_core)
    targetSlot->TaskA->MaxWcetUs = params->EstimatedWcetUs;
    targetSlot->TaskA->AffinityMask = targetSlot->AffinityMask; // <--- ASSEGNATA DA DRIVER!
    targetSlot->TaskA->TaskFunction = realTaskFunction;
    targetSlot->TaskA->CustomContext = customContext;
    targetSlot->TaskA->pInChannelData = targetSlot->InChannel;
    targetSlot->TaskA->pOutChannelData = targetSlot->OutChannel;
    targetSlot->TaskA->IsHighPriority = 1;

    targetSlot->TaskB->MaxWcetUs = params->EstimatedWcetUs;
    targetSlot->TaskB->AffinityMask = targetSlot->AffinityMask; // <--- ASSEGNATA DA DRIVER!
    targetSlot->TaskB->TaskFunction = realTaskFunction;
    targetSlot->TaskB->CustomContext = customContext;
    targetSlot->TaskB->pInChannelData = targetSlot->InChannel;
    targetSlot->TaskB->pOutChannelData = targetSlot->OutChannel;
    targetSlot->TaskB->IsHighPriority = 0;

    targetSlot->IsAllocated = TRUE;
    g_PoolRegistry.ActiveAllocatedCores++;

    KdPrint(("[MALAMORE] Assegnato automaticamente il task '%s' al Core hardware logico %lu\n", taskName, targetSlot->CoreId));
    return STATUS_SUCCESS;
}


NTSTATUS StartAllConfiguredTandems(ULONG64 CurrentTicks, ULONG64 PerfFrequency) {
    if (g_PoolRegistry.ActiveAllocatedCores == 0) return STATUS_INVALID_DEVICE_STATE;

    ULONG64 synchronizedStart = CurrentTicks + (PerfFrequency / 1000ull);

    for (ULONG i = 0; i < g_PoolRegistry.TotalConfiguredCores; ++i) {
        CorePoolEntry* slot = &g_PoolRegistry.Cores[i];
        if (!slot->IsAllocated || slot->hThreadA != NULL) continue;

        ULONG64 periodTicks = (static_cast<ULONG64>(slot->TaskA->MaxWcetUs * 50) * PerfFrequency) / 1000000ull;

        slot->TaskA->SynchronizedStartTicks = synchronizedStart;
        slot->TaskA->PeriodTicks = periodTicks;
        slot->TaskA->PhaseOffsetTicks = 0;
        slot->TaskA->PerfCounterFrequency = PerfFrequency;
        slot->TaskA->KeepRunning = 1;

        slot->TaskB->SynchronizedStartTicks = synchronizedStart;
        slot->TaskB->PeriodTicks = periodTicks;
        slot->TaskB->PhaseOffsetTicks = periodTicks / 2;
        slot->TaskB->PerfCounterFrequency = PerfFrequency;
        slot->TaskB->KeepRunning = 1;

        PsCreateSystemThread(&slot->hThreadA, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, slot->TaskA);
        PsCreateSystemThread(&slot->hThreadB, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, slot->TaskB);
    }
    return STATUS_SUCCESS;
}

// NTSTATUS AllocateTaskToCore(const struct ServiceParams* params, ULONG sourceNodeId, PFN_EXECUTE_ROUTINE_IO taskFunc, PVOID customContext) {
//     if (g_PoolRegistry.IsActive == 0) return STATUS_INVALID_DEVICE_STATE;
//
//     // 3. RICERCA DI UNO SLOT DISPONIBILE NELLA TABELLA DEL POOL
//     CorePoolEntry* targetSlot = nullptr;
//     for (ULONG i = 0; i < g_PoolRegistry.TotalConfiguredCores; ++i) {
//         if (!g_PoolRegistry.Cores[i].IsAllocated) {
//             targetSlot = &g_PoolRegistry.Cores[i];
//             break;
//         }
//     }
//
//     // Se tutti i core configurati dalla percentuale sono pieni, fallisce preservando il sistema
//     if (!targetSlot) return STATUS_INSUFFICIENT_RESOURCES;
//
//     // Allocazione dinamica differenziata dei contesti del tandem per lo slot attuale
//     targetSlot->TaskA = (CorePollingContext*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CorePollingContext), 'TskA');
//     targetSlot->TaskB = (CorePollingContext*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CorePollingContext), 'TskB');
//
//     if (!targetSlot->TaskA || !targetSlot->TaskB) {
//         if (targetSlot->TaskA) ExFreePoolWithTag(targetSlot->TaskA, 'TskA');
//         if (targetSlot->TaskB) ExFreePoolWithTag(targetSlot->TaskB, 'TskB');
//         return STATUS_INSUFFICIENT_RESOURCES;
//     }
//
//     RtlZeroMemory(targetSlot->TaskA, sizeof(CorePollingContext));
//     RtlZeroMemory(targetSlot->TaskB, sizeof(CorePollingContext));
//
//     // Popolamento dei canali e dei puntatori funzionali estratti dall'Ioctl
//     targetSlot->TaskA->MaxWcetUs = params->EstimatedWcetUs;
//     targetSlot->TaskA->AffinityMask = targetSlot->AffinityMask;
//     targetSlot->TaskA->TaskFunction = taskFunc;
//     targetSlot->TaskA->CustomContext = customContext;
//     targetSlot->TaskA->pInChannelData = targetSlot->InChannel;
//     targetSlot->TaskA->pOutChannelData = targetSlot->OutChannel;
//     targetSlot->TaskA->IsHighPriority = 1; // Parte High
//
//     targetSlot->TaskB->MaxWcetUs = params->EstimatedWcetUs;
//     targetSlot->TaskB->AffinityMask = targetSlot->AffinityMask;
//     targetSlot->TaskB->TaskFunction = taskFunc;
//     targetSlot->TaskB->CustomContext = customContext;
//     targetSlot->TaskB->pInChannelData = targetSlot->InChannel;
//     targetSlot->TaskB->pOutChannelData = targetSlot->OutChannel;
//     targetSlot->TaskB->IsHighPriority = 0; // Parte Low (Yielding)
//
//     targetSlot->IsAllocated = TRUE;
//     g_PoolRegistry.ActiveAllocatedCores++;
//
//     return STATUS_SUCCESS;
// }
//
// NTSTATUS StartAllConfiguredTandems(ULONG64 CurrentTicks, ULONG64 PerfFrequency) {
//     if (g_PoolRegistry.ActiveAllocatedCores == 0) return STATUS_INVALID_DEVICE_STATE;
//
//     // Calcolo della barriera temporale comune a livello di Pool di Core: Attuale + 1ms
//     ULONG64 synchronizedStart = CurrentTicks + (PerfFrequency / 1000ULL);
//
//     for (ULONG i = 0; i < g_PoolRegistry.TotalConfiguredCores; ++i) {
//         CorePoolEntry* slot = &g_PoolRegistry.Cores[i];
//         if (!slot->IsAllocated || slot->hThreadA != NULL) continue;
//
//         ULONG64 periodTicks = (static_cast<ULONG64>(slot->TaskA->MaxWcetUs * 50) * PerfFrequency) / 1000000ULL; // Esempio proporzionale
//
//         // Configurazione delle scadenze orarie speculari allineate sulla barriera
//         slot->TaskA->SynchronizedStartTicks = synchronizedStart;
//         slot->TaskA->PeriodTicks = periodTicks;
//         slot->TaskA->PhaseOffsetTicks = 0;
//         slot->TaskA->PerfCounterFrequency = PerfFrequency;
//         slot->TaskA->KeepRunning = 1;
//
//         slot->TaskB->SynchronizedStartTicks = synchronizedStart;
//         slot->TaskB->PeriodTicks = periodTicks;
//         slot->TaskB->PhaseOffsetTicks = periodTicks / 2; // Opposizione di fase T/2 rigida
//         slot->TaskB->PerfCounterFrequency = PerfFrequency;
//         slot->TaskB->KeepRunning = 1;
//
//         // Innesco simultaneo dei System Thread sul core logico blindato
//         PsCreateSystemThread(&slot->hThreadA, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, slot->TaskA);
//         PsCreateSystemThread(&slot->hThreadB, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, slot->TaskB);
//     }
//
//     return STATUS_SUCCESS;
// }

VOID StopAndFreeCorePool() {
    g_PoolRegistry.IsActive = 0;

    for (ULONG i = 0; i < g_PoolRegistry.TotalConfiguredCores; ++i) {
        CorePoolEntry* slot = &g_PoolRegistry.Cores[i];

        if (slot->TaskA) InterlockedExchange(&slot->TaskA->KeepRunning, 0);
        if (slot->TaskB) InterlockedExchange(&slot->TaskB->KeepRunning, 0);

        // Attesa del rientro sicuro ed abbattimento degli handle dei thread
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
        if (slot->TaskA) { ExFreePoolWithTag(slot->TaskA, 'TskA'); slot->TaskA = nullptr; }
        if (slot->TaskB) { ExFreePoolWithTag(slot->TaskB, 'TskB'); slot->TaskB = nullptr; }
        if (slot->InChannel) { ExFreePoolWithTag(slot->InChannel, 'ChIn'); slot->InChannel = nullptr; }
        if (slot->OutChannel) { ExFreePoolWithTag(slot->OutChannel, 'ChOu'); slot->OutChannel = nullptr; }

        slot->IsAllocated = FALSE;
    }
    g_PoolRegistry.ActiveAllocatedCores = 0;
}
