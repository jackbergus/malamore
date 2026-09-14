#include <malamore.h>
#include "malamore/pool_core.h"

static CorePoolRegistry g_PoolRegistry;

NTSTATUS InitializeCorePool(ULONG PercentageToUse) {
    RtlZeroMemory(&g_PoolRegistry, sizeof(CorePoolRegistry));

    if (PercentageToUse == 0 || PercentageToUse > 100) {
        PercentageToUse = 50; // Fallback di sicurezza: usa la metà dei core
    }

    // 1. SCANSIONE DELLA TOPOLOGIA REALE DI WINDOWS NT
    ULONG activeProcessors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    // Calcolo della percentuale conservativa dei core da blindare
    ULONG coresToTarget = (activeProcessors * PercentageToUse) / 100;
    if (coresToTarget == 0) coresToTarget = 1;

    ULONG configured = 0;

    // 2. ALLOCAZIONE CONSERVATIVA (Escludiamo sempre il Core 0 di sistema)
    for (ULONG i = 1; i < activeProcessors && configured < coresToTarget && configured < MAX_POOL_CORES; ++i) {

        // Verifichiamo la maschera hardware del core corrente
        KAFFINITY mask = (KAFFINITY)1 << i;

        g_PoolRegistry.Cores[configured].CoreId = i;
        g_PoolRegistry.Cores[configured].AffinityMask = mask;
        g_PoolRegistry.Cores[configured].IsAllocated = FALSE;

        // Allocazione preliminare dei canali di Ring 0 nel NonPagedPoolNx
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

    return (configured > 0) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS AllocateTaskToCore(const struct ServiceParams* params, ULONG sourceNodeId, PFN_EXECUTE_ROUTINE_IO taskFunc, PVOID customContext) {
    if (g_PoolRegistry.IsActive == 0) return STATUS_INVALID_DEVICE_STATE;

    // 3. RICERCA DI UNO SLOT DISPONIBILE NELLA TABELLA DEL POOL
    CorePoolEntry* targetSlot = nullptr;
    for (ULONG i = 0; i < g_PoolRegistry.TotalConfiguredCores; ++i) {
        if (!g_PoolRegistry.Cores[i].IsAllocated) {
            targetSlot = &g_PoolRegistry.Cores[i];
            break;
        }
    }

    // Se tutti i core configurati dalla percentuale sono pieni, fallisce preservando il sistema
    if (!targetSlot) return STATUS_INSUFFICIENT_RESOURCES;

    // Allocazione dinamica differenziata dei contesti del tandem per lo slot attuale
    targetSlot->TaskA = (CorePollingContext*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CorePollingContext), 'TskA');
    targetSlot->TaskB = (CorePollingContext*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CorePollingContext), 'TskB');

    if (!targetSlot->TaskA || !targetSlot->TaskB) {
        if (targetSlot->TaskA) ExFreePoolWithTag(targetSlot->TaskA, 'TskA');
        if (targetSlot->TaskB) ExFreePoolWithTag(targetSlot->TaskB, 'TskB');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(targetSlot->TaskA, sizeof(CorePollingContext));
    RtlZeroMemory(targetSlot->TaskB, sizeof(CorePollingContext));

    // Popolamento dei canali e dei puntatori funzionali estratti dall'Ioctl
    targetSlot->TaskA->MaxWcetUs = params->EstimatedWcetUs;
    targetSlot->TaskA->AffinityMask = targetSlot->AffinityMask;
    targetSlot->TaskA->TaskFunction = taskFunc;
    targetSlot->TaskA->CustomContext = customContext;
    targetSlot->TaskA->pInChannelData = targetSlot->InChannel;
    targetSlot->TaskA->pOutChannelData = targetSlot->OutChannel;
    targetSlot->TaskA->IsHighPriority = 1; // Parte High

    targetSlot->TaskB->MaxWcetUs = params->EstimatedWcetUs;
    targetSlot->TaskB->AffinityMask = targetSlot->AffinityMask;
    targetSlot->TaskB->TaskFunction = taskFunc;
    targetSlot->TaskB->CustomContext = customContext;
    targetSlot->TaskB->pInChannelData = targetSlot->InChannel;
    targetSlot->TaskB->pOutChannelData = targetSlot->OutChannel;
    targetSlot->TaskB->IsHighPriority = 0; // Parte Low (Yielding)

    targetSlot->IsAllocated = TRUE;
    g_PoolRegistry.ActiveAllocatedCores++;

    return STATUS_SUCCESS;
}

NTSTATUS StartAllConfiguredTandems(ULONG64 CurrentTicks, ULONG64 PerfFrequency) {
    if (g_PoolRegistry.ActiveAllocatedCores == 0) return STATUS_INVALID_DEVICE_STATE;

    // Calcolo della barriera temporale comune a livello di Pool di Core: Attuale + 1ms
    ULONG64 synchronizedStart = CurrentTicks + (PerfFrequency / 1000ULL);

    for (ULONG i = 0; i < g_PoolRegistry.TotalConfiguredCores; ++i) {
        CorePoolEntry* slot = &g_PoolRegistry.Cores[i];
        if (!slot->IsAllocated || slot->hThreadA != NULL) continue;

        ULONG64 periodTicks = (static_cast<ULONG64>(slot->TaskA->MaxWcetUs * 50) * PerfFrequency) / 1000000ULL; // Esempio proporzionale

        // Configurazione delle scadenze orarie speculari allineate sulla barriera
        slot->TaskA->SynchronizedStartTicks = synchronizedStart;
        slot->TaskA->PeriodTicks = periodTicks;
        slot->TaskA->PhaseOffsetTicks = 0;
        slot->TaskA->PerfCounterFrequency = PerfFrequency;
        slot->TaskA->KeepRunning = 1;

        slot->TaskB->SynchronizedStartTicks = synchronizedStart;
        slot->TaskB->PeriodTicks = periodTicks;
        slot->TaskB->PhaseOffsetTicks = periodTicks / 2; // Opposizione di fase T/2 rigida
        slot->TaskB->PerfCounterFrequency = PerfFrequency;
        slot->TaskB->KeepRunning = 1;

        // Innesco simultaneo dei System Thread sul core logico blindato
        PsCreateSystemThread(&slot->hThreadA, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, slot->TaskA);
        PsCreateSystemThread(&slot->hThreadB, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, slot->TaskB);
    }

    return STATUS_SUCCESS;
}

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
