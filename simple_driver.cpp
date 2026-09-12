#include <malamore.h>

// Liste e sincronizzazioni globali dell'Orchestratore
LIST_ENTRY g_ListaServizi;
ERESOURCE g_ListaLock;

TASK_MAP_ENTRY g_TabellaTask[MAX_TASK_TABLE_ENTRIES];
ULONG g_NumeroTaskRegistrati = 0;
ERESOURCE g_TabellaLock;

PDEVICE_OBJECT g_DeviceObject = NULL;

// Funzione esportata per permettere la registrazione dinamica ad altri .sys esterni
extern "C" __declspec(dllexport) NTSTATUS RegistraTaskEsterno(PCSTR NomeServizio, PFN_KERNEL_TASK PuntatoreTask) {
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&g_TabellaLock, TRUE);

    if (g_NumeroTaskRegistrati >= MAX_TASK_TABLE_ENTRIES) {
        ExReleaseResourceLite(&g_TabellaLock);
        KeLeaveCriticalRegion();
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(g_TabellaTask[g_NumeroTaskRegistrati].NomeServizio, NomeServizio, MAX_SERVICE_NAME_LEN);
    g_TabellaTask[g_NumeroTaskRegistrati].PuntatoreTask = PuntatoreTask;
    g_NumeroTaskRegistrati++;

    ExReleaseResourceLite(&g_TabellaLock);
    KeLeaveCriticalRegion();

    KdPrint(("Orchestratore: Registrato mapping per la stringa [%s]\n", NomeServizio));
    return STATUS_SUCCESS;
}

PFN_KERNEL_TASK CercaTaskInTabella(PCSTR NomeServizio) {
    PFN_KERNEL_TASK trovato = NULL;
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&g_TabellaLock, TRUE);

    for (ULONG i = 0; i < g_NumeroTaskRegistrati; i++) {
        if (RtlCompareMemory(g_TabellaTask[i].NomeServizio, NomeServizio, MAX_SERVICE_NAME_LEN) == MAX_SERVICE_NAME_LEN) {
            trovato = g_TabellaTask[i].PuntatoreTask;
            break;
        }
    }

    ExReleaseResourceLite(&g_TabellaLock);
    KeLeaveCriticalRegion();
    return trovato;
}

NTSTATUS AvviaNuovoServizio(PSERVIZIO_CONFIG_INPUT Input) {
    NTSTATUS status;

    // Convalida e Lookup della stringa identificativa nella tabella
    PFN_KERNEL_TASK taskPuntatore = CercaTaskInTabella(Input->NomeServizio);
    if (!taskPuntatore) return STATUS_NOT_FOUND;

    if (Input->PayloadSize > MAX_PAYLOAD_SIZE) return STATUS_INVALID_PARAMETER;

    LARGE_INTEGER freq;
    KeQueryPerformanceCounter(&freq);

    PCONTESTO_SERVIZIO contesto = (PCONTESTO_SERVIZIO)ExAllocatePoolWithTag(NonPagedPool, sizeof(CONTESTO_SERVIZIO), 'SrvC');
    if (!contesto) return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(contesto, sizeof(CONTESTO_SERVIZIO));
    contesto->ServizioId = Input->ServizioId;
    contesto->TerminateThreads = FALSE;
    contesto->TimerFrequency = freq;
    contesto->TaskDaEseguire = taskPuntatore;
    RtlCopyMemory(contesto->NomeServizio, Input->NomeServizio, MAX_SERVICE_NAME_LEN);

    // Se l'utente ha fornito parametri, alloca e isola il messaggio in NonPagedPool
    if (Input->PayloadSize > 0) {
        contesto->MessaggioParametrizzazione = ExAllocatePoolWithTag(NonPagedPool, Input->PayloadSize, 'MsgP');
        if (contesto->MessaggioParametrizzazione) {
            RtlCopyMemory(contesto->MessaggioParametrizzazione, Input->Payload, Input->PayloadSize);
        }
    }

    LONGLONG activeWindowTicks = (LONGLONG)((ACTIVE_WINDOW_US * freq.QuadPart) / 1000000);

    // --- Configurazione ed Istanziazione del Thread sul Core A ---
    PTHREAD_CONFIG configA = (PTHREAD_CONFIG)ExAllocatePoolWithTag(NonPagedPool, sizeof(THREAD_CONFIG), 'ThCf');
    if (configA) {
        RtlZeroMemory(configA, sizeof(THREAD_CONFIG));
        configA->Affinity.Group = 0;
        configA->Affinity.Mask = ((KAFFINITY)1 << Input->CoreA);
        configA->PhaseOffsetTicks = 0;
        configA->ContestoServizio = contesto;

        status = PsCreateSystemThread(&contesto->ThreadHandleA, THREAD_ALL_ACCESS, NULL, NULL, NULL, CoreAPollingRoutine, configA);
        if (!NT_SUCCESS(status)) ExFreePoolWithTag(configA, 'ThCf');
    }

    // --- Configurazione ed Istanziazione del Thread sul Core B (In tandem sfasato) ---
    PTHREAD_CONFIG configB = (PTHREAD_CONFIG)ExAllocatePoolWithTag(NonPagedPool, sizeof(THREAD_CONFIG), 'ThCf');
    if (configB) {
        RtlZeroMemory(configB, sizeof(THREAD_CONFIG));
        configB->Affinity.Group = 0;
        configB->Affinity.Mask = ((KAFFINITY)1 << Input->CoreB);
        configB->PhaseOffsetTicks = activeWindowTicks;
        configB->ContestoServizio = contesto;

        status = PsCreateSystemThread(&contesto->ThreadHandleB, THREAD_ALL_ACCESS, NULL, NULL, NULL, CoreBPollingRoutine, configB);
        if (!NT_SUCCESS(status)) ExFreePoolWithTag(configB, 'ThCf');
    }

    // Registra l'istanza del servizio nella coda attiva
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&g_ListaLock, TRUE);
    InsertTailList(&g_ListaServizi, &contesto->Entry);
    ExReleaseResourceLite(&g_ListaLock);
    KeLeaveCriticalRegion();

    return STATUS_SUCCESS;
}

