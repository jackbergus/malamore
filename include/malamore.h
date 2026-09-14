#ifndef SHARED_DEFS_H
#define SHARED_DEFS_H

#define POOL_ZERO_DOWN_LEVEL_SUPPORT 1
#include <ntddk.h>
#include "malamore/task_registry.h"
#include "malamore/pool_core.h"

#define CODA_MAX_MESSAGGI     4096

#define FSCTL_DEVICE_TYPE 0x00008000
#define IOCTL_AVVIA_SERVIZIO   CTL_CODE(FSCTL_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_INVIA_PARAMETRI  CTL_CODE(FSCTL_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_FERMA_SERVIZIO   CTL_CODE(FSCTL_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _RECORD_FILE {
    ULONG SequenceId;
    LONG Dati[PARAM_COUNT];
} RECORD_FILE, *PRECORD_FILE;

typedef struct _CODA_CIRCOLARE {
    RECORD_FILE Messaggi[CODA_MAX_MESSAGGI];
    volatile LONG Head;
    volatile LONG Tail;
} CODA_CIRCOLARE, *PCODA_CIRCOLARE;

// Input semplificato: l'utente NON sceglie i core
typedef struct _SERVIZIO_CONFIG_INPUT {
    ULONG ServizioId;
    CHAR NomeTaskA[MAX_SERVICE_NAME_LEN];
    ULONG PeriodoTaskAMs;
    ULONG DurataMediaTaskAUs;
    CHAR NomeTaskB[MAX_SERVICE_NAME_LEN];
    ULONG PeriodoTaskBMs;
    ULONG DurataMediaTaskBUs;
} SERVIZIO_CONFIG_INPUT, *PSERVIZIO_CONFIG_INPUT;


typedef struct _CONTESTO_SERVIZIO {
    LIST_ENTRY Entry;
    ULONG ServizioId;

    // Core assegnati dal Driver a runtime
    ULONG AllocazioneCoreA;
    ULONG AllocazioneCoreB;

    PFN_KERNEL_TASK TaskA;
    PFN_KERNEL_TASK TaskB;

    LONG BufferParametri[PARAM_COUNT];
    LONG BufferRisultati[PARAM_COUNT];
    KSPIN_LOCK LockDati;

    PCODA_CIRCOLARE CodaFile;
    HANDLE FileHandle;
    KEVENT CodaDatiEvent;
    HANDLE DiskWriterThreadHandle;

    BOOLEAN TerminateThreads;
    HANDLE ThreadHandleA;
    HANDLE ThreadHandleB;
    LARGE_INTEGER TimerFrequency;
} CONTESTO_SERVIZIO, *PCONTESTO_SERVIZIO;

typedef struct _THREAD_CONFIG {
    GROUP_AFFINITY Affinity;
    LONGLONG PeriodTicks;
    LONGLONG PhaseOffsetTicks;
    PCONTESTO_SERVIZIO ContestoServizio;
    BOOLEAN IsTaskA;
} THREAD_CONFIG, *PTHREAD_CONFIG;

VOID ScriviRecordSuFileSink(PCONTESTO_SERVIZIO contesto, PLONG datiDaSalvare);
VOID CorePollingRoutineGeneric(_In_ PVOID Context);

    struct PACKET_PARAMETRI {
        ULONG ServizioId;
        LONG Parametri[PARAM_COUNT];
    };

#ifdef __cplusplus
}
#endif

#endif // SHARED_DEFS_H
