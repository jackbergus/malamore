//
// Created by Giacomo Bergami on 15/09/2026.
//

#include <malamore/structures/InterProcessChannelData.h>
// #include <ntddk.h>


void RealTimeIPC_Init(InterProcessChannelData* cb) {
    if (cb != nullptr) {
        cb->Init();
    }
}

BOOLEAN RealTimeIPC_IsFull(const InterProcessChannelData* cb) {
    // InterlockedOr con 0 esegue una lettura atomica barriera-protetta a livello hardware
    return (cb != nullptr && cb->IsFull()) ? TRUE : FALSE;
}

BOOLEAN RealTimeIPC_IsEmpty(const InterProcessChannelData* cb) {
    // Lettura atomica thread-safe per verificare se il buffer è vuoto
    return (cb != nullptr && cb->IsEmpty()) ? TRUE : FALSE;
}

/**
 * PUSH PROTETTA MULTI-WRITER (MOLTEPLICI SCRITTORI CONCORRENTI A DISPATCH_LEVEL)
 */
BOOLEAN RealTimeIPC_Push(InterProcessChannelData* cb, const struct DataChannelParcel* newMsg) {
    if (cb == nullptr || newMsg == nullptr) return FALSE;
    return cb->Push(newMsg);
}

/*ULONG Timestamp;
ULONG SourceNodeId;
ULONG DataLength;
UCHAR PayloadData[1024];*/

BOOLEAN RealTimeIPC_Push(InterProcessChannelData* cb, ULONG Timestamp, ULONG SourceNodeId, const UCHAR* PayloadData ,ULONG DataLength) {
    if (cb == nullptr) return FALSE;
     {
        // 1. Acquisizione del lock tra i soli scrittori concorrenti
        while (InterlockedExchange(&cb->WriteLock.WriterState, 1) == 1) {
            YieldProcessor(); // Istruzione assembly PAUSE
        }

        // 2. Controllo dello spazio all'interno dell'area protetta
        if (cb->IsFull()) {
            InterlockedExchange(&cb->WriteLock.WriterState, 0); // Rilascio di sicurezza
            return FALSE;
        }

        // Mascheramento bit a bit velocissimo (funziona se QueueDepth è potenza di 2).
        LONG currentWrite = Malamore::Math::WrapIndex<InterProcessChannelData::QUEUE_DEPTH>(cb->WriteIndex);
        if (DataLength > DataChannelParcelArraySize) {
            DataLength = DataChannelParcelArraySize;
        }
        auto& memory= cb->Buffer[currentWrite];
        memory.Timestamp  = Timestamp;
        memory.SourceNodeId = SourceNodeId;
        memory.DataLength = DataLength;
        RtlCopyMemory(&memory.PayloadData, PayloadData, DataLength);

        // Hardware Memory Barrier contro le ottimizzazioni out-of-order della CPU
        KeMemoryBarrier();
        cb->WriteIndex = Malamore::Math::WrapIndex<InterProcessChannelData::QUEUE_DEPTH>(cb->WriteIndex + ((LONG)1));

        // 3. Rilascio del lock
        InterlockedExchange(&cb->WriteLock.WriterState, 0);

        // 4. Incremento atomico per svegliare il singolo lettore
        InterlockedIncrement(&cb->MessageCount);
        return TRUE;
    }
}

/**
 * TOP SINGLE-READER LOCK-FREE (ZERO STARVATION PER IL PROCESSO DESTINATARIO)
 */
const struct DataChannelParcel* RealTimeIPC_Top(const InterProcessChannelData* cb) {
    if (cb == nullptr || RealTimeIPC_IsEmpty(cb)) return nullptr;
    // Ritorna l'indirizzo di memoria dell'elemento (Zero-Copy)
    return cb->Top();
}

/**
 * POP SINGLE-READER LOCK-FREE
 */
BOOLEAN RealTimeIPC_Pop(InterProcessChannelData* cb) {
    if (cb == nullptr || RealTimeIPC_IsEmpty(cb)) return FALSE;
    return cb->Pop();
}
