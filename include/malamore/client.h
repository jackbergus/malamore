//
// Created by Giacomo Bergami on 14/09/2026.
//

#ifndef MALAMORE_CLIENT_H
#define MALAMORE_CLIENT_H

// Definizione del tipo per i parametri in ingresso dal client
#pragma pack(push, 1)
#include "ipc_manager.h"

struct ServiceParams {
    ULONG PeriodUs;
    ULONG EstimatedWcetUs;
    char TaskName[64]; // <--- NUOVO: Fornito direttamente dall'applicazione utente (Ring 3)
};
#pragma pack(pop)

#include <malamore/shared.h>

#endif //MALAMORE_CLIENT_H