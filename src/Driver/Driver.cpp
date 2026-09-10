// #define LINK_KERNEL
#include <malamore.h>

UNICODE_STRING g_DeviceName = RTL_CONSTANT_STRING(L"\\Device\\KernelBinderBridge");
UNICODE_STRING g_SymLinkName = RTL_CONSTANT_STRING(L"\\??\\KernelBinderLink");
PDEVICE_OBJECT g_DeviceObject = NULL;
PKEVENT g_ServiceManagerEvent = NULL;
BinderParcel g_SharedParcel;

NTSTATUS DispatchCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS DispatchIoctl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR info = 0;

    switch (stack->Parameters.DeviceIoControl.IoControlCode) {
        case IOCTL_REGISTER_SERVICE_MANAGER: {
            HANDLE hEvent = *(HANDLE*)Irp->AssociatedIrp.SystemBuffer;
            ObReferenceObjectByHandle(hEvent, EVENT_MODIFY_STATE, *ExEventObjectType, UserMode, (PVOID*)&g_ServiceManagerEvent, NULL);
            break;
        }
        case IOCTL_BINDER_COMMAND: {
            RtlCopyMemory(&g_SharedParcel, Irp->AssociatedIrp.SystemBuffer, sizeof(BinderParcel));
            if (g_ServiceManagerEvent) KeSetEvent(g_ServiceManagerEvent, IO_NO_INCREMENT, FALSE);

            LARGE_INTEGER delay;
            delay.QuadPart = -10000 * 30; // Finestra di tolleranza di 30ms per lo spawn
            KeDelayExecutionThread(KernelMode, FALSE, &delay);

            RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &g_SharedParcel, sizeof(BinderParcel));
            info = sizeof(BinderParcel);
            break;
        }
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);
    if (g_ServiceManagerEvent) ObDereferenceObject(g_ServiceManagerEvent);
    IoDeleteSymbolicLink(&g_SymLinkName);
    IoDeleteDevice(g_DeviceObject);
}

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverUnload = DriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchIoctl;
    return IoCreateDevice(DriverObject, 0, &g_DeviceName, MY_DEVICE_TYPE, 0, FALSE, &g_DeviceObject)
           || IoCreateSymbolicLink(&g_SymLinkName, &g_DeviceName);
}
