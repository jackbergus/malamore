//
// Created by giaco on 12/09/2026.
//
#include <malamore.h>
#pragma alloc_text(PAGEWNK, TaskFiltroPinco)
#pragma alloc_text(PAGEWNK, TaskSinkPallino)
#pragma alloc_text(PAGEWNK, TaskAnalisiGenerico)

VOID TaskFiltroPinco(ULONG CoreId, PVOID ContextMessage) {
    PCONTESTO_SERVIZIO contesto = (PCONTESTO_SERVIZIO)ContextMessage;
    for (int i = 0; i < PARAM_COUNT; i++) {
        contesto->BufferRisultati[i] = contesto->BufferParametri[i] * 3;
    }
    UNREFERENCED_PARAMETER(CoreId);
}

VOID TaskSinkPallino(ULONG CoreId, PVOID ContextMessage) {
    PCONTESTO_SERVIZIO contesto = (PCONTESTO_SERVIZIO)ContextMessage;
    LONG datiElaborati[PARAM_COUNT];
    for (int i = 0; i < PARAM_COUNT; i++) {
        datiElaborati[i] = contesto->BufferRisultati[i] + 50;
    }
    ScriviRecordSuFileSink(contesto, datiElaborati);
    UNREFERENCED_PARAMETER(CoreId);
}

VOID TaskAnalisiGenerico(ULONG CoreId, PVOID ContextMessage) {
    PCONTESTO_SERVIZIO contesto = (PCONTESTO_SERVIZIO)ContextMessage;
    // Un terzo task ipotetico richiamabile dal client
    for (int i = 0; i < PARAM_COUNT; i++) {
        contesto->BufferParametri[i] = 0; // Reset
    }
    UNREFERENCED_PARAMETER(CoreId);
}