#pragma once
#include <cmath>

#ifdef LINK_KERNEL
#include <ntddk.h>
#include <ntdef.h>
#else
#include <windows.h>
#endif

#define MAX_PAYLOAD_SIZE                    (1024)
#define MAX_NODES_IN_SYSTEM                 (16)

// =========================================================================
// DEFINIZIONE COSTANTI DEI COMANDI BINDER A RING 0 (MAPPATE SU ULONG)
// =========================================================================
#define CMD_SPAWN_NODE                      (1UL)
#define CMD_CONNECT_NODES                   (2UL)
#define CMD_SIGNAL_NODE                     (3UL)
#define CMD_DESTROY_NODE                    (4UL)
#define CMD_REGISTER_PERIODIC_TASK          (5UL)

#define STATUS_BINDER_OK                    (0L)
#define STATUS_BINDER_CORE_EXHAUSTED        (-1L)
#define STATUS_BINDER_ERROR                 (-2L)
#define MSG_BUFFER_SIZE                     (1024)
#define CHANNEL_BUFFER_SIZE                 (4096)
#define IN_MSG_BUFFER_SIZE                  (128)

// Struttura dati stile iovec Linux compatibile con il NonPagedPool
struct MSG2 {
    ULONG Mitt;
    ULONG Dest;
    PCHAR Buffer[MSG_BUFFER_SIZE];       // Puntatore alla RAM del Pool non-pagabile
    ULONG Dimension;    // Lunghezza effettiva dei byte
};

struct MessageMap {
    ULONG Size;
    struct MSG2 Nodes[IN_MSG_BUFFER_SIZE];
};

/**
 * @param ptr Pointer to the map to consider
 * @return True whether the initialization was effective, False otherwise
 */
static inline BOOLEAN clear_map(MessageMap* ptr) {
    if (!ptr) {
        return FALSE;
    }
    ptr->Size = 0;
    RtlZeroMemory(ptr->Nodes, sizeof(MSG2) * IN_MSG_BUFFER_SIZE); // Fill buffer with 0
    return TRUE;
}

/**
 * This function mainly adds a message to the buffer, so that it can be send elsewhere.
 */
static inline BOOLEAN add_message(MessageMap* ptr,
                                    const ULONG Mitt,
                                    const ULONG Dest,
                                    const PCHAR Buffer,
                                    const ULONG Dimension) {
    if (!ptr) {
        return FALSE;
    }
    if (ptr->Size >= IN_MSG_BUFFER_SIZE) {
        return FALSE;
    }
    struct MSG2* loc = &ptr->Nodes[ptr->Size];
    loc->Mitt = Mitt;
    loc->Dest = Dest;
    ULONG minDim = Dimension;
    if (minDim > MSG_BUFFER_SIZE) {
        minDim = MSG_BUFFER_SIZE;
    }
    RtlCopyMemory(loc->Buffer, Buffer, minDim); // Copy memory
    loc->Dimension = minDim;
    ptr->Size += 1;
    return TRUE;
}

#pragma pack(push, 1)
struct TaskRealtimeMeta {
    char TaskName;
    ULONG PeriodUs;
    ULONG EstimatedWcetUs;
};
#pragma pack(pop)

// Il pacchetto di transazione nativo del Kernel (Binder Parcel)
#pragma pack(push, 1)
struct BinderParcel {
    ULONG CommandType;   // Ora combacia nativamente con le macro 1UL, 2UL, 3UL...
    ULONG SourceNodeId;
    ULONG TargetNodeId;
    ULONG DataLength;
    LONG ResponseStatus;
    UCHAR PayloadData[MAX_PAYLOAD_SIZE];
    struct TaskRealtimeMeta TaskMeta;
};
#pragma pack(pop)


// Il Ring Buffer lock-free Real-time nel NonPagedPoolNx
#pragma pack(push, 1)
struct InterProcessChannelData {
    volatile LONG ReadIndex;
    volatile LONG WriteIndex;
    volatile LONG MessageCount;
    BinderParcel Buffer[CHANNEL_BUFFER_SIZE];
};
#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS KernelBufferChannelWrite(struct InterProcessChannelData* channelData, const void* buffer, ULONG count);
NTSTATUS KernelBufferChannelRead(struct InterProcessChannelData* channelData, void* outBuffer, ULONG maxCount);

#ifdef __cplusplus
}
#endif
