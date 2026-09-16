//
// Created by Giacomo Bergami on 15/09/2026.
//

//
// Created by Giacomo Bergami on 15/09/2026.
//

#ifndef MALAMORE_GENERICRINGBUFFER_H
#define MALAMORE_GENERICRINGBUFFER_H

#include <malamore/utility/kernel_compatibility.h>

#include "malamore/utility/math.h"

#pragma pack(push, 1)
// Il TAS Lock rimane compatto a 1 byte o 4 byte
struct TAS_CHANNEL_LOCK {
    volatile LONG WriterState; // 0 = Libero, 1 = Occupato
};
#pragma pack(pop)

// Forziamo rigidamente l'allineamento a 4 byte per l'efficacia dei comandi Interlocked
#pragma pack(push, 4)
template <typename T, ULONG QueueDepth>
struct GenericRingBuffer {
    struct TAS_CHANNEL_LOCK WriteLock;
    static constexpr ULONG QUEUE_DEPTH = QueueDepth;

    volatile LONG ReadIndex;
    volatile LONG WriteIndex;
    volatile LONG MessageCount; // Corrisponde al vecchio 'Count' di TelemetryQueue

    // L'array statico il cui tipo e dimensione vengono iniettati a runtime dal compilatore
    T Buffer[QueueDepth];

    // =========================================================================
    // APPI DEL MOTORE GENERICO (LOCK-FREE IN LETTURA / SPINLOCK IN SCRITTURA)
    // =========================================================================

    inline void Init() {
        this->WriteLock.WriterState = 0;
        this->ReadIndex = 0;
        this->WriteIndex = 0;
        this->MessageCount = 0;
        RtlZeroMemory(static_cast<void*>(this->Buffer), sizeof(this->Buffer));
    }

    inline BOOLEAN IsFull() const {
        return (InterlockedOr(const_cast<volatile LONG*>(&this->MessageCount), 0) == QueueDepth) ? TRUE : FALSE;
    }

    inline BOOLEAN IsEmpty() const {
        return (InterlockedOr(const_cast<volatile LONG*>(&this->MessageCount), 0) == 0) ? TRUE : FALSE;
    }

    // Inserimento Multi-Writer protetto da TAS_LOCK hardware a DISPATCH_LEVEL
    inline BOOLEAN Push(const T* newItem) {
        if (newItem == nullptr) return FALSE;

        // 1. Acquisizione del lock tra i soli scrittori concorrenti
        while (InterlockedExchange(&this->WriteLock.WriterState, 1) == 1) {
            YieldProcessor(); // Istruzione assembly PAUSE
        }

        // 2. Controllo dello spazio all'interno dell'area protetta
        if (this->IsFull()) {
            InterlockedExchange(&this->WriteLock.WriterState, 0); // Rilascio di sicurezza
            return FALSE;
        }

        // Mascheramento bit a bit velocissimo (funziona se QueueDepth è potenza di 2)
        LONG currentWrite = Malamore::Math::WrapIndex<QueueDepth>(this->WriteIndex);
        RtlCopyMemory(&this->Buffer[currentWrite], newItem, sizeof(T));

        // Hardware Memory Barrier contro le ottimizzazioni out-of-order della CPU
        KeMemoryBarrier();

        this->WriteIndex = Malamore::Math::WrapIndex<QueueDepth>((this->WriteIndex + 1));

        // 3. Rilascio del lock
        InterlockedExchange(&this->WriteLock.WriterState, 0);

        // 4. Incremento atomico per svegliare il singolo lettore
        InterlockedIncrement(&this->MessageCount);
        return TRUE;
    }

    // Lettura Zero-Copy Single-Reader (Completamente Lock-Free)
    inline const T* Top() const {
        if (this->IsEmpty()) return nullptr;
        LONG currentRead = Malamore::Math::WrapIndex<QueueDepth>(this->ReadIndex);
        return &this->Buffer[currentRead];
    }

    // Rimozione dello slot consumato dal Singolo Lettore
    inline BOOLEAN Pop() {
        if (this->IsEmpty()) return FALSE;

        KeMemoryBarrier(); // Blinda la conclusione della lettura prima del rilascio dello slot
        this->ReadIndex = Malamore::Math::WrapIndex<QueueDepth>(this->ReadIndex + 1);
        InterlockedDecrement(&this->MessageCount);
        return TRUE;
    }
};
#pragma pack(pop)

#endif //MALAMORE_GENERICRINGBUFFER_H
