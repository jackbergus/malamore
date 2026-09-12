//
// Created by giaco on 12/09/2026.
//

#ifndef MALAMORE_TASK_LETTURA_H
#define MALAMORE_TASK_LETTURA_H
#ifndef DRIVER_LETTURA_H
#define DRIVER_LETTURA_H

#include <ntddk.h>

#ifdef __cplusplus
extern "C" {
#endif

// __declspec(dllexport) dice al linker di inserire la funzione nella tavola dei simboli del .sys
__declspec(dllexport) VOID EseguiLetturaPeriferica(ULONG CoreId);

#ifdef __cplusplus
}
#endif

#endif // DRIVER_LETTURA_H

#endif //MALAMORE_TASK_LETTURA_H
