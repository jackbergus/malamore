//
// Created by Giacomo Bergami on 14/09/2026.
//

#ifndef SERVICES_MANAGER_H
#define SERVICES_MANAGER_H

#include "threads/thread_shared.h"

#ifdef __cplusplus
extern "C" {
#endif
    /**
     *
     * Ricerca il task per nome all'interno della tabella delle associazioni. In quanto siamo purtroppo nel kernel, ci
     * tocca scandire un'array lineare senza ricorrere alla std::map (dato che la tabella può essere popolata dall'utente)
     *
     * @param taskName Il nome del task da ricercare nella tabella delle dichiarazioni
     * @return Se esiste il puntatore all'entrypoint del task summenzionato, verraà restituito il puntatore-non nullo
     *         associato
     */
    PFN_EXECUTE_ROUTINE_IO CercaTaskInTabella(const char* taskName);

#ifdef __cplusplus
}
#endif

#endif // SERVICES_MANAGER_H
