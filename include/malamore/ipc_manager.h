#pragma once

#ifdef LINK_KERNEL
#include <ntddk.h>
#else
#include <windows.h>
#endif

#define MAX_PAYLOAD_SIZE 1024
#define MAX_NODES_IN_SYSTEM 16

// =========================================================================
// DEFINIZIONE COSTANTI DEI COMANDI BINDER A RING 0 (MAPPATE SU ULONG)
// =========================================================================
#define CMD_SPAWN_NODE             1UL
#define CMD_CONNECT_NODES          2UL
#define CMD_SIGNAL_NODE            3UL
#define CMD_DESTROY_NODE           4UL
#define CMD_REGISTER_PERIODIC_TASK 5UL

#define STATUS_BINDER_OK            0L
#define STATUS_BINDER_CORE_EXHAUSTED -1L
#define STATUS_BINDER_ERROR         -2L

// Struttura dati stile iovec Linux compatibile con il NonPagedPool
struct MSG2 {
    PVOID Buffer;       // Puntatore alla RAM del Pool non-pagabile
    ULONG Dimension;    // Lunghezza effettiva dei byte
};

struct MessageInputMap {
    struct MSG2 Nodes[MAX_NODES_IN_SYSTEM];
};

struct MessageOutputMap {
    struct MSG2 Nodes[MAX_NODES_IN_SYSTEM];
};

#pragma pack(push, 1)
struct TaskRealtimeMeta {
    char TaskName;
    ULONG PeriodUs;
    ULONG EstimatedWcetUs;
};

// Il pacchetto di transazione nativo del Kernel (Binder Parcel)
struct BinderParcel {
    ULONG CommandType;   // Ora combacia nativamente con le macro 1UL, 2UL, 3UL...
    ULONG SourceNodeId;
    ULONG TargetNodeId;
    ULONG DataLength;
    LONG ResponseStatus;
    UCHAR PayloadData[MAX_PAYLOAD_SIZE];
    struct TaskRealtimeMeta TaskMeta;
};

#define CHANNEL_BUFFER_SIZE 4096
// Il Ring Buffer lock-free Real-time nel NonPagedPoolNx
struct InterProcessChannelData {
    volatile LONG ReadIndex;
    volatile LONG WriteIndex;
    volatile LONG MessageCount;
    UCHAR Buffer[CHANNEL_BUFFER_SIZE];
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
