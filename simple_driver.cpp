#include <malamore.h>
// #include "src/threads/services_manager.h"
#include <malamore/service_manager.h>
#include <ntddk.h>
#include <wdmsec.h>

PDEVICE_OBJECT g_DeviceObject = NULL;

extern "C" NTSTATUS DriverCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS DriverDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
) {
    UNREFERENCED_PARAMETER(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG ioctlCode = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG inputLength = stack->Parameters.DeviceIoControl.InputBufferLength;
    PVOID buffer = Irp->AssociatedIrp.SystemBuffer;

    if (ioctlCode == IOCTL_AVVIA_SERVIZIO &&
        inputLength >= sizeof(SERVIZIO_CONFIG_INPUT) && buffer != NULL) {
        status = AvviaNuovoServizio((PSERVIZIO_CONFIG_INPUT)buffer);
    }
    else if (ioctlCode == IOCTL_INVIA_PARAMETRI &&
             inputLength >= (sizeof(ULONG) + (sizeof(LONG) * PARAM_COUNT)) &&
             buffer != NULL) {
        ULONG id = *(PULONG)buffer;
        PLONG params = (PLONG)((PUCHAR)buffer + sizeof(ULONG));
        status = AggiornaParametriServizio(id, params);
    }
    else if (ioctlCode == IOCTL_FERMA_SERVIZIO &&
             inputLength >= sizeof(ULONG) && buffer != NULL) {
        status = FermaServizio(*(PULONG)buffer);
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

extern "C" NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
) {
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status;

    ExInitializeDriverRuntime((ULONG)0);

    // Inizializza i moduli esterni separati
    InizializzaPoolCore();
    InizializzaManagerServizi();

    UNICODE_STRING deviceName =
        RTL_CONSTANT_STRING(L"\\Device\\MalamoreOrchestrator");
    UNICODE_STRING symLinkName =
        RTL_CONSTANT_STRING(L"\\??\\MalamoreOrchestratorLink");

    // Configura SDDL per consentire l'apertura del device agli utenti standard
    UNICODE_STRING sddlString =
        RTL_CONSTANT_STRING(L"D:P(A;;GA;;;BA)(A;;GA;;;AU)");

    DriverObject->DriverUnload = [](PDRIVER_OBJECT Obj) {
        UNREFERENCED_PARAMETER(Obj);
        UNICODE_STRING symLink =
            RTL_CONSTANT_STRING(L"\\??\\MalamoreOrchestratorLink");
        IoDeleteSymbolicLink(&symLink);

        if (g_DeviceObject) IoDeleteDevice(g_DeviceObject);

        DeinizializzaManagerServizi();
        DeinizializzaPoolCore();
    };

    DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]  = DriverCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DriverDeviceControl;

    status = IoCreateDeviceSecure(
        DriverObject, 0, &deviceName,
        FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN,
        FALSE, &sddlString, NULL, &g_DeviceObject
    );

    if (!NT_SUCCESS(status)) return status;

    IoDeleteSymbolicLink(&symLinkName);
    status = IoCreateSymbolicLink(&symLinkName, &deviceName);
    return status;
}
