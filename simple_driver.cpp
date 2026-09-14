#include <ntddk.h>
#include "malamore/ipc_manager.h"
#include "malamore/thread_generic_polling.h"

#define DEVICE_NAME_WCHAR      L"\\Device\\KernelBinderDevice"
#define SYMBOLIC_LINK_WCHAR    L"\\DosDevices\\KernelBinderLink"

// =========================================================================
// DEFINIZIONE IOCTL ARCHITETTURALI RIGIDI PER IL RING 0
// =========================================================================
#define MY_DEVICE_TYPE 0x8000UL
#define IOCTL_INVIA_PARAMETRI  CTL_CODE(MY_DEVICE_TYPE, 0x910, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AVVIA_SERVIZIO   CTL_CODE(MY_DEVICE_TYPE, 0x911, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_FERMA_SERVIZIO   CTL_CODE(MY_DEVICE_TYPE, 0x912, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Puntatori globali ai contesti allocati nel NonPagedPoolNx
static CorePollingContext* g_pTaskAContext = nullptr;
static CorePollingContext* g_pTaskBContext = nullptr;
static HANDLE g_hThreadA = NULL;
static HANDLE g_hThreadB = NULL;
static BOOLEAN g_ThreadsSpawned = FALSE;

// Dichiarazione routine driver
extern "C" DRIVER_UNLOAD DriverUnload;
extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath);
extern "C" NTSTATUS DispatchCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
extern "C" NTSTATUS DispatchIoctl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);

// Definizione del tipo per i parametri in ingresso dal client
#pragma pack(push, 1)
struct ServiceParams {
    ULONG PeriodUs;
    ULONG EstimatedWcetUs;
    KAFFINITY AffinityMask;
};
#pragma pack(pop)

