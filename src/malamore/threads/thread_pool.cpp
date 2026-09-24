//
// Created by Giacomo Bergami on 15/09/2026.
//

#include <malamore/service_manager.h>
#include <malamore/threads/thread_pool.h>
// #include <ntstrsafe.h> // <--- NUOVO: Routine di manipolazione stringhe sicure nativamente in Ring 0

#include "malamore/utility/Path.h"
#include "malamore/utility/telemetry_formatter.h"
#include "malamore/utility/streambuf/MalamoreOStream.h"
#include "malamore/wrapper_tasks/hardware_reader.h"
#include "malamore/wrapper_tasks/telemetry_writer.h"

// Allocazione statica contigua nel segmento dati (Zero heap runtime - Specifica 4)
static CorePoolRegistry g_PoolRegistry;
static MalamorePath     telemetryFilePath{L"Malamore_Telemetry_Sink.txt"};
static MalamoreOStream  telemetryFile;
// static LARGE_INTEGER freq;
// static LARGE_INTEGER currentTicks;


extern "C" ULONG64 usecToTicks(const ULONG64 usec) {
    return (static_cast<ULONG64>(usec) * g_PoolRegistry.snapshot.qpcFrequency.QuadPart) / 1000000ULL;
}

extern "C" BOOL isTelemetryQueueEmpty() {
    return RealTimeIPC_IsEmpty(&g_PoolRegistry.TelemetryChannel) ? TRUE : FALSE;
}

extern "C" VOID dumpBinaryTelemetryToFile() {
    const struct DataChannelParcel* msg = RealTimeIPC_Top(&g_PoolRegistry.TelemetryChannel);
    while (msg) {
        telemetryFile.Sputn(reinterpret_cast<const char *>(msg), sizeof(*msg));
        // KdPrint(("%s\n", msg->PayloadData));
        RealTimeIPC_Pop(&g_PoolRegistry.TelemetryChannel);
        msg = RealTimeIPC_Top(&g_PoolRegistry.TelemetryChannel);
    }
    telemetryFile.Flush();
}

extern "C" NTSTATUS CorePool_RouteMessageSrcToDest(ULONGLONG Timestamp, ULONG SourceNodeId, ULONG targetNodeId,  const UCHAR* PayloadData ,ULONG DataLength) {
    InterProcessChannelData* pChannel;
    if (targetNodeId == TELEMETRY_WRITER_ID) {
        pChannel = &g_PoolRegistry.TelemetryChannel;
    } else {
        ULONG channelIndex = Malamore::Math::WrapIndex<MAX_GENERIC_TASKS>(targetNodeId);
        pChannel = &g_PoolRegistry.Tasks[channelIndex].IpcChannel;
    }

    if (RealTimeIPC_Push(pChannel, Timestamp, SourceNodeId, PayloadData, DataLength)) {
        return STATUS_SUCCESS;
    }

    KdPrint(("[CorePool_RegisterGenericTask] It seems that the queue is full, and you cannot push another message \n"));
    return STATUS_INSUFFICIENT_RESOURCES;
}

extern "C" NTSTATUS CorePool_RouteMessageToDest(ULONG targetNodeId, const struct DataChannelParcel* packet) {
    ULONG channelIndex = Malamore::Math::WrapIndex<MAX_GENERIC_TASKS>(targetNodeId);
    InterProcessChannelData* pChannel = &g_PoolRegistry.Tasks[channelIndex].IpcChannel;

    if (RealTimeIPC_Push(pChannel, packet)) {
        return STATUS_SUCCESS;
    }

    KdPrint(("[CorePool_RegisterGenericTask] It seems that the queue is full, and you cannot push another message \n"));
    return STATUS_INSUFFICIENT_RESOURCES;
}


void clear_task_and_thread(struct MalamoreThreadSlot &thread) {
    InterlockedExchange(&thread.threadArgument.Context.KeepRunning, 0);
    RtlZeroMemory(&thread.threadArgument.FutureResult, sizeof(thread.threadArgument.FutureResult));
    thread.threadArgument.IsCompleted = 0;
    thread.threadArgument.ThreadHandle = NULL;
    thread.threadArgument.Context.TaskFunctionPointer = NULL;
    RtlZeroMemory(&thread.threadArgument.Context.DevicePath, MAX_PATH);
    thread.threadArgument.Context.SynchronizedStartTicks = 0;
    thread.threadArgument.Context.PhaseOffsetTicks = 0;
    thread.threadArgument.Context.PeriodUs = 0;
    thread.threadArgument.Context.MaxBudgetUs = 0;
    thread.threadArgument.Context.IsHighPriority = 1;
    thread.threadArgument.Context.KeepRunning = 0;
    thread.threadArgument.Context.runReadOperation = FALSE;
    thread.threadArgument.Context.runWriteOperation = FALSE;
    thread.threadArgument.Context.saveFileToDisk = FALSE;
}

extern "C"
NTSTATUS CorePool_InitializeRegistry(ULONG PercentageToUse) {
    NTSTATUS status;
    // Sostituito RtlZeroMemory al posto di macro non standard
    RtlZeroMemory(&g_PoolRegistry, sizeof(CorePoolRegistry));
    fillInSnapshot(&g_PoolRegistry.snapshot);

    ULONG activeProcessors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (activeProcessors < 3) {
        KdPrint(("Error: we have just %llu processors available, we require at least 4 of them...", activeProcessors));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ULONG coresToTarget = (activeProcessors * PercentageToUse) / 100;
    if (coresToTarget == 0)
        coresToTarget = 4;

    ULONG configured = 0;
    for (ULONG i = 0; i < activeProcessors && configured < coresToTarget && configured < MAX_TOTAL_SLOTS; ++i) {
        KAFFINITY mask = static_cast<KAFFINITY>(1) << i;
        auto& thread = g_PoolRegistry.ThreadPool[configured];
        thread.BaseAffinityMask = mask;
        thread.Type = MalamoreThreadType::THREAD_TYPE_FREE;
        thread.SlotId = i;
        thread.threadArgument.Context.AffinityMask = mask;
        thread.threadArgument.Context.PerfCounterFrequency = g_PoolRegistry.snapshot.qpcFrequency.QuadPart;
        thread.threadArgument.Context.TargetThreadId = i;
        clear_task_and_thread(thread);
        configured++;
    }

    g_PoolRegistry.CurrentHwThreadsCount = configured;
    g_PoolRegistry.TerminateDiskThread = 0;

    status = telemetryFile.Open(telemetryFilePath, StreamFlags::Trunc | StreamFlags::Creat | StreamFlags::Write);
    RealTimeIPC_Init(&g_PoolRegistry.TelemetryChannel);
    RealTimeIPC_Push(&g_PoolRegistry.TelemetryChannel, 0, ULLONG_MAX, reinterpret_cast<const UCHAR *>(&g_PoolRegistry.snapshot), sizeof(g_PoolRegistry.snapshot));

    // Configurazione dei contatori iniziali di controllo per la flotta omogenea
    g_PoolRegistry.CurrentGenericTasksCount = 0;
    g_PoolRegistry.TotalAllocatedSlots = 0;
    // Armiamo lo stato del registro e resettiamo il flag di arresto della telemetria
    g_PoolRegistry.IsRegistryActive = TRUE; // TRUE
    g_PoolRegistry.TerminateDiskThread = 0;

    /////////Now, registering the Telemetry Thread
        if (g_PoolRegistry.IsRegistryActive == 0) {
        KdPrint(("[CorePool_RegisterHwReader] Inactive registry, cannot continue with registering an HW reader \n"));
        return STATUS_INVALID_DEVICE_STATE;
    }

    // 3. SELEZIONE AUTOMATICA: Trova il primo slot omogeneo disponibile nel pool statico
    struct MalamoreThreadSlot* slot = nullptr;
    struct MalamoreTaskSlot* task = nullptr;
    for (LONG i = MAX_TOTAL_SLOTS-1; i >= 0; --i) {
        // Uno slot è libero se il suo tipo è impostato a THREAD_TYPE_FREE
        if (g_PoolRegistry.ThreadPool[i].Type == THREAD_TYPE_FREE) {
            slot = &g_PoolRegistry.ThreadPool[i];
            task = &g_PoolRegistry.Tasks[i];
            break;
        }
    }

    // Se l'array statico unificato pre-allocato è saturo, respinge la richiesta per proteggere il sistema
    if ((!slot) || (!task)) {
        KdPrint(("[CorePool_RegisterGenericTask] It seems that all threads in the pool are locked. I cannot do anything more. \n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    slot->Type = THREAD_TYPE_TELEMETRY_WRITER;
    task->ContextAB[0] = slot;
    auto* ctx = &slot->threadArgument.Context;
    // struct UnifiedThreadContext* ctx = &slot->ContextA;
    // slot->ProcessId = slot->SlotId;
    ctx->PeriodUs = 5000;
    ctx->MaxBudgetUs = 5000;
    ctx->TargetThreadId = -1;
    ctx->TaskFunctionPointer = TelemetrySink;
    // RtlZeroMemory(ctx->DevicePath, sizeof(ctx->DevicePath));
    ctx->KeepRunning = 0;
    ctx->PhaseOffsetTicks = 0;
    g_PoolRegistry.CurrentHwThreadsCount++;
    g_PoolRegistry.TotalAllocatedSlots++;

    // Ritorna il successo: l'infrastruttura statica è pronta a ricevere le registrazioni dei nodi
    return STATUS_SUCCESS;
}


extern "C"
NTSTATUS CorePool_RegisterHwReader(
    const WCHAR* devicePath,
    ULONG readTimeoutUs,
    ULONG samplingPeriodUs,
    ULONG targetTaskId,
    BOOL  asynch
) {

    if (g_PoolRegistry.IsRegistryActive == 0) {
        KdPrint(("[CorePool_RegisterHwReader] Inactive registry, cannot continue with registering an HW reader \n"));
        return STATUS_INVALID_DEVICE_STATE;
    }

    // 3. SELEZIONE AUTOMATICA: Trova il primo slot omogeneo disponibile nel pool statico
    struct MalamoreThreadSlot* slot = nullptr;
    struct MalamoreTaskSlot* task = nullptr;
    for (LONG i = 0; i < MAX_TOTAL_SLOTS; ++i) {
        // Uno slot è libero se il suo tipo è impostato a THREAD_TYPE_FREE
        if (g_PoolRegistry.ThreadPool[i].Type == THREAD_TYPE_FREE) {
            slot = &g_PoolRegistry.ThreadPool[i];
            task = &g_PoolRegistry.Tasks[i];
            break;
        }
    }

    // Se l'array statico unificato pre-allocato è saturo, respinge la richiesta per proteggere il sistema
    if ((!slot) || (!task)) {
        KdPrint(("[CorePool_RegisterGenericTask] It seems that all threads in the pool are locked. I cannot do anything more. \n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    slot->Type = THREAD_TYPE_HW_READER;
    task->ContextAB[0] = slot;
    auto* ctx = &slot->threadArgument.Context;
    // struct UnifiedThreadContext* ctx = &slot->ContextA;
    // slot->ProcessId = slot->SlotId;
    ctx->PeriodUs = samplingPeriodUs;
    ctx->MaxBudgetUs = readTimeoutUs;
    ctx->TargetThreadId = targetTaskId;
    ctx->TaskFunctionPointer = HardwareReaderThreadRoutine;

    // CORREZIONE HARDWARE: Utilizziamo la funzione sicura del kernel per copiare la stringa WCHAR
    // Sostituisce wcscpy_s dello spazio utente
    // NTSTATUS strStatus = RtlStringCchCopyW(ctx->DevicePath, MAX_PATH, devicePath);
    auto idx = MalamoreDevicePool_AddDevicePath(devicePath, asynch);
    if (idx == -1) {
        KdPrint(("[CorePool_RegisterHwReader] Cannot copy the devicePath to ctx->DevicePath \n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    ctx->DevicePath = &g_PoolRegistry.Devices[idx].Stream;
    ctx->KeepRunning = 0;
    ctx->PhaseOffsetTicks = 0;

    g_PoolRegistry.CurrentHwThreadsCount++;
    g_PoolRegistry.TotalAllocatedSlots++;
    return STATUS_SUCCESS;
}

extern "C"
NTSTATUS CorePool_StartPoolExecution() {
    NTSTATUS status = STATUS_SUCCESS;
    LARGE_INTEGER freq, currentTicks;

    // Cattura istantanea della costante di frequenza hardware della macchina
    currentTicks = KeQueryPerformanceCounter(&freq);

    // Calcolo della Barriera Oraria Unica Globale (Tempo Attuale + 1ms di rincorsa)
    // Questo millisecondo fornisce il budget di tempo necessario alla CPU per completare
    // i cambi di contesto e posizionare i thread sui core corretti prima dello scatto in fase.
    ULONG64 synchronizedStart = currentTicks.QuadPart + (static_cast<ULONG64>(freq.QuadPart) / 1000ULL);
    g_PoolRegistry.GlobalSynchronizedStartTicks = synchronizedStart;
    g_PoolRegistry.GlobalPerfCounterFrequency = freq.QuadPart;

    for (ULONG i = 0; i < MAX_TOTAL_SLOTS; ++i) {
        // Uno slot è libero se il suo tipo è impostato a THREAD_TYPE_FREE
        if (g_PoolRegistry.ThreadPool[i].Type != THREAD_TYPE_FREE) {
            auto* slot = &g_PoolRegistry.ThreadPool[i];
            auto* task = &g_PoolRegistry.Tasks[i];

            switch (slot->Type) {
                case THREAD_TYPE_TELEMETRY_WRITER:
                case THREAD_TYPE_HW_READER: {
                    struct UnifiedThreadContext* ctx = &task->ContextAB[0]->threadArgument.Context;
                    ctx->SynchronizedStartTicks = synchronizedStart;
                    ctx->PerfCounterFrequency = freq.QuadPart;
                    ctx->KeepRunning = 1;

                    // Tentativo di spawn del System Thread
                    auto& t = task->ContextAB[0]->actualThread;
                    if (slot->Type == THREAD_TYPE_HW_READER) {
                        t.Init(&task->ContextAB[0]->threadArgument, HardwareReaderThreadRoutine);
                    } else { // THREAD_TYPE_TELEMETRY_WRITER
                        t.Init(&task->ContextAB[0]->threadArgument, TelemetrySink);
                    }
                    status = t.Start();

                    // VERIFICA RIGIDA DI INTEGRITÀ DI SPATIAL SPAWN
                    if (!NT_SUCCESS(status)) {
                        KdPrint(("[MALAMORE 🚨] Fallimento spawn Thread HW allo slot %lu. Status: 0x%X. Avvio Rollback...\n", i, status));
                        t.Stop();

                        // Interrompe bruscamente il boot e distrugge tutto l'armamento parziale configurato fino ad ora
                        // TODO: CorePool_ShutdownAndClear();
                        // TODO: return status;
                    }
                }
                    break;
                case THREAD_TYPE_GENERIC_TASK: {
                    // TODO
                }
                    break;

                default: continue;
            }

        }
    }
    return status;
}


VOID CorePool_ShutdownAndClear() {
    g_PoolRegistry.IsRegistryActive = 0;
    for (ULONG i = 0; i < g_PoolRegistry.TotalAllocatedSlots; ++i) {
        // No need to release the memory associated with the IPC communication queue, as everything is
        // statically allocated. Just send the signal to stop the threads.
        auto* slot = &g_PoolRegistry.ThreadPool[i];
        slot->actualThread.Stop();
    }
    for (int i = 0; i < MALAMORE_MAX_DEVICES; i++) {
        if (g_PoolRegistry.Devices[i].InUse == 1) {
            MalamoreDevicePool_CleanDeviceByIndex(i);
        }
    }
    g_PoolRegistry.TotalAllocatedSlots = 0;
    g_PoolRegistry.CurrentHwThreadsCount = 0;
    g_PoolRegistry.CurrentGenericTasksCount = 0;
}

extern "C"
void MalamoreDevicePool_Initialize(void) {
    for (int i = 0; i < MALAMORE_MAX_DEVICES; i++) {
        RtlZeroMemory(g_PoolRegistry.Devices[i].DevicePath, MALAMORE_PATH_MAX * sizeof(WCHAR));
        g_PoolRegistry.Devices[i].InUse = 0;
    }
}

extern "C"
int MalamoreDevicePool_AddDevicePath(const WCHAR* devicePath, BOOL asynch) {
    if (devicePath == NULL || devicePath[0] == L'\0') {
        return -1;
    }

    // 1. Cerca se il percorso esiste già nell'array statico
    for (int i = 0; i < MALAMORE_MAX_DEVICES; i++) {
        if (g_PoolRegistry.Devices[i].InUse == 1 && wcscmp(g_PoolRegistry.Devices[i].DevicePath, devicePath) == 0) {
            return i; // Trovato, restituisce l'indice esistente
        }
    }

    // 2. Se non esiste ed il manager ha ancora spazio, occupa il primo slot libero
    for (int i = 0; i < MALAMORE_MAX_DEVICES; i++) {
        if (g_PoolRegistry.Devices[i].InUse == 0) {
            RtlStringCchCopyW(g_PoolRegistry.Devices[i].DevicePath, MALAMORE_PATH_MAX, devicePath);

            // Tentativo di apertura asincrona tramite il layer di compatibilità unificato
            auto flags = StreamFlags::Read | StreamFlags::Write;
            if (asynch) {
                flags |= StreamFlags::ASync;
            }
            NTSTATUS status = g_PoolRegistry.Devices[i].Stream.Open( devicePath, flags);

            if (NT_SUCCESS(status)) {
                g_PoolRegistry.Devices[i].InUse = 1;
                return i; // Apertura riuscita, restituisce l'indice
            }

            g_PoolRegistry.Devices[i].Stream.Close();
            g_PoolRegistry.Devices[i].InUse = 0 ;
            return -1;
        }
    }

    return -1; // Pool esaurito
}

extern "C"
void MalamoreDevicePool_CleanDeviceByPath(const WCHAR* devicePath) {
    if (devicePath == NULL || devicePath[0] == L'\0') {
        return;
    }

    for (int i = 0; i < MALAMORE_MAX_DEVICES; i++) {
        if (g_PoolRegistry.Devices[i].InUse == 1 && wcscmp(g_PoolRegistry.Devices[i].DevicePath, devicePath) == 0) {
            // Chiude il descrittore, pulisce le stringhe e azzera lo stato della cella
            g_PoolRegistry.Devices[i].Stream.Close();
            g_PoolRegistry.Devices[i].InUse = 0;
            return;
        }
    }
}

extern "C"
void MalamoreDevicePool_CleanDeviceByIndex(int index) {
    if (index < 0 || index >= MALAMORE_MAX_DEVICES) {
        return;
    }

    if (g_PoolRegistry.Devices[index].InUse == 1) {
        // Se l'indice è valido ed è aperto, invoca la close logica e libera l'allocazione
        g_PoolRegistry.Devices[index].Stream.Close();
        g_PoolRegistry.Devices[index].InUse = 0;
    }
}

extern "C"
MalamoreIStream* MalamoreDevicePool_GetStream(int index) {
    if (index < 0 || index >= MALAMORE_MAX_DEVICES) {
        return NULL;
    }
    if (g_PoolRegistry.Devices[index].InUse == 0) {
        return NULL;
    }
    return &g_PoolRegistry.Devices[index].Stream;
}