// #define LINK_KERNEL
#include <malamore.h>

UNICODE_STRING g_DeviceName = RTL_CONSTANT_STRING(L"\\Device\\KernelBinderBridge");
UNICODE_STRING g_SymLinkName = RTL_CONSTANT_STRING(L"\\??\\KernelBinderLink");
PDEVICE_OBJECT g_DeviceObject = NULL;

PVOID g_KernelAddress = NULL;
PMDL g_Mdl = NULL;
const ULONG SHARED_SIZE = sizeof(SharedDataBlock);

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

    if (stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_MAP_SHARED_MEMORY) {
        if (stack->Parameters.DeviceIoControl.OutputBufferLength >= sizeof(PVOID)) {
            PVOID* userAddressOut = (PVOID*)Irp->AssociatedIrp.SystemBuffer;
            
            __try {
                // Mappa lo stesso blocco di memoria fisica nel processo utente chiamante corrente
                PVOID userAddress = MmMapLockedPagesSpecifyCache(
                    g_Mdl, UserMode, MmCached, NULL, FALSE, NormalPagePriority
                );
                
                *userAddressOut = userAddress;
                info = sizeof(PVOID);
                KdPrint(("Memoria mappata con successo per il processo ID: %p\n", PsGetCurrentProcessId()));
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                status = STATUS_NONE_MAPPED;
            }
        } else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    } else {
        status = STATUS_INVALID_DEVICE_REQUEST;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

VOID DriverUnload(_In_ PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);
    if (g_Mdl) {
        IoFreeMdl(g_Mdl);
    }
    if (g_KernelAddress) {
        ExFreePoolWithTag(g_KernelAddress, 'Bind');
    }
    IoDeleteSymbolicLink(&g_SymLinkName);
    IoDeleteDevice(g_DeviceObject);
    KdPrint(("Driver Binder scaricato.\n"));
}

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverUnload = DriverUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchIoctl;

    // Alloca memoria permanente a livello hardware
    // Sostituisci questo:
    // g_KernelAddress = ExAllocatePool2(POOL_FLAG_NON_PAGED, SHARED_SIZE, 'Bind');
    // Con questo:
    g_KernelAddress = ExAllocatePoolWithTag(NonPagedPool, SHARED_SIZE, 'Bind');
    // g_KernelAddress = ExAllocatePool2(POOL_FLAG_NON_PAGED, SHARED_SIZE, 'Bind');
    if (!g_KernelAddress) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(g_KernelAddress, SHARED_SIZE);

    g_Mdl = IoAllocateMdl(g_KernelAddress, SHARED_SIZE, FALSE, FALSE, NULL);
    if (!g_Mdl) {
        ExFreePoolWithTag(g_KernelAddress, 'Bind');
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    MmBuildMdlForNonPagedPool(g_Mdl);

    return IoCreateDevice(DriverObject, 0, &g_DeviceName, MY_DEVICE_TYPE, 0, FALSE, &g_DeviceObject)
           || IoCreateSymbolicLink(&g_SymLinkName, &g_DeviceName);
}