void DummyTaskFunction(ULONG CoreId, MessageInputMap* InMap, MessageOutputMap* OutMap, PVOID CustomContext) {
    UNREFERENCED_PARAMETER(CoreId);
    UNREFERENCED_PARAMETER(InMap);
    UNREFERENCED_PARAMETER(OutMap);
    UNREFERENCED_PARAMETER(CustomContext);
}

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_OBJECT deviceObject = nullptr;
    UNICODE_STRING deviceName, symLink;

    DriverObject->DriverUnload = DriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchIoctl;

    RtlInitUnicodeString(&deviceName, DEVICE_NAME_WCHAR);
    status = IoCreateDevice(DriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &deviceObject);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&symLink, SYMBOLIC_LINK_WCHAR);
    status = IoCreateSymbolicLink(&symLink, &symLink);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    // Allocazione preventiva della RAM non-pagabile per i contesti
    g_pTaskAContext = (CorePollingContext*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CorePollingContext), 'TskA');
    g_pTaskBContext = (CorePollingContext*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(CorePollingContext), 'TskB');

    if (!g_pTaskAContext || !g_pTaskBContext) {
        IoDeleteSymbolicLink(&symLink);
        IoDeleteDevice(deviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(g_pTaskAContext, sizeof(CorePollingContext));
    RtlZeroMemory(g_pTaskBContext, sizeof(CorePollingContext));

    KdPrint(("[MALAMORE DRIVER] Caricato. In attesa di parametri di configurazione IPC...\n"));
    return STATUS_SUCCESS;
}

/**
 * DISPATCH IOCTL: Gestione del ciclo di vita Real-Time controllato dal Client
 */
NTSTATUS DispatchIoctl(_In_ PDEVICE_OBJECT DeviceObject, _In_ IRP *Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG bytesReturned = 0;

    ULONG ioControlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;
    PVOID buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG inLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;

    LARGE_INTEGER freq, currentTicks;
    currentTicks = KeQueryPerformanceCounter(&freq);
    // Calcolo pulito dei Tick senza ricorrere alle variabili double
    ULONG64 synchronizedStart = currentTicks.QuadPart + ((freq.QuadPart * 1000) / 1000000);
    ULONG64 periodTicks = (10000ULL * freq.QuadPart) / 1000000ULL; // Esempio: Periodo 10ms


    switch (ioControlCode) {

        // =========================================================================
        // 1. CONFIGURAZIONE DEI PARAMETRI CRITICI DI TEMPORIZZAZIONE
        // =========================================================================
        case IOCTL_INVIA_PARAMETRI: {
            if (buffer && inLength >= sizeof(ServiceParams)) {
                ServiceParams* params = (ServiceParams*)buffer;

                if (g_ThreadsSpawned) {
                    status = STATUS_DEVICE_BUSY; // Impossibile ri-configurare a servizio avviato
                    break;
                }

                // ULONG64 periodTicks = static_cast<ULONG64>(params->PeriodUs * ticksPerUs);

                // Configurazione asimmetrica dello sfasamento dei Task
                g_pTaskAContext->PeriodTicks = periodTicks;
                g_pTaskAContext->PhaseOffsetTicks = 0; // TaskA in fase zero
                g_pTaskAContext->MaxWcetUs = params->EstimatedWcetUs;
                g_pTaskAContext->PerfCounterFrequency = freq.QuadPart; // <--- PASSA LA FREQUENZA INTERA NATIVA
                g_pTaskAContext->AffinityMask = params->AffinityMask;
                g_pTaskAContext->TaskFunction = DummyTaskFunction;
                g_pTaskAContext->IsHighPriority = 1; // TaskA parte in High

                g_pTaskBContext->PeriodTicks = periodTicks;
                g_pTaskBContext->PhaseOffsetTicks = periodTicks / 2; // Sfasamento rigido T/2 per il tandem
                g_pTaskBContext->MaxWcetUs = params->EstimatedWcetUs;
                g_pTaskBContext->PerfCounterFrequency = freq.QuadPart; // <--- PASSA LA FREQUENZA INTERA NATIVA
                g_pTaskBContext->AffinityMask = params->AffinityMask;
                g_pTaskBContext->TaskFunction = DummyTaskFunction;
                g_pTaskBContext->IsHighPriority = 0; // TaskB parte in Low (Attesa attiva)

                KdPrint(("[MALAMORE] Parametri acquisiti. Periodo: %lu us, Core Mask: 0x%p\n", params->PeriodUs, (void*)params->AffinityMask));
                status = STATUS_SUCCESS;
            } else {
                status = STATUS_INVALID_PARAMETER;
            }
            break;
        }

        // =========================================================================
        // 2. CALCOLO DELLA BARRIERA ED INNESCO DEI SYSTEM THREADS
        // =========================================================================
        case IOCTL_AVVIA_SERVIZIO: {
            if (g_ThreadsSpawned) {
                status = STATUS_SUCCESS; // Già in esecuzione
                break;
            }

            if (g_pTaskAContext->PeriodTicks == 0) {
                status = STATUS_INVALID_DEVICE_STATE; // Parametri mancanti
                break;
            }

            // Calcolo del timestamp hardware coordinato: Tempo Attuale + 1ms (1000 microsecondi)
            /*Per convertire 1 ms (pari a 1000 microsecondi) in tick hardware senza appoggiarsi ai decimali, la formula intera corretta è:\(\text{Ticks}=\frac{1000\times \text{Frequenza}}{1.000.000}=\frac{\text{Frequenza}}{1000}\)*/
            // ULONG64 synchronizedStart = currentTicks.QuadPart + static_cast<ULONG64>(ticksPerUs * 1000);
            // CORREZIONE DEFINITIVA: Calcolo dei tick hardware per 1ms senza l'uso di double
            // Formula pura a 64-bit intera: Ticks = freq.QuadPart / 1000
            ULONG64 synchronizedStart = currentTicks.QuadPart + (static_cast<ULONG64>(freq.QuadPart) / 1000ULL);


            g_pTaskAContext->SynchronizedStartTicks = synchronizedStart;
            g_pTaskAContext->KeepRunning = 1;

            g_pTaskBContext->SynchronizedStartTicks = synchronizedStart;
            g_pTaskBContext->KeepRunning = 1;

            // Spawn simultaneo dei thread di Ring 0
            status = PsCreateSystemThread(&g_hThreadA, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, g_pTaskAContext);
            if (!NT_SUCCESS(status)) break;

            status = PsCreateSystemThread(&g_hThreadB, THREAD_ALL_ACCESS, NULL, NULL, NULL, CorePollingRoutineGeneric, g_pTaskBContext);
            if (!NT_SUCCESS(status)) {
                InterlockedExchange(&g_pTaskAContext->KeepRunning, 0);
                break;
            }

            g_ThreadsSpawned = TRUE;
            KdPrint(("[MALAMORE] Boot del tandem eseguito. Partenza sincrona agganciata.\n"));
            break;
        }

        // =========================================================================
        // 3. ARRESTO ATOMICO E SFRATTO DEL POLLING ATTIVO
        // =========================================================================
        case IOCTL_FERMA_SERVIZIO: {
            if (!g_ThreadsSpawned) {
                status = STATUS_SUCCESS;
                break;
            }

            // Segnale atomico hardware di arresto dei cicli
            InterlockedExchange(&g_pTaskAContext->KeepRunning, 0);
            InterlockedExchange(&g_pTaskBContext->KeepRunning, 0);

            // Attesa sincronizzata del rientro a PASSIVE_LEVEL dei thread prima del rilascio
            if (g_hThreadA) {
                PVOID threadObj = nullptr;
                if (NT_SUCCESS(ObReferenceObjectByHandle(g_hThreadA, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &threadObj, NULL))) {
                    KeWaitForSingleObject(threadObj, Executive, KernelMode, FALSE, NULL);
                    ObDereferenceObject(threadObj);
                }
                ZwClose(g_hThreadA);
                g_hThreadA = NULL;
            }

            if (g_hThreadB) {
                PVOID threadObj = nullptr;
                if (NT_SUCCESS(ObReferenceObjectByHandle(g_hThreadB, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &threadObj, NULL))) {
                    KeWaitForSingleObject(threadObj, Executive, KernelMode, FALSE, NULL);
                    ObDereferenceObject(threadObj);
                }
                ZwClose(g_hThreadB);
                g_hThreadB = NULL;
            }

            g_ThreadsSpawned = FALSE;

            // Ripristiniamo la memoria a zero per un'eventuale successiva chiamata a IOCTL_INVIA_PARAMETRI
            g_pTaskAContext->PeriodTicks = 0;
            g_pTaskBContext->PeriodTicks = 0;
            KdPrint(("[MALAMORE] Servizio interrotto. Core liberati dal tandem.\n"));
            break;
        }

        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesReturned;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

// =========================================================================
// ROUTINE DI GESTIONE APERTURA/CHIUSURA CANALE (RING 3 <-> RING 0)
// =========================================================================
NTSTATUS DispatchCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ IRP *Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);

    // Convalida l'I/O impostando lo stato di successo per CreateFileW / CloseHandle
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;

    // Rilascia l'IRP completando la richiesta senza incrementare la priorità
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

// =========================================================================
// ROUTINE DI UNLOAD: RILASCIO HARDWARE E SCONGIURAMENTO BSOD ORFANI
// =========================================================================
VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject) {
    UNICODE_STRING symLink;

    // 1. Rimozione del link simbolico e distruzione del Device Object nel kernel
    RtlInitUnicodeString(&symLink, SYMBOLIC_LINK_WCHAR);
    IoDeleteSymbolicLink(&symLink);

    if (DriverObject->DeviceObject) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }

    // 2. DISATTIVAZIONE DI SICUREZZA IN CASO DI UNLOAD BRUTALE (Senza IOCTL_FERMA_SERVIZIO)
    if (g_ThreadsSpawned) {
        KdPrint(("[MALAMORE] Rilevato scaricamento forzato. Sfratto asincrono del tandem...\n"));

        // Ordina ai cicli while a tandem di arrestarsi ed uscire dallo Yield
        if (g_pTaskAContext) InterlockedExchange(&g_pTaskAContext->KeepRunning, 0);
        if (g_pTaskBContext) InterlockedExchange(&g_pTaskBContext->KeepRunning, 0);

        // Attesa bloccante del rientro del thread A
        if (g_hThreadA) {
            PVOID threadObj = nullptr;
            if (NT_SUCCESS(ObReferenceObjectByHandle(g_hThreadA, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &threadObj, NULL))) {
                KeWaitForSingleObject(threadObj, Executive, KernelMode, FALSE, NULL);
                ObDereferenceObject(threadObj);
            }
            ZwClose(g_hThreadA);
            g_hThreadA = NULL;
        }

        // Attesa bloccante del rientro del thread B
        if (g_hThreadB) {
            PVOID threadObj = nullptr;
            if (NT_SUCCESS(ObReferenceObjectByHandle(g_hThreadB, THREAD_ALL_ACCESS, *PsThreadType, KernelMode, &threadObj, NULL))) {
                KeWaitForSingleObject(threadObj, Executive, KernelMode, FALSE, NULL);
                ObDereferenceObject(threadObj);
            }
            ZwClose(g_hThreadB);
            g_hThreadB = NULL;
        }

        g_ThreadsSpawned = FALSE;
    }

    // 3. RILASCIO DELLA MEMORIA ALLOCATA NEL POOL NON-PAGATO
    if (g_pTaskAContext) {
        ExFreePoolWithTag(g_pTaskAContext, 'TskA');
        g_pTaskAContext = nullptr;
    }

    if (g_pTaskBContext) {
        ExFreePoolWithTag(g_pTaskBContext, 'TskB');
        g_pTaskBContext = nullptr;
    }

    KdPrint(("[MALAMORE DRIVER] Unload ultimato in sicurezza. Tutti i core logici liberati.\n"));
}

