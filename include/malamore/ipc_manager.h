#ifndef IPC_MANAGER_H
#define IPC_MANAGER_H

#include <ntddk.h>

#define MAX_SERVICE_NAME_LEN 32
#define PARAM_COUNT          10
#define MAX_IPC_PAYLOAD_SIZE 128

typedef struct _MESSAGGIO_IPC {
    LIST_ENTRY Entry;
    ULONG ServizioMittenteId;
    ULONG ServizioDestinatarioId;
    ULONG LunghezzaPayload;
    UCHAR Payload[MAX_IPC_PAYLOAD_SIZE];
} MESSAGGIO_IPC, *PMESSAGGIO_IPC;

#ifdef __cplusplus
extern "C" {
#endif

    VOID InizializzaIpcManager(VOID);
    VOID DeinizializzaIpcManager(VOID);

    VOID IpcInviaMessaggio(
        ULONG MittenteId,
        ULONG DestinatarioId,
        PVOID Dati,
        ULONG Dimensione
    );

    BOOLEAN IpcRiceviMessaggio(
        ULONG DestinatarioId,
        PVOID BufferOut,
        PULONG DimensioneOut
    );

#ifdef __cplusplus
}
#endif

#endif // IPC_MANAGER_H