NTSTATUS FermaServizio(ULONG ServizioId) {
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&g_ListaLock, TRUE);

    PLIST_ENTRY entry = g_ListaServizi.Flink;
    while (entry != &g_ListaServizi) {
        PCONTESTO_SERVIZIO contesto = CONTAINING_RECORD(entry, CONTESTO_SERVIZIO, Entry);
        if (contesto->ServizioId == ServizioId) {
            contesto->TerminateThreads = TRUE;

            if (contesto->ThreadHandleA) ZwClose(contesto->ThreadHandleA);
            if (contesto->ThreadHandleB) ZwClose(contesto->ThreadHandleB);

            if (contesto->MessaggioParametrizzazione) {
                ExFreePoolWithTag(contesto->MessaggioParametrizzazione, 'MsgP');
            }

            RemoveEntryList(entry);
            ExFreePoolWithTag(contesto, 'SrvC');
            break;
        }
        entry = entry->Flink;
    }

    ExReleaseResourceLite(&g_ListaLock);
    KeLeaveCriticalRegion();
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS DriverCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS DriverDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;

    ULONG ioctlCode = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG inputLength = stack->Parameters.DeviceIoControl.InputBufferLength;
    PVOID buffer = Irp->AssociatedIrp.SystemBuffer;

    if (ioctlCode == IOCTL_AVVIA_SERVIZIO) {
        if (inputLength >= sizeof(SERVIZIO_CONFIG_INPUT) && buffer != NULL) {
            status = AvviaNuovoServizio((PSERVIZIO_CONFIG_INPUT)buffer);
        } else {
            status = STATUS_INFO_LENGTH_MISMATCH;
        }
    }
    else if (ioctlCode == IOCTL_FERMA_SERVIZIO) {
        if (inputLength >= sizeof(ULONG) && buffer != NULL) {
            status = FermaServizio(*(PULONG)buffer);
        } else {
            status = STATUS_INFO_LENGTH_MISMATCH;
        }
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status;

    InitializeListHead(&g_ListaServizi);
    InitializeListHead(&g_ListaServizi);
    ExInitializeResourceLite(&g_ListaLock);
    ExInitializeResourceLite(&g_TabellaLock);

    UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\MalamoreOrchestrator");
    UNICODE_STRING symLinkName = RTL_CONSTANT_STRING(L"\\??\\MalamoreOrchestratorLink");

    DriverObject->DriverUnload = [](PDRIVER_OBJECT Obj) {
        UNREFERENCED_PARAMETER(Obj);
        UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\MalamoreOrchestratorLink");
        IoDeleteSymbolicLink(&symLink);
        if (g_DeviceObject) IoDeleteDevice(g_DeviceObject);

        ExDeleteResourceLite(&g_ListaLock);
        ExDeleteResourceLite(&g_TabellaLock);
    };

    DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = DriverCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverDeviceControl;

    status = IoCreateDevice(DriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &g_DeviceObject);
    if (!NT_SUCCESS(status)) return status;

    IoDeleteSymbolicLink(&symLinkName);
    status = IoCreateSymbolicLink(&symLinkName, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    return STATUS_SUCCESS;
}
