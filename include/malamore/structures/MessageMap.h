//
// Created by Giacomo Bergami on 15/09/2026.
//


#include <windows.h>

#ifndef MALAMORE_MESSAGEMAP_H
#define MALAMORE_MESSAGEMAP_H


#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

// Rende le funzioni linkabili sia in contesti C che C++ senza alterazione dei nomi


#define MSG_BUFFER_SIZE    1024
#define IN_MSG_BUFFER_SIZE 256

    // Struttura dati originale stile iovec Linux compatibile con il NonPagedPool
    struct MSG2 {
        ULONG Other; // If part of an output message, this is the destination. If part of an input message, this is the sender info
        // ULONG Dest;
        CHAR Buffer[MSG_BUFFER_SIZE]; // Puntatore alla RAM del Pool non-pagabile
        ULONG Dimension;               // Lunghezza effettiva dei byte
    };

    // Struttura del buffer circolare estesa con gli indici di gestione
    struct MessageMap {
        ULONG Size;                            // Contatore elementi attivi
        struct MSG2 Nodes[IN_MSG_BUFFER_SIZE]; // Array circolare fisso
        ULONG Head;                            // Indice di lettura (prossima Top/Pop)
        ULONG Tail;                            // Indice di scrittura (prossima Push)
    };

#ifdef __cplusplus
extern "C" {
#endif

    // API del Buffer Circolare
    void MessageMap_Init(struct MessageMap* cb);
    BOOLEAN MessageMap_IsFull(const struct MessageMap* cb);
    BOOLEAN MessageMap_IsEmpty(const struct MessageMap* cb);
    BOOLEAN MessageMap_Push(struct MessageMap* cb, ULONG Other, PCHAR Buffer, ULONG BUFLEN);

    // Interfaccia ridefinita in stile C (Zero-Copy)
    const struct MSG2* MessageMap_Top(const struct MessageMap* cb);
    BOOLEAN MessageMap_Pop(struct MessageMap* cb);

#ifdef __cplusplus
}
#endif


#endif // CIRCULAR_BUFFER_H


#endif //MALAMORE_MESSAGEMAP_H