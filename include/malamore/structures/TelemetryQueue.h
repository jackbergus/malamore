//
// Created by Giacomo Bergami on 15/09/2026.
//

#ifndef MALAMORE_TELEMETRYQUEUE_H
#define MALAMORE_TELEMETRYQUEUE_H

#include <malamore/structures/GenericRingBuffer.h>

#define TELEMETRY_QUEUE_SIZE 128

struct TelemetryRecord {
    ULONG64 Timestamp;
    ULONG CoreId;
    ULONG64 ExecutionTimeUs;
    char StatusFlag;
};
using TelemetryQueue = GenericRingBuffer<struct TelemetryRecord, TELEMETRY_QUEUE_SIZE>;

// Dichiarazione delle API di sincronizzazione hardware del canale
void TelemetryQ_Init(TelemetryQueue* cb);
BOOLEAN TelemetryQ_IsFull(const TelemetryQueue* cb);
BOOLEAN TelemetryQ_IsEmpty(const TelemetryQueue* cb);

// Scrittura protetta per molteplici scrittori concorrenti a DISPATCH_LEVEL
BOOLEAN TelemetryQ_Push(TelemetryQueue* cb, const struct TelemetryRecord* newMsg);

// Lettura asimmetrica Lock-Free / Zero-Copy per singolo lettore dedicato
const struct DataChannelParcel* RealTimeIPC_Top(const TelemetryQueue* cb);
BOOLEAN TelemetryQ_Pop(TelemetryQueue* cb);



#endif //MALAMORE_TELEMETRYQUEUE_H