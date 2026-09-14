#include <ntddk.h>
#include <ntstrsafe.h>
#include "malamore/ipc_manager.h"

LIST_ENTRY g_IpcCodaMessaggi;
KSPIN_LOCK g_IpcLock;

LOOKASIDE_LIST_EX g_IpcLookasideList;
BOOLEAN g_IpcInizializzato = FALSE;

VOID InizializzaIpcManager(VOID) {
    InitializeListHead(&g_IpcCodaMessaggi);
    KeInitializeSpinLock(&g_IpcLock);

    // CORREZIONE: Firma a 8 parametri con PoolType corretto (NonPagedPool)
    NTSTATUS status = ExInitializeLookasideListEx(
        &g_IpcLookasideList,
        NULL,                  // Allocate function di default
        NULL,                  // Free function di default
        NonPagedPool,          // PoolType (POOL_TYPE enum)
        0,                     // Flags (Riservato, impostare a 0)
        sizeof(MESSAGGIO_IPC), // Size
        'IpcL',                // Tag
        0                      // Depth (Riservato, impostare a 0)
    );

    if (NT_SUCCESS(status)) {
        g_IpcInizializzato = TRUE;
    }
}

VOID DeinizializzaIpcManager(VOID) {
    KLOCK_QUEUE_HANDLE lockHandle;
    PLIST_ENTRY entry;

    if (!g_IpcInizializzato) return;

    KeAcquireInStackQueuedSpinLock(&g_IpcLock, &lockHandle);
    while (!IsListEmpty(&g_IpcCodaMessaggi)) {
        entry = RemoveHeadList(&g_IpcCodaMessaggi);
        PMESSAGGIO_IPC msg = CONTAINING_RECORD(
            entry, MESSAGGIO_IPC, Entry
        );
        // CORREZIONE: Uso della funzione estesa corretta
        ExFreeToLookasideListEx(&g_IpcLookasideList, msg);
    }
    KeReleaseInStackQueuedSpinLock(&lockHandle);

    ExDeleteLookasideListEx(&g_IpcLookasideList);
    g_IpcInizializzato = FALSE;
}

extern "C" VOID IpcInviaMessaggio(
    ULONG MittenteId, ULONG DestinatarioId,
    PVOID Dati, ULONG Dimensione
) {
    if (Dimensione > MAX_IPC_PAYLOAD_SIZE || !g_IpcInizializzato) return;

    PMESSAGGIO_IPC nuovoMsg = (PMESSAGGIO_IPC)ExAllocateFromLookasideListEx(
        &g_IpcLookasideList
    );

    if (nuovoMsg == NULL) return;

    nuovoMsg->ServizioMittenteId = MittenteId;
    nuovoMsg->ServizioDestinatarioId = DestinatarioId;
    nuovoMsg->LunghezzaPayload = Dimensione;
    RtlCopyMemory(nuovoMsg->Payload, Dati, Dimensione);

    KLOCK_QUEUE_HANDLE lockHandle;
    KeAcquireInStackQueuedSpinLock(&g_IpcLock, &lockHandle);
    InsertTailList(&g_IpcCodaMessaggi, &nuovoMsg->Entry);
    KeReleaseInStackQueuedSpinLock(&lockHandle);
}

extern "C" BOOLEAN IpcRiceviMessaggio(
    ULONG DestinatarioId, PVOID BufferOut, PULONG DimensioneOut
) {
    KLOCK_QUEUE_HANDLE lockHandle;
    PLIST_ENTRY entry;
    PMESSAGGIO_IPC msgTrovato = NULL;

    if (!g_IpcInizializzato) return FALSE;

    KeAcquireInStackQueuedSpinLock(&g_IpcLock, &lockHandle);
    entry = g_IpcCodaMessaggi.Flink;
    while (entry != &g_IpcCodaMessaggi) {
        PMESSAGGIO_IPC corrente = CONTAINING_RECORD(
            entry, MESSAGGIO_IPC, Entry
        );
        if (corrente->ServizioDestinatarioId == DestinatarioId) {
            msgTrovato = corrente;
            RemoveEntryList(entry);
            break;
        }
        entry = entry->Flink;
    }
    KeReleaseInStackQueuedSpinLock(&lockHandle);

    if (msgTrovato != NULL) {
        RtlCopyMemory(BufferOut, msgTrovato->Payload, msgTrovato->LunghezzaPayload);
        *DimensioneOut = msgTrovato->LunghezzaPayload;

        // CORREZIONE: Rilascio pulito tramite l'API LookasideListEx reale
        ExFreeToLookasideListEx(&g_IpcLookasideList, msgTrovato);
        return TRUE;
    }
    return FALSE;
}
