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


#define FUTURE_BUFFER_SIZE 512

// =========================================================================
// PACCHETTO DI SCAMBIO STATICO AD ARCHITETTURA Ring0/Ring3 (ZERO HEAP)
// =========================================================================
#pragma pack(push, 4)
struct FutureResultParcel {
    UCHAR DataBuffer[FUTURE_BUFFER_SIZE]; // <--- REQUISITO: Buffer statico nativo di sosta
    ULONG DataLength;                     // Dimensione reale espressa in byte dei dati validi
};

// Firma aggiornata della routine computazionale Real-Time del tandem o dell'I/O
typedef void (*PFN_FUTURE_THREAD_ROUTINE)(PVOID Context, struct FutureResultParcel* OutResult);

struct ThreadBridgePayload {
    struct UnifiedThreadInterface* Slot;
    PFN_FUTURE_THREAD_ROUTINE RealRoutine;
};

// =========================================================================
// STRUTTURA UNIFICATA DI UN THREAD DEL FRAMEWORK MALAMORE
// =========================================================================
struct UnifiedThreadInterface {
    HANDLE ThreadHandle;
    struct UnifiedThreadContext Context;

    // SPECIFICA: Sostituzione dei campi sparsi con l'incapsulamento del Parcel nativo
    struct FutureResultParcel FutureResult;
    volatile LONG IsCompleted;              // Flag atomico hardware: 0 = In esecuzione, 1 = Concluso
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
    ThreadBridgePayload             fields; // Puntatore alla funzione ponte (Hardware o Tandem)
    BOOLEAN                        m_IsStarted;

    // Disabilitiamo esplicitamente la copia e l'assegnazione per prevenire leak di Handle hardware
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;


#ifdef LINK_KERNEL
    static VOID SystemThreadBridge(_In_ PVOID Context);
#else
    static DWORD WINAPI UserThreadBridge(LPVOID Context);
#endif

public:
    // Il costruttore aggancia lo slot statico del pool e la routine di riferimento
    ThreadGuard(struct UnifiedThreadInterface* threadSlot,
                PFN_FUTURE_THREAD_ROUTINE  routine);

    // Il Distruttore RAII: Garantisce lo sfratto protetto automatico all'uscita dello scope
    ~ThreadGuard();

    // Allineamento alle API Java Thread: Innesca il ciclo
    NTSTATUS Start();


    // Attesa attiva deterministica sul flag hardware
    VOID Join();

    // Sfratto protetto: Spegne il flag, attende la morte fisica del thread e chiude l'handle
    VOID Stop();

    // =========================================================================
    // METODO FUTURE 'GET' LOCK-FREE: Restituisce direttamente il puntatore al parcel
    // =========================================================================
    const FutureResultParcel* Get() const {
        auto& m_ThreadSlot = this->fields.Slot;
        auto& m_Routine = this->fields.RealRoutine;
        if (m_ThreadSlot == nullptr)
            return nullptr;

        // Controllo atomico hardware a costo zero: se non ha completato, esce inline
        // azzerando il jitter dell'Engine Real-Time e prevenendo la starvation
        if (InterlockedOr(const_cast<volatile LONG*>(&m_ThreadSlot->IsCompleted), 0) == 0) {
            return nullptr;
        }

        // Restituisce direttamente l'indirizzo del parcel interno tipizzato (Zero-Copy hardware)
        return &m_ThreadSlot->FutureResult;
    }

    // Metodi di ispezione dello stato
    BOOLEAN IsStarted() const { return m_IsStarted; }
};
#endif

#endif //MALAMORE_THREADGUARD_H