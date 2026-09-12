#ifndef SHARED_DEFS_H
#define SHARED_DEFS_H

#define POOL_ZERO_DOWN_LEVEL_SUPPORT 1
#include <ntddk.h>

#define TASK_PERIOD_MS         5
#define ACTIVE_WINDOW_US       2500
#define MAX_SERVICE_NAME_LEN    32
#define MAX_TASK_TABLE_ENTRIES  16
#define MAX_PAYLOAD_SIZE       512

// Codici di Controllo IOCTL per la comunicazione User-Kernel
#define FSCTL_DEVICE_TYPE 0x00008000
#define IOCTL_AVVIA_SERVIZIO   CTL_CODE(FSCTL_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_FERMA_SERVIZIO   CTL_CODE(FSCTL_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#ifdef __cplusplus
extern "C" {
#endif

// Firma universale polimorfica per i task dei driver esterni
typedef VOID (*PFN_KERNEL_TASK)(ULONG CoreId, PVOID ContextMessage);

// Voce della Tabella di Lookup
typedef struct _TASK_MAP_ENTRY {
    CHAR NomeServizio[MAX_SERVICE_NAME_LEN];
    PFN_KERNEL_TASK PuntatoreTask;
} TASK_MAP_ENTRY, *PTASK_MAP_ENTRY;

// Struttura di input proveniente dall'User Mode (CreateFile/DeviceIoControl)
typedef struct _SERVIZIO_CONFIG_INPUT {
    ULONG CoreA;
    ULONG CoreB;
    ULONG ServizioId;
    CHAR NomeServizio[MAX_SERVICE_NAME_LEN];
    ULONG PayloadSize;
    UCHAR Payload[MAX_PAYLOAD_SIZE];
} SERVIZIO_CONFIG_INPUT, *PSERVIZIO_CONFIG_INPUT;

// Contesto di allocazione dinamica persistente per ogni coppia di thread attiva
typedef struct _CONTESTO_SERVIZIO {
    LIST_ENTRY Entry;
    ULONG ServizioId;
    CHAR NomeServizio[MAX_SERVICE_NAME_LEN];
    PFN_KERNEL_TASK TaskDaEseguire;
    PVOID MessaggioParametrizzazione;
    BOOLEAN TerminateThreads;
    HANDLE ThreadHandleA;
    HANDLE ThreadHandleB;
    LARGE_INTEGER TimerFrequency;
} CONTESTO_SERVIZIO, *PCONTESTO_SERVIZIO;

// Struttura passata singolarmente a ciascun thread di polling
typedef struct _THREAD_CONFIG {
    GROUP_AFFINITY Affinity;
    LONGLONG PhaseOffsetTicks;
    PCONTESTO_SERVIZIO ContestoServizio;
} THREAD_CONFIG, *PTHREAD_CONFIG;

// Funzione esportata per permettere la registrazione dei moduli esterni (.sys)
__declspec(dllexport) NTSTATUS RegistraTaskEsterno(PCSTR NomeServizio, PFN_KERNEL_TASK PuntatoreTask);

// Routine dei Thread di esecuzione
VOID CoreAPollingRoutine(_In_ PVOID Context);
VOID CoreBPollingRoutine(_In_ PVOID Context);

#ifdef __cplusplus
}
#endif

#endif // SHARED_DEFS_H
