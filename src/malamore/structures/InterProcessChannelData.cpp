//
// Created by Giacomo Bergami on 15/09/2026.
//

#include <malamore/structures/InterProcessChannelData.h>
#include <ntddk.h>


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
