//
// Created by giaco on 12/09/2026.
//

#ifndef MALAMORE_TASK_LETTURA_H
#define MALAMORE_TASK_LETTURA_H
#ifndef DRIVER_LETTURA_H
#define DRIVER_LETTURA_H

#include <ntddk.h>

// Prototipi dei task accessibili (Dichiarati in sezione non paginata)
#ifdef __cplusplus
extern "C" {
#endif
    VOID TaskFiltroPinco(ULONG CoreId, PVOID ContextMessage);
    VOID TaskSinkPallino(ULONG CoreId, PVOID ContextMessage);
    VOID TaskAnalisiGenerico(ULONG CoreId, PVOID ContextMessage);
#ifdef __cplusplus
}
#endif

#endif
#endif




