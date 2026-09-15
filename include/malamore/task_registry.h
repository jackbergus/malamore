//
// Created by Giacomo Bergami on 14/09/2026.
//

#ifndef TASKS_REGISTRY_H
#define TASKS_REGISTRY_H

#include <ntddk.h>

// #include "malamore/ipc_manager.h"
// #include "malamore/thread_generic_polling.h"
#include "malamore/threads/thread_shared.h"

#define MAX_SERVICE_NAME_LEN 32
#define PARAM_COUNT          10

// Firma universale dei Task
// typedef VOID (*PFN_KERNEL_TASK)(ULONG CoreId, PVOID ContextMessage);

// Struttura per mappare la stringa al puntatore a funzione
typedef struct _STATIC_TASK_ENTRY {
    CHAR NomeTask[MAX_SERVICE_NAME_LEN];
    PFN_EXECUTE_ROUTINE_IO FunctionPointer; // TODO: uniform this type
    // PFN_KERNEL_TASK PuntatoreTask;
} STATIC_TASK_ENTRY, *PSTATIC_TASK_ENTRY;

// Prototipi dei task accessibili (Dichiarati in sezione non paginata)
#ifdef __cplusplus
extern "C" {
#endif
    // VOID TaskFiltroPinco(ULONG CoreId, PVOID ContextMessage);
    // VOID TaskSinkPallino(ULONG CoreId, PVOID ContextMessage);
    // VOID TaskAnalisiGenerico(ULONG CoreId, PVOID ContextMessage);
    VOID TaskLetturaExecute(ULONG CoreId, MessageMap* InMap, MessageMap* OutMap, PVOID CustomContext);
#ifdef __cplusplus
}
#endif

// =========================================================================
// REGISTRO DEIC COMPITI DISPONIBILI A TEMPO DI CARICAMENTO
// Per aggiungere un nuovo task, basta inserire la coppia {"Nome", Funzione} qui sotto.
// =========================================================================
// static const STATIC_TASK_ENTRY g_RegistroTaskStatici[] = {
//     { "Task_Pinco",    TaskFiltroPinco },
//     { "Task_Pallino",  TaskSinkPallino },
//     { "Task_Generico", TaskAnalisiGenerico }
// };
static _STATIC_TASK_ENTRY g_TaskTable[] = {
    { "TaskLettura", TaskLetturaExecute },
    // { nullptr, nullptr } // Sentinella di chiusura array
};

#define TOTALE_TASK_REGISTRATI (sizeof(g_TaskTable) / sizeof(_STATIC_TASK_ENTRY))

#endif // TASKS_REGISTRY_H
