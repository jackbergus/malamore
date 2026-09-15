//
// Created by Giacomo Bergami, PhD on 15/09/2026.
//

#ifndef MALAMORE_DRIVER_UTILITY_H
#define MALAMORE_DRIVER_UTILITY_H

#include <ntddk.h>
#include <ntstrsafe.h>

static inline ULONG MakeDynamicTag(char c1, char c2, int d1, int d2, ULONG default_ = 'DFLT')
{
    CHAR tagBuffer[5]; // 4 characters + null terminator
    // Format two characters followed by two digits into the buffer
    // Example: c1='X', c2='Y', d1=5, d2=7 => "XY57"
    // Use RtlStringCchPrintf to safely format in kernel mode
    NTSTATUS status = RtlStringCchPrintfA(tagBuffer, sizeof(tagBuffer), "%c%c%d%d", c1, c2, d1, d2);
    if (!NT_SUCCESS(status))
    {
        return default_;
    }
    // Construct ULONG tag by copying each byte
    ULONG tag = ((ULONG)tagBuffer[0])       |
                ((ULONG)tagBuffer[1] << 8) |
                ((ULONG)tagBuffer[2] << 16)|
                ((ULONG)tagBuffer[3] << 24);
    return tag;
}

#endif //MALAMORE_DRIVER_UTILITY_H