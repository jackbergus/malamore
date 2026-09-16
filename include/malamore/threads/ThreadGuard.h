//
// Created by mbda on 16/09/2026.
//

#ifndef MALAMORE_THREADGUARD_H
#define MALAMORE_THREADGUARD_H
//
// Created by Giacomo Bergami on 16/09/2026.
//


#include "malamore/threads/thread_shared.h"
#include "malamore/utility/kernel_compatibility.h"
// #include "malamore/pool_core.h" // Per importare la definizione di UnifiedThreadContext

// =========================================================================
// STRUTTURA UNIFICATA DI UN THREAD DEL FRAMEWORK MALAMORE
// =========================================================================
#pragma pack(push, 4)
struct UnifiedThreadInterface {
    HANDLE ThreadHandle;
    struct UnifiedThreadContext Context;
};
#pragma pack(pop)

#ifdef __cplusplus
/**
 * This class provides an unification layer between threads at the Kernel Level and the ones at the user level
 * This is provided so that threads can be run and tested both at the user level and at the kernel level. This
 * should expedite the testing for threads
 */
class ThreadGuard {
private:
    struct UnifiedThreadInterface* m_ThreadSlot;
    PKSTART_ROUTINE                m_Routine; // Puntatore alla funzione ponte (Hardware o Tandem)
    BOOLEAN                        m_IsStarted;

    // Disabilitiamo esplicitamente la copia e l'assegnazione per prevenire leak di Handle hardware
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;

public:
    // Il costruttore aggancia lo slot statico del pool e la routine di riferimento
    ThreadGuard(struct UnifiedThreadInterface* threadSlot,
                PKSTART_ROUTINE routine);

    // Il Distruttore RAII: Garantisce lo sfratto protetto automatico all'uscita dello scope
    ~ThreadGuard();

    // Allineamento alle API Java Thread: Innesca il ciclo
    NTSTATUS Start();

    // Attesa attiva deterministica sul flag hardware
    VOID Join();

    // Sfratto protetto: Spegne il flag, attende la morte fisica del thread e chiude l'handle
    VOID Stop();

    // Metodi di ispezione dello stato
    BOOLEAN IsStarted() const { return m_IsStarted; }
};
#endif

#endif //MALAMORE_THREADGUARD_H