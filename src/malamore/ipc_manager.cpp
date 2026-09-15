#include "malamore/ipc_manager.h"
#include <ntstatus.h>

NTSTATUS KernelBufferChannelWrite(struct InterProcessChannelData* channelData,
                                  const void* buffer,
                                  ULONG count) {
    if (!channelData || count > 256) return STATUS_INVALID_PARAMETER;

    // Controllo atomico dello spazio nel Pool Real-time
    if (channelData->MessageCount >= 10) return STATUS_INSUFFICIENT_RESOURCES;

    LONG currentWrite = channelData->WriteIndex;
    UCHAR* target = &channelData->Buffer[(currentWrite * 256) % CHANNEL_BUFFER_SIZE];

    RtlCopyMemory(target, buffer, count);

    // Scrittura atomica a Ring 0
    InterlockedExchange(&channelData->WriteIndex, (currentWrite + 1) % 10);
    InterlockedIncrement(&channelData->MessageCount);

    return STATUS_SUCCESS;
}

NTSTATUS KernelBufferChannelRead(struct InterProcessChannelData* channelData,
                                void* outBuffer,
                                ULONG maxCount) {
    if (!channelData || channelData->MessageCount == 0) return STATUS_NO_DATA_DETECTED;

    LONG currentRead = channelData->ReadIndex;
    UCHAR* source = &channelData->Buffer[(currentRead * 256) % CHANNEL_BUFFER_SIZE];

    ULONG toCopy = (maxCount < 256) ? maxCount : 256;
    RtlCopyMemory(outBuffer, source, toCopy);

    // Avanzamento atomico dell'indice di lettura hardware
    InterlockedExchange(&channelData->ReadIndex, (currentRead + 1) % 10);
    InterlockedDecrement(&channelData->MessageCount);

    return STATUS_SUCCESS;
}
