#include "malamore/service_manager.h"
#include "malamore/pool_core.h"
#include "malamore/task_registry.h"
#include <ntstrsafe.h>

LIST_ENTRY g_ListaServizi;
ERESOURCE g_ListaLock;

 PFN_EXECUTE_ROUTINE_IO CercaTaskInTabella(PCSTR NomeTask) {
    for (ULONG i = 0; i < TOTALE_TASK_REGISTRATI; i++) {
        if (RtlCompareMemory(g_TaskTable[i].NomeTask,
                             NomeTask,
                             MAX_SERVICE_NAME_LEN) == MAX_SERVICE_NAME_LEN) {
            return g_TaskTable[i].FunctionPointer;
        }
    }
    return NULL;
}

// extern "C" VOID ScriviRecordSuFileSink(PCONTESTO_SERVIZIO contesto, PLONG datiDaSalvare) {
//     PCODA_CIRCOLARE coda = contesto->CodaFile;
//     LONG attualeHead = coda->Head;
//     LONG prossimaHead = (attualeHead + 1) % CODA_MAX_MESSAGGI;
//     if (prossimaHead != coda->Tail) {
//         coda->Messaggi[attualeHead].SequenceId = attualeHead;
//         RtlCopyMemory(coda->Messaggi[attualeHead].Dati, datiDaSalvare, sizeof(LONG) * PARAM_COUNT);
//         InterlockedExchange(&coda->Head, prossimaHead);
//         KeSetEvent(&contesto->CodaDatiEvent, IO_NO_INCREMENT, FALSE);
//     }
// }

// =========================================================================
// SUTTOSISTEMA ASINCRONO DISK WRITER (SINK DI TELEMETRIA)
// =========================================================================



// VOID InizializzaManagerServizi(VOID) {
//     InitializeListHead(&g_ListaServizi);
//     ExInitializeResourceLite(&g_ListaLock);
// }
//
// VOID DeinizializzaManagerServizi(VOID) {
//     ExDeleteResourceLite(&g_ListaLock);
// }

