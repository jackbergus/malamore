//
// Created by Giacomo Bergami on 15/09/2026.
//

#include <malamore/structures/TelemetryQueue.h>

void TelemetryQ_Init(TelemetryQueue* cb) {
    if (cb != nullptr) {
        cb->Init();
    }
}

BOOLEAN TelemetryQ_IsFull(const TelemetryQueue* cb) {
    // InterlockedOr con 0 esegue una lettura atomica barriera-protetta a livello hardware
    return (cb != nullptr && cb->IsFull()) ? TRUE : FALSE;
}

BOOLEAN TelemetryQ_IsEmpty(const TelemetryQueue* cb) {
    // Lettura atomica thread-safe per verificare se il buffer è vuoto
    return (cb != nullptr && cb->IsEmpty()) ? TRUE : FALSE;
}

/**
 * PUSH PROTETTA MULTI-WRITER (MOLTEPLICI SCRITTORI CONCORRENTI A DISPATCH_LEVEL)
 */
BOOLEAN TelemetryQ_Push(TelemetryQueue* cb, const struct TelemetryRecord* newMsg) {
    if (cb == nullptr || newMsg == nullptr) return FALSE;
    return cb->Push(newMsg);
}

/**
 * TOP SINGLE-READER LOCK-FREE (ZERO STARVATION PER IL PROCESSO DESTINATARIO)
 */
const struct TelemetryRecord* TelemetryQ_Top(const TelemetryQueue* cb) {
    if (cb == nullptr || TelemetryQ_IsEmpty(cb)) return nullptr;
    // Ritorna l'indirizzo di memoria dell'elemento (Zero-Copy)
    return cb->Top();
}

/**
 * POP SINGLE-READER LOCK-F\REE
 */
BOOLEAN TelemetryQ_Pop(TelemetryQueue* cb) {
    if (cb == nullptr || TelemetryQ_IsEmpty(cb)) return FALSE;
    return cb->Pop();
}
