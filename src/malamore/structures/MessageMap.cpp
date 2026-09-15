//
// Created by Giacomo Bergami on 15/09/2026.
//

#include <malamore/structures/MessageMap.h>
#include <ntddk.h>

// #include <cstring> // Utilizzo delle funzioni standard per l'azzeramento e copia della memoria

void MessageMap_Init(struct MessageMap* cb) {
    if (cb != nullptr) {
        cb->Size = 0;
        cb->Head = 0;
        cb->Tail = 0;
        RtlZeroMemory(static_cast<void*>(cb->Nodes), sizeof(cb->Nodes));
        // std::memset(cb->Nodes, 0, sizeof(cb->Nodes));
    }
}

BOOLEAN MessageMap_IsFull(const struct MessageMap* cb) {
    return (cb != nullptr && cb->Size == IN_MSG_BUFFER_SIZE) ? TRUE : FALSE;
}

BOOLEAN MessageMap_IsEmpty(const struct MessageMap* cb) {
    return (cb != nullptr && cb->Size == 0) ? TRUE : FALSE;
}

BOOLEAN MessageMap_Push(struct MessageMap* cb, ULONG Other, PCHAR Buffer, ULONG BUFLEN) {
    if (cb == nullptr) return FALSE;
    if (MessageMap_IsFull(cb)) return FALSE;
    if (Buffer==NULL) {
        BUFLEN = 0;
    }
    if (BUFLEN > MSG_BUFFER_SIZE) {
        BUFLEN = MSG_BUFFER_SIZE;
    }

    // Copia della struttura dati nella posizione Tail
    cb->Nodes[cb->Tail].Other = Other;
    cb->Nodes[cb->Tail].Dimension = BUFLEN;
    RtlCopyMemory(cb->Nodes[cb->Tail].Buffer, Buffer, BUFLEN);

    // Avanzamento dell'indice Tail in modo circolare
    cb->Tail = (cb->Tail + 1) % IN_MSG_BUFFER_SIZE;
    cb->Size++;

    return TRUE;
}

const struct MSG2* MessageMap_Top(const struct MessageMap* cb) {
    if (cb == nullptr || cb->Size == 0) return nullptr;

    // Ritorna direttamente l'indirizzo di memoria del primo elemento (Zero-Copy)
    return &cb->Nodes[cb->Head];
}

BOOLEAN MessageMap_Pop(struct MessageMap* cb) {
    if (cb == nullptr || cb->Size == 0) return FALSE;

    // Riqualifica l'elemento come rimosso modificando solo l'indice (Nessuna copia eseguita)
    cb->Head = (cb->Head + 1) % IN_MSG_BUFFER_SIZE;
    cb->Size--;

    return TRUE;
}
