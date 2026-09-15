
#include "malamore/task_registry.h"
#include <ntstrsafe.h>

// LIST_ENTRY g_ListaServizi;
// ERESOURCE g_ListaLock;


 PFN_EXECUTE_ROUTINE_IO CercaTaskInTabella(PCSTR NomeTask) {
    for (ULONG i = 0; i < TOTALE_TASK_REGISTRATI; i++) {
        if (RtlCompareMemory(g_TaskTable[i].NomeTask,
                             NomeTask,
                             MAX_SERVICE_NAME_LEN) == MAX_SERVICE_NAME_LEN) {
            return g_TaskTable[i].FunctionPointer;
        }
    }
    return NULL;
}