// NTSTATUS AvviaNuovoServizio(PSERVIZIO_CONFIG_INPUT Input) {
//     NTSTATUS status; ULONG coreA = 0, coreB = 0;
//
//     // Cerca l'unico task richiesto dal client
//     PFN_KERNEL_TASK taskCodice = CercaTaskInTabella(Input->NomeTask);
//     if (!taskCodice) return STATUS_NOT_FOUND;
//
//     if (!AllocaCoppiaCore(&coreA, &coreB)) return STATUS_INSUFFICIENT_RESOURCES;
//
//     LARGE_INTEGER freq; KeQueryPerformanceCounter(&freq);
//     PCONTESTO_SERVIZIO contesto = (PCONTESTO_SERVIZIO)ExAllocatePoolWithTag(NonPagedPool, sizeof(CONTESTO_SERVIZIO), 'SrvC');
//     if (!contesto) { LiberaCoppiaCore(coreA, coreB); return STATUS_INSUFFICIENT_RESOURCES; }
//     RtlZeroMemory(contesto, sizeof(CONTESTO_SERVIZIO));
//
//     contesto->ServizioId = Input->ServizioId;
//     contesto->AllocazioneCoreA = coreA; contesto->AllocazioneCoreB = coreB;
//     contesto->TaskCodice = taskCodice; contesto->TimerFrequency = freq;
//     KeInitializeSpinLock(&contesto->LockDati);
//
//     contesto->CodaFile = (PCODA_CIRCOLARE)ExAllocatePoolWithTag(NonPagedPool, sizeof(CODA_CIRCOLARE), 'CodF');
//     if (!contesto->CodaFile) { LiberaCoppiaCore(coreA, coreB); ExFreePoolWithTag(contesto, 'SrvC'); return STATUS_INSUFFICIENT_RESOURCES; }
//     RtlZeroMemory(contesto->CodaFile, sizeof(CODA_CIRCOLARE));
//
//     OBJECT_ATTRIBUTES objAttributes; UNICODE_STRING filePath; WCHAR pathBuffer[256];
//     RtlStringCchPrintfW(pathBuffer, 256, L"\\SystemRoot\\Temp\\Servizio_%u.dat", Input->ServizioId);
//     RtlInitUnicodeString(&filePath, pathBuffer);
//     InitializeObjectAttributes(&objAttributes, &filePath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
//     IO_STATUS_BLOCK ioStatus;
//
//     status = ZwCreateFile(&contesto->FileHandle, FILE_APPEND_DATA | SYNCHRONIZE, &objAttributes, &ioStatus, NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_SUPERSEDE, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
//     if (!NT_SUCCESS(status)) { ExFreePoolWithTag(contesto->CodaFile, 'CodF'); LiberaCoppiaCore(coreA, coreB); ExFreePoolWithTag(contesto, 'SrvC'); return status; }
//
//     KeInitializeEvent(&contesto->CodaDatiEvent, NotificationEvent, FALSE);
//     PsCreateSystemThread(&contesto->DiskWriterThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, DiskWriterThreadRoutine, contesto);
//
//     LONGLONG periodTicks = (LONGLONG)((Input->PeriodoTaskMs * freq.QuadPart) / 1000);
//     LONGLONG phaseOffsetBTicks = (LONGLONG)((Input->DurataMediaTaskUs * freq.QuadPart) / 1000000);
//
//     // Duplicazione del singolo Task sul Core A (Offset 0)
//     PTHREAD_CONFIG configA = (PTHREAD_CONFIG)ExAllocatePoolWithTag(NonPagedPool, sizeof(THREAD_CONFIG), 'ThCf');
//     if (configA) {
//         RtlZeroMemory(configA, sizeof(THREAD_CONFIG));
//         configA->Affinity.Group = 0; configA->Affinity.Mask = ((KAFFINITY)1 << coreA);
//         configA->PeriodTicks = periodTicks; configA->PhaseOffsetTicks = 0;
//         configA->ContestoServizio = contesto;
//         PsCreateSystemThread(&contesto->ThreadHandleA, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, configA);
//     }
//
//     // Duplicazione dello STESSO Task sul Core B (Offset temporale protettivo)
//     PTHREAD_CONFIG configB = (PTHREAD_CONFIG)ExAllocatePoolWithTag(NonPagedPool, sizeof(THREAD_CONFIG), 'ThCf');
//     if (configB) {
//         RtlZeroMemory(configB, sizeof(THREAD_CONFIG));
//         configB->Affinity.Group = 0; configB->Affinity.Mask = ((KAFFINITY)1 << coreB);
//         configB->PeriodTicks = periodTicks; configB->PhaseOffsetTicks = phaseOffsetBTicks;
//         configB->ContestoServizio = contesto;
//         PsCreateSystemThread(&contesto->ThreadHandleB, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, configB);
//     }
//
//     KeEnterCriticalRegion(); ExAcquireResourceExclusiveLite(&g_ListaLock, TRUE);
//     InsertTailList(&g_ListaServizi, &contesto->Entry);
//     ExReleaseResourceLite(&g_ListaLock); KeLeaveCriticalRegion();
//     return STATUS_SUCCESS;
// }
//
// NTSTATUS AggiornaParametriServizio(ULONG ServizioId, PLONG NuoviParametri) {
//     NTSTATUS status = STATUS_NOT_FOUND;
//     KeEnterCriticalRegion(); ExAcquireResourceSharedLite(&g_ListaLock, TRUE);
//     PLIST_ENTRY entry = g_ListaServizi.Flink;
//     while (entry != &g_ListaServizi) {
//         PCONTESTO_SERVIZIO contesto = CONTAINING_RECORD(entry, CONTESTO_SERVIZIO, Entry);
//         if (contesto->ServizioId == ServizioId) {
//             KIRQL oldIrql;
//             KeAcquireSpinLock(&contesto->LockDati, &oldIrql);
//             RtlCopyMemory(contesto->BufferParametri, NuoviParametri, sizeof(LONG) * PARAM_COUNT);
//             KeReleaseSpinLock(&contesto->LockDati, oldIrql);
//             status = STATUS_SUCCESS; break;
//         }
//         entry = entry->Flink;
//     }
//     ExReleaseResourceLite(&g_ListaLock); KeLeaveCriticalRegion();
//     return status;
// }
//
// NTSTATUS FermaServizio(ULONG ServizioId) {
//     KeEnterCriticalRegion(); ExAcquireResourceExclusiveLite(&g_ListaLock, TRUE);
//     PLIST_ENTRY entry = g_ListaServizi.Flink;
//     while (entry != &g_ListaServizi) {
//         PCONTESTO_SERVIZIO contesto = CONTAINING_RECORD(entry, CONTESTO_SERVIZIO, Entry);
//         if (contesto->ServizioId == ServizioId) {
//             contesto->TerminateThreads = TRUE;
//             KeSetEvent(&contesto->CodaDatiEvent, IO_NO_INCREMENT, FALSE);
//             if (contesto->ThreadHandleA) ZwClose(contesto->ThreadHandleA);
//             if (contesto->ThreadHandleB) ZwClose(contesto->ThreadHandleB);
//             if (contesto->DiskWriterThreadHandle) ZwClose(contesto->DiskWriterThreadHandle);
//             if (contesto->FileHandle) ZwClose(contesto->FileHandle);
//
//             LiberaCoppiaCore(contesto->AllocazioneCoreA, contesto->AllocazioneCoreB);
//             RemoveEntryList(entry);
//             ExFreePoolWithTag(contesto->CodaFile, 'CodF'); ExFreePoolWithTag(contesto, 'SrvC');
//             break;
//         }
//         entry = entry->Flink;
//     }
//     ExReleaseResourceLite(&g_ListaLock); KeLeaveCriticalRegion();
//     return STATUS_SUCCESS;
// }
