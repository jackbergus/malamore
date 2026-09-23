//
// Created by Giacomo Bergami, PhD on 23/09/2026.
//

#include <malamore/utility/kernel_compatibility.h>

/**
 *
 * @param DeviceObject
 * @param Irp
 * @return
 */
extern "C" NTSTATUS DriverCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp) {
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}