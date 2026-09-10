#pragma once

#ifdef LINK_KERNEL
#include <ntddk.h>
#else
#include <windows.h>
#endif

#define MY_DEVICE_TYPE 0x8000
#define IOCTL_MAP_SHARED_MEMORY CTL_CODE(MY_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define EVENT_NAME_SERVER L"\\BaseNamedObjects\\KernelBinderServerEvent"
#define EVENT_NAME_CLIENT L"\\BaseNamedObjects\\KernelBinderClientEvent"

#pragma pack(push, 1)
struct SharedDataBlock {
    volatile ULONG TaskReady;
    volatile ULONG TaskCompleted;
    volatile ULONG ExitSignal;
    char DllPath[260];
    ULONG DataLength;
    UCHAR Payload[1024]; // Dimensione fissa a array di byte
};
#pragma pack(pop)
