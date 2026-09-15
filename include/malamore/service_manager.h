//
// Created by Giacomo Bergami on 14/09/2026.
//

#ifndef SERVICES_MANAGER_H
#define SERVICES_MANAGER_H

#include <malamore.h>

#include "threads/thread_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

    // Liste e lock globali esportati per il driver principale
    // extern LIST_ENTRY g_ListaServizi;
    // extern ERESOURCE g_ListaLock;

    // // Funzioni pubbliche di gestione dei servizi
    // VOID InizializzaManagerServizi(VOID);
    // VOID DeinizializzaManagerServizi(VOID);
    // NTSTATUS AvviaNuovoServizio(PSERVIZIO_CONFIG_INPUT Input);
    // NTSTATUS AggiornaParametriServizio(ULONG ServizioId, PLONG NuoviParametri);
    // NTSTATUS FermaServizio(ULONG ServizioId);

    // Rintraccia il puntatore alla funzione del kernel in base al nome stringa fornito dal client
    PFN_EXECUTE_ROUTINE_IO CercaTaskInTabella(const char* taskName);

#ifdef __cplusplus
}
#endif

#endif // SERVICES_MANAGER_H
