//
// Created by Giacomo Bergami on 15/09/2026.
//

#include <malamore/service_manager.h>
#include <malamore/threads/thread_pool.h>
// #include <ntstrsafe.h> // <--- NUOVO: Routine di manipolazione stringhe sicure nativamente in Ring 0

#include "malamore/utility/Path.h"
#include "malamore/utility/telemetry_formatter.h"
#include "malamore/utility/streambuf/MalamoreOStream.h"

// Allocazione statica contigua nel segmento dati (Zero heap runtime - Specifica 4)
static CorePoolRegistry g_PoolRegistry;
static MalamorePath     telemetryFilePath{L"\\SystemRoot\\Malamore_Telemetry_Sink.bin"};
static MalamoreOStream  telemetryFile;

NTSTATUS CorePool_RouteMessageSrcToDest(LONG Timestamp, ULONG SourceNodeId, ULONG targetNodeId,  const UCHAR* PayloadData ,ULONG DataLength) {
    ULONG channelIndex = Malamore::Math::WrapIndex<MAX_GENERIC_TASKS>(targetNodeId);
    InterProcessChannelData* pChannel = &g_PoolRegistry.IpcChannels[channelIndex];

    if (RealTimeIPC_Push(pChannel, Timestamp, SourceNodeId, PayloadData, DataLength)) {
        return STATUS_SUCCESS;
    }

    KdPrint(("[CorePool_RegisterGenericTask] It seems that the queue is full, and you cannot push another message \n"));
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS CorePool_RouteMessageToDest(ULONG sourceNodeId, ULONG targetNodeId, const struct DataChannelParcel* packet) {
    ULONG channelIndex = Malamore::Math::WrapIndex<MAX_GENERIC_TASKS>(targetNodeId);
    InterProcessChannelData* pChannel = &g_PoolRegistry.IpcChannels[channelIndex];


    if (RealTimeIPC_Push(pChannel, packet)) {
        return STATUS_SUCCESS;
    }

    KdPrint(("[CorePool_RegisterGenericTask] It seems that the queue is full, and you cannot push another message \n"));
    return STATUS_INSUFFICIENT_RESOURCES;
}
#if INCLUDE_UNTESTED_FEATURES

NTSTATUS CorePool_InitializeRegistry() {
    NTSTATUS status;

    // Sostituito RtlZeroMemory al posto di macro non standard
    RtlZeroMemory(&g_PoolRegistry, sizeof(CorePoolRegistry));

    // =========================================================================
    // STEP 1: APERTURA O CREAZIONE SICURA DEL FILE SINK SUL DISCO DI WINDOWS
    // =========================================================================
    // UNICODE_STRING fileName;
    // RtlInitUnicodeString(&fileName, L"\\SystemRoot\\Malamore_Telemetry_Sink.bin");
    // OBJECT_ATTRIBUTES objAttr;
    // InitializeObjectAttributes(&objAttr, &fileName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    //
    // // Esegue l'apertura sincrona ad accesso esclusivo sul file system di Ring 0
    // IO_STATUS_BLOCK ioStatus;
    // status = ZwCreateFile(
    //     &g_PoolRegistry.hFileHandle,
    //     FILE_APPEND_DATA | SYNCHRONIZE,
    //     &objAttr,
    //     &ioStatus,
    //     NULL,                                       // AllocationSize
    //     FILE_ATTRIBUTE_NORMAL,                      // FileAttributes
    //     0,                                          // ShareAccess (0 = Esclusivo)
    //     FILE_SUPERSEDE,                             // CreateDisposition (Sovrascrive o crea da zero)
    //     FILE_SYNCHRONOUS_IO_NONALERT,               // CreateOptions
    //     NULL, 0                                     // EaBuffer, EaLength
    // );
    //
    // if (!NT_SUCCESS(status)) {
    //     g_PoolRegistry.hFileHandle = NULL;
    //     KdPrint(("[MALAMORE DRIVER] Cannot create the C:\\Windows\\Malamore_Telemetry_Sink.bin file. Quitting initialization \n"));
    //     return STATUS_INSUFFICIENT_RESOURCES; // Fallimento catastrofico dell'I/O di tracciamento
    // }
    telemetryFile.Open(telemetryFilePath, StreamDisposition::OpenOrCreate);

    RealTimeIPC_Init(&g_PoolRegistry.TelemetryChannel);
    // g_PoolRegistry.TelemetryChannel.Init();

    for (ULONG i = 0; i < MAX_GENERIC_TASKS; ++i) {
        KdPrint(("[MALAMORE DRIVER] Init IPC  Channel #%i\n", i));
        // g_PoolRegistry.IpcChannels[i].Init();
        RealTimeIPC_Init(&g_PoolRegistry.IpcChannels[i]);
    }


    // Configurazione dei contatori iniziali di controllo per la flotta omogenea
    g_PoolRegistry.CurrentHwThreadsCount = 0;
    g_PoolRegistry.CurrentGenericTasksCount = 0;
    g_PoolRegistry.TotalAllocatedSlots = 0;

    // Armiamo lo stato del registro e resettiamo il flag di arresto della telemetria
    g_PoolRegistry.IsRegistryActive = TRUE; // TRUE
    g_PoolRegistry.TerminateDiskThread = 0;

    // =========================================================================
    // STEP 4: AVVIO DEL THREAD PARALLELO A PRIORITÀ PASSIVA DEL DISK WRITER
    // =========================================================================
    // Crea il System Thread di background vincolato al processo protetto "System"
    KdPrint(("[MALAMORE DRIVER] Creating telemetry thread\n"));
    status = PsCreateSystemThread(
        &g_PoolRegistry.hDiskThread,     // _Out_ PHANDLE ThreadHandle
        THREAD_ALL_ACCESS,               // _In_ ACCESS_MASK DesiredAccess
        NULL,                            // _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes
        NULL,                            // _In_opt_ HANDLE ProcessHandle (NULL = System Process)
        NULL,                            // _Out_opt_ PCLIENT_ID ClientId
        DiskWriterThreadRoutine,         // _In_ PKSTART_ROUTINE StartRoutine
        NULL                             // _In_opt_ PVOID StartContext
    );

    if (!NT_SUCCESS(status)) {
        // Se lo spawn fallisce, esegue il rollback chiudendo l'handle del file aperto
        ZwClose(g_PoolRegistry.hFileHandle);
        g_PoolRegistry.hFileHandle = NULL;
        g_PoolRegistry.hDiskThread = NULL;
        g_PoolRegistry.IsRegistryActive = FALSE;
        KdPrint(("[MALAMORE DRIVER] Not enough resources to create the telemetry thread\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Ritorna il successo: l'infrastruttura statica è pronta a ricevere le registrazioni dei nodi
    return STATUS_SUCCESS;
}





// Dichiarazione esterna delle funzioni dei System Thread del driver per il linkage
extern "C" VOID HardwareReaderThreadRoutine(_In_ PVOID Context);
extern "C" VOID CorePollingRoutineGeneric(_In_ PVOID Context);

// Dichiarazione esterna della routine a priorità passiva del Disk Writer
extern "C" VOID DiskWriterThreadRoutine(_In_ PVOID Context);


/***
 *
 *
*Forcing Release Behavior ManuallyIf you want to ensure KdPrint is entirely disabled regardless of the build environment,
*verify that DBG is not defined in your preprocessor settings. You can also explicitly undefine it in your main header
*wrapper:c #undef DBG
// Now any subsequent include of wdm.h or ntddk.h
// will evaluate KdPrint(...) to an empty statement.
*/



// SPECIFICA 2: Metodo dedicato all'inizializzazione del thread Hardware
NTSTATUS CorePool_RegisterHwReader(
    ULONG64 processId,
    const WCHAR* devicePath,
    ULONG readTimeoutUs,
    ULONG samplingPeriodUs,
    KAFFINITY coreAffinityMask,
    ULONG targetTaskId
) {
    if (g_PoolRegistry.IsRegistryActive == 0) {
        KdPrint(("[CorePool_RegisterHwReader] Inactive registry, cannot continue with registering an HW reader \n"));
        return STATUS_INVALID_DEVICE_STATE;
    }

    // 3. SELEZIONE AUTOMATICA: Trova il primo slot omogeneo disponibile nel pool statico
    struct MalamoreThreadSlot* slot = nullptr;
    for (ULONG i = 0; i < MAX_TOTAL_SLOTS; ++i) {
        // Uno slot è libero se il suo tipo è impostato a THREAD_TYPE_FREE
        if (g_PoolRegistry.ThreadPool[i].Type == THREAD_TYPE_FREE) {
            slot = &g_PoolRegistry.ThreadPool[i];
            break;
        }
    }

    // Se l'array statico unificato pre-allocato è saturo, respinge la richiesta per proteggere il sistema
    if (!slot) {
        KdPrint(("[CorePool_RegisterGenericTask] It seems that all threads in the pool are locked. I cannot do anything more. \n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    slot->Type = THREAD_TYPE_HW_READER;
    slot->BaseAffinityMask = coreAffinityMask;

    struct UnifiedThreadContext* ctx = &slot->ContextA;
    ctx->ProcessId = processId;
    ctx->PeriodUs = samplingPeriodUs;
    ctx->MaxBudgetUs = readTimeoutUs;
    ctx->AffinityMask = slot->BaseAffinityMask;
    ctx->TargetThreadId = targetTaskId;

    // CORREZIONE HARDWARE: Utilizziamo la funzione sicura del kernel per copiare la stringa WCHAR
    // Sostituisce wcscpy_s dello spazio utente
    NTSTATUS strStatus = RtlStringCchCopyW(ctx->DevicePath, MAX_PATH, devicePath);
    if (!NT_SUCCESS(strStatus)) {
        KdPrint(("[CorePool_RegisterHwReader] Cannot copy the devicePath to ctx->DevicePath \n"));
        return strStatus;
    }

    ctx->KeepRunning = 0;
    ctx->PhaseOffsetTicks = 0;

    g_PoolRegistry.CurrentHwThreadsCount++;
    g_PoolRegistry.TotalAllocatedSlots++;
    return STATUS_SUCCESS;
}


// SPECIFICA 2: Metodo dedicato all'inizializzazione del Generic Task (Tandem)
NTSTATUS CorePool_RegisterGenericTask(
    ULONG64 processId,
    const char* taskName,
    ULONG executionPeriodUs,
    ULONG estimatedWcetUs,
    KAFFINITY coreAffinityMask,
    ULONG targetNodeId,
    BOOLEAN runRead,
    BOOLEAN runWrite,
    BOOLEAN saveDisk,
    PFN_EXECUTE_ROUTINE_IO executionFunction
) {
    if (g_PoolRegistry.IsRegistryActive == 0) {
        KdPrint(("[CorePool_RegisterGenericTask] Inactive registry, cannot continue with registering a generic task \n"));
        return STATUS_INVALID_DEVICE_STATE;
    }

    // 2. TRADUZIONE DEL PUNTATORE A FUNZIONE DAL REGISTRO COMPITI (DIZIONARIO RING 0)
    // Il client passa solo la stringa; il driver risolve l'indirizzo kernel reale
    PFN_EXECUTE_ROUTINE_IO realTaskFunction = CercaTaskInTabella(taskName);
    if (!realTaskFunction) {
        return STATUS_NOT_FOUND; // Il task richiesto dal client non è censito nel driver
    }

    // 3. SELEZIONE AUTOMATICA: Trova il primo slot omogeneo disponibile nel pool statico
    struct MalamoreThreadSlot* targetSlot = nullptr;
    for (ULONG i = 0; i < MAX_TOTAL_SLOTS; ++i) {
        // Uno slot è libero se il suo tipo è impostato a THREAD_TYPE_FREE
        if (g_PoolRegistry.ThreadPool[i].Type == THREAD_TYPE_FREE) {
            targetSlot = &g_PoolRegistry.ThreadPool[i];
            break;
        }
    }

    // Se l'array statico unificato pre-allocato è saturo, respinge la richiesta per proteggere il sistema
    if (!targetSlot) {
        KdPrint(("[CorePool_RegisterGenericTask] It seems that all threads in the pool are locked. I cannot do anything more. \n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    targetSlot->Type = THREAD_TYPE_GENERIC_TASK;
    targetSlot->BaseAffinityMask = coreAffinityMask;

    // CORREZIONE HARDWARE: Copia sicura ANSI string per il nome del compito nel kernel
    // Sostituisce la funzione strcpy_s dello spazio utente
    // NTSTATUS strStatus = RtlStringCchCopyA(slot->ServiceContext.TaskName, 64, taskName);
    // if (!NT_SUCCESS(strStatus)) return strStatus;
    // slot->ServiceContext.SourceNodeId = slotIndex;

    // Configurazione simmetrica del Tandem tramite la struttura dati unificata
    struct UnifiedThreadContext* ctxA = &targetSlot->ContextA;
    ctxA->ProcessId = processId;
    ctxA->PeriodUs = executionPeriodUs;
    ctxA->MaxBudgetUs = estimatedWcetUs;
    ctxA->AffinityMask = coreAffinityMask;
    ctxA->TaskFunctionPointer = (PFN_EXECUTE_ROUTINE_IO)executionFunction;
    ctxA->IsHighPriority = 1;
    ctxA->runReadOperation = runRead;
    ctxA->runWriteOperation = runWrite;
    ctxA->saveFileToDisk = saveDisk;

    struct UnifiedThreadContext* ctxB = &targetSlot->ContextB;
    ctxB->ProcessId = processId;
    ctxB->PeriodUs = executionPeriodUs;
    ctxB->MaxBudgetUs = estimatedWcetUs;
    ctxB->AffinityMask = coreAffinityMask;
    ctxB->TaskFunctionPointer = (PFN_EXECUTE_ROUTINE_IO)executionFunction;
    ctxB->IsHighPriority = 0;
    ctxB->runReadOperation = runRead;
    ctxB->runWriteOperation = runWrite;
    ctxB->saveFileToDisk = saveDisk;

    g_PoolRegistry.CurrentGenericTasksCount++;
    g_PoolRegistry.TotalAllocatedSlots++;
    return STATUS_SUCCESS;
}

NTSTATUS CorePool_RouteMessageToDest(ULONG sourceNodeId, ULONG targetNodeId, const struct DataChannelParcel* packet) {
    ULONG channelIndex = targetNodeId % MAX_GENERIC_TASKS;
    InterProcessChannelData* pChannel = &g_PoolRegistry.IpcChannels[channelIndex];


    if (RealTimeIPC_Push(pChannel, packet)) {
        return STATUS_SUCCESS;
    }

    KdPrint(("[CorePool_RegisterGenericTask] It seems that the queue is full, and you cannot push another message \n"));
    return STATUS_INSUFFICIENT_RESOURCES;
}

/**
 * INNESCO SIMULTANEO DELLA TIMELINE HARDWARE CON CONTROLLO RIGIDO E ROLLBACK A CASCATA
 */
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

    // Scansione sequenziale del pool omogeneo statico
    for (ULONG i = 0; i < g_PoolRegistry.TotalAllocatedSlots; ++i) {
        struct MalamoreThreadSlot* slot = &g_PoolRegistry.ThreadPool[i];

        // =========================================================================
        // CASO 1: INNESCO THREAD DI CATTURA HARDWARE (PASSIVE_LEVEL REAL-TIME)
        // =========================================================================
        if (slot->Type == THREAD_TYPE_HW_READER) {
            struct UnifiedThreadContext* ctx = &slot->ContextA;
            ctx->SynchronizedStartTicks = synchronizedStart;
            ctx->PerfCounterFrequency = freq.QuadPart;
            ctx->KeepRunning = 1;

            // Tentativo di spawn del System Thread
            status = PsCreateSystemThread(&slot->hThreadHandleA, THREAD_ALL_ACCESS, NULL, NULL, NULL, HardwareReaderThreadRoutine, ctx);

            // VERIFICA RIGIDA DI INTEGRITÀ DI SPATIAL SPAWN
            if (!NT_SUCCESS(status)) {
                KdPrint(("[MALAMORE 🚨] Fallimento spawn Thread HW allo slot %lu. Status: 0x%X. Avvio Rollback...\n", i, status));
                ctx->KeepRunning = 0;
                slot->hThreadHandleA = NULL;

                // Interrompe bruscamente il boot e distrugge tutto l'armamento parziale configurato fino ad ora
                CorePool_ShutdownAndClear();
                return status;
            }
        }
        // =========================================================================
        // CASO 2: INNESCO THREAD COMPUTAZIONALI DISTRIBUITI (TANDEM A DISPATCH_LEVEL)
        // =========================================================================
        else if (slot->Type == THREAD_TYPE_GENERIC_TASK) {
            // Conversione del periodo da microseconds a Ticks hardware basata sulla costante comune
            ULONG64 periodTicks = (static_cast<ULONG64>(slot->ContextA.PeriodUs) * freq.QuadPart) / 1000000W64;

            // Armamento atomico dei parametri del Task A (Innesco in fase zero)
            slot->ContextA.SynchronizedStartTicks = synchronizedStart;
            slot->ContextA.PerfCounterFrequency = freq.QuadPart;
            slot->ContextA.PhaseOffsetTicks = 0;
            slot->ContextA.KeepRunning = 1;

            // Armamento atomico dei parametri del Task B (Innesco in opposizione geometrica T/2)
            slot->ContextB.SynchronizedStartTicks = synchronizedStart;
            slot->ContextB.PerfCounterFrequency = freq.QuadPart;
            slot->ContextB.PhaseOffsetTicks = periodTicks / 2;
            slot->ContextB.KeepRunning = 1;

            // 1. Tentativo di spawn del Task A del Tandem
            status = PsCreateSystemThread(&slot->hThreadHandleA, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, &slot->ContextA);
            if (!NT_SUCCESS(status)) {
                KdPrint(("[MALAMORE 🚨] Fallimento spawn TandemA allo slot %lu. Status: 0x%X. Avvio Rollback...\n", i, status));
                slot->ContextA.KeepRunning = 0;
                slot->ContextB.KeepRunning = 0;
                slot->hThreadHandleA = NULL;

                CorePool_ShutdownAndClear();
                return status;
            }

            // 2. Tentativo di spawn del Task B del Tandem (Il compagno speculare in Yielding)
            status = PsCreateSystemThread(&slot->hThreadHandleB, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, &slot->ContextB);
            if (!NT_SUCCESS(status)) {
                KdPrint(("[MALAMORE 🚨] Fallimento spawn TandemB allo slot %lu. Status: 0x%X. Avvio Rollback...\n", i, status));
                // Disarmo immediato del Task A che era appena nato ma non è ancora uscito dalla barriera oraria
                InterlockedExchange(&slot->ContextA.KeepRunning, 0);
                slot->ContextB.KeepRunning = 0;
                slot->hThreadHandleB = NULL;

                CorePool_ShutdownAndClear();
                return status;
            }
        }
    }

    // Se l'intero ciclo si chiude senza interruzioni, la flotta è in camera di sosta sui core, pronta allo scatto
    KdPrint(("[MALAMORE] Tutti i System Thread caricati correttamente sul perimetro hardware. In attesa del tick zero...\n"));
    return STATUS_SUCCESS;
}

#ifndef MAX_LOG_LINE_SIZE
#define MAX_LOG_LINE_SIZE       (4080)
#endif

VOID DiskWriterThreadRoutine(_In_ PVOID Context) {
    UNREFERENCED_PARAMETER(Context);
    // TelemetryQueue* queue = g_PoolRegistry.LogQueue;
    IO_STATUS_BLOCK ioStatus;
    NTSTATUS status;

    // Allocazione fissa sullo stack del thread (Massima velocità a DISPATCH/PASSIVE, no heap)
    char timeString[80];
    char logLine[MAX_LOG_LINE_SIZE];
    RtlZeroMemory(timeString, sizeof(timeString));
    RtlZeroMemory(logLine, sizeof(logLine));

    // Il thread gira fiso a PASSIVE_LEVEL: può effettuare I/O sincrono su file senza bloccare i core
    while (!InterlockedOr(&g_PoolRegistry.TerminateDiskThread, 0) || (!RealTimeIPC_IsEmpty(&g_PoolRegistry.TelemetryChannel))) {
        if (RealTimeIPC_IsEmpty(&g_PoolRegistry.TelemetryChannel)) {
            // Se la coda è vuota, rilascia temporaneamente la CPU con un piccolo stallo preventivo
            LARGE_INTEGER interval;
            interval.QuadPart = -100 * 1000; // 10 millisecondi di sleep relativo
            KeDelayExecutionThread(KernelMode, FALSE, &interval);
            continue;
        }

        struct DataChannelParcel* msg = RealTimeIPC_Top(&g_PoolRegistry.TelemetryChannel);

        // 1. Formatta il timestamp nella stringa cronologica richiesta
        status = FormatKernelTimestamp(msg->Timestamp, timeString, sizeof(timeString));
        if (!NT_SUCCESS(status)) {
            KdPrint(("[MALAMORE 🚨] Error printing the time string\n"));
            InterlockedOr(&g_PoolRegistry.TerminateDiskThread, 1);
        } else {
            // 2. DISCRIMINAZIONE DEL PAYLOAD: VERIFICA SE STAMPABILE O BINARIO
            BOOLEAN isPrintable = IsBufferPrintable(msg->PayloadData, msg->DataLength);
            if (isPrintable) {
                // CASO A: Contenuto stampabile -> Formato testo quotato con salto riga
                // Intestazione fissa dell'output: [Node=X, Time=YYYY-MM-DD HH:MM:SS.NNNNNNNNN] "content"\n
                status = RtlStringCchPrintfA(
                    logLine, MAX_LOG_LINE_SIZE,
                    "[Node=%lu, Time=%s] \"%.*s\"\r\n",
                    msg->SourceNodeId, timeString, msg->PayloadData
                );
            } else {
                // CASO B: Contenuto binario/non stampabile -> Rappresentazione Esadecimale (Hex Number)
                // Prepariamo l'intestazione fissa iniziale della stringa di log
                status = RtlStringCchPrintfA(
                    logLine, MAX_LOG_LINE_SIZE,
                    "[Node=%lu, Time=%s] ",
                    msg->SourceNodeId, timeString
                );
            }
            if (!NT_SUCCESS(status)) {
                KdPrint(("[MALAMORE 🚨] Error printing the info part string\n"));
                InterlockedOr(&g_PoolRegistry.TerminateDiskThread, 1);
            } else {
                if (!isPrintable) {
                    // Calcoliamo lo spazio rimanente nella stringa dopo l'intestazione fissa
                    size_t currentLength = 0;
                    RtlStringCchLengthA(logLine, MAX_LOG_LINE_SIZE, &currentLength);

                    char* hexAppendPtr = logLine + currentLength;
                    size_t remainingCch = MAX_LOG_LINE_SIZE - currentLength;

                    // Appendiamo sequenzialmente la rappresentazione esadecimale di ogni singolo byte
                    for (ULONG i = 0; i < MAX_LOG_LINE_SIZE && remainingCch > 4; ++i) {
                        status = RtlStringCchPrintfA(hexAppendPtr, remainingCch, "%02X", buffer[i]);
                        if (!NT_SUCCESS(status)) {
                            KdPrint(("[MALAMORE 🚨] Error appending the hexadecimal part\n"));
                            break;
                        }

                        hexAppendPtr += 2;
                        remainingCch -= 2;
                    }
                    if (!NT_SUCCESS(status)) {
                        continue;
                    }

                    // Aggiungiamo la terminazione del ritorno a capo richiesto per lo stream del file log
                    status = RtlStringCchPrintfA(hexAppendPtr, remainingCch, "\r\n");
                    if (!NT_SUCCESS(status)) {
                        KdPrint(("[MALAMORE 🚨] Cannot append the terminating string, continuing\n"));
                        continue;
                    }
                }
                if (g_PoolRegistry.hFileHandle != NULL) {
                    // 3. SCRITTURA EFFETTIVA E LINEARE SUL FILE SINK DI RING 0
                    size_t finalLineLength = 0;
                    status = RtlStringCchLengthA(logLine, MAX_LOG_LINE_SIZE, &finalLineLength);
                    if (!NT_SUCCESS(status)) {
                        KdPrint(("[MALAMORE 🚨] Cannot determine the string lenth, for some odd reason, continuing\n"));
                        continue;
                    }

                    // Scrittura sincrona protetta: operando a PASSIVE_LEVEL nel DiskWriterThreadRoutine,
                    // l'I/O non genera alcuna violazione di contesto o stallo catastrofico.
                    status = ZwWriteFile(
                        hFileHandle,
                        NULL, NULL, NULL, // Event, ApcRoutine, ApcContext
                        &ioStatus,
                        logLine,          // Il buffer di testo formattato condizionalmente
                        static_cast<ULONG>(finalLineLength),
                        NULL, NULL        // ByteOffset, Key
                    );
                } else {
                    KdPrint(("[MALAMORE 🚨] The file handler for the telemetry task is cleared, I cannot write.\n"));
                    continue;
                }
            }
        }
    }

    PsTerminateSystemThread(STATUS_SUCCESS);
}

VOID CorePool_ShutdownAndClear() {
    g_PoolRegistry.IsRegistryActive = 0;

    for (ULONG i = 0; i < g_PoolRegistry.TotalAllocatedSlots; ++i) {
        struct MalamoreThreadSlot* slot = &g_PoolRegistry.ThreadPool[i];

        InterlockedExchange(&slot->ContextA.KeepRunning, 0);
        InterlockedExchange(&slot->ContextB.KeepRunning, 0);

        if (slot->hThreadHandleA) {
            PVOID obj;
            if (NT_SUCCESS(ObReferenceObjectByHandle(slot->hThreadHandleA, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &obj, NULL))) {
                KeWaitForSingleObject(obj, Executive, KernelMode, FALSE, NULL); ObDereferenceObject(obj);
            }
            ZwClose(slot->hThreadHandleA); slot->hThreadHandleA = NULL;
        }
        if (slot->hThreadHandleB) {
            PVOID obj;
            if (NT_SUCCESS(ObReferenceObjectByHandle(slot->hThreadHandleB, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &obj, NULL))) {
                KeWaitForSingleObject(obj, Executive, KernelMode, FALSE, NULL); ObDereferenceObject(obj);
            }
            ZwClose(slot->hThreadHandleB); slot->hThreadHandleB = NULL;
        }
        slot->Type = THREAD_TYPE_FREE;
    }
    g_PoolRegistry.TotalAllocatedSlots = 0;
    g_PoolRegistry.CurrentHwThreadsCount = 0;
    g_PoolRegistry.CurrentGenericTasksCount = 0;
}
#endif