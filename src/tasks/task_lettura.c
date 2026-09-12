//
// Created by giaco on 12/09/2026.
//
#include <tasks/task_lettura.h>

// Specifica esplicitamente al compilatore che questo codice deve stare
// in una sezione NON paginata (P=Paginata, N=Non Paginata)
#pragma alloc_text(PAGEWNK, EseguiLetturaPeriferica)

__declspec(dllexport) VOID EseguiLetturaPeriferica(ULONG CoreId) {
    // Questo codice viene eseguito a HIGH_LEVEL nel contesto dei thread dell'Orchestratore.
    // Esegui qui il polling hardware puro dei registri della periferica.

    // Esempio:
    // UCHAR dato = READ_PORT_UCHAR((PUCHAR)0x3F8);

    UNREFERENCED_PARAMETER(CoreId);
}

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    // Gli export driver puri spesso non fanno nulla nel DriverEntry
    return STATUS_SUCCESS;
}
