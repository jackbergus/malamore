//
// Created by Giacomo Bergami on 15/09/2026.
//

#ifndef MALAMORE_INTERPROCESSCHANNELDATA_H
#define MALAMORE_INTERPROCESSCHANNELDATA_H
// #include <ntdef.h>
#include <malamore/structures/GenericRingBuffer.h>
#include "malamore/structures/DataChannelParcel.h"


#ifndef MAX_PAYLOAD_SIZE
#define MAX_PAYLOAD_SIZE        (1024)
#endif

// Modifichiamo CHANNEL_BUFFER_SIZE per rappresentare il numero di SLOT del Ring Buffer.
// Deve tassativamente essere una potenza di 2 per consentire il mascheramento veloce bit-a-bit.
#ifndef CHANNEL_BUFFER_SIZE
    // 1. Istanza dei Canali di Comunicazione a Lunghezza Variabile (es. 16 o 32 Slot)
#define CHANNEL_BUFFER_SIZE                 (16)
#endif


    // Il canale IPC diventa:
    using InterProcessChannelData = GenericRingBuffer<struct DataChannelParcel, CHANNEL_BUFFER_SIZE>;

    // Dichiarazione delle API di sincronizzazione hardware del canale
    void RealTimeIPC_Init(InterProcessChannelData* cb);
    BOOLEAN RealTimeIPC_IsFull(const InterProcessChannelData* cb);
    BOOLEAN RealTimeIPC_IsEmpty(const InterProcessChannelData* cb);

    // Scrittura protetta per molteplici scrittori concorrenti a DISPATCH_LEVEL
    BOOLEAN RealTimeIPC_Push(InterProcessChannelData* cb, const struct DataChannelParcel* newMsg);
    BOOLEAN RealTimeIPC_Push(InterProcessChannelData* cb, ULONGLONG Timestamp, ULONG SourceNodeId, const UCHAR* PayloadData ,ULONG DataLength);

    // Lettura asimmetrica Lock-Free / Zero-Copy per singolo lettore dedicato
    const struct DataChannelParcel* RealTimeIPC_Top(const InterProcessChannelData* cb);
    BOOLEAN RealTimeIPC_Pop(InterProcessChannelData* cb);


#endif //MALAMORE_INTERPROCESSCHANNELDATA_H
