//
// Created by Giacomo Bergami on 15/09/2026.
//

#ifndef MALAMORE_TELEMETRY_FORMATTER_H
#define MALAMORE_TELEMETRY_FORMATTER_H

#ifdef LINK_KERNEL
#include <ntddk.h>
#else
#include <windows.h>
#endif

/**
 * Verifica se il buffer contiene esclusivamente caratteri ASCII stampabili
 */
BOOLEAN IsBufferPrintable(const UCHAR* buffer, ULONG length);

/**
 * Converte il timestamp hardware a 64-bit nel formato stringa ISO richiesto:
 * YYYY-MM-DD HH:MM:SS.NNNNNNNNN
 */
NTSTATUS FormatKernelTimestamp(ULONG64 kernelTimestamp, char* outBuffer, ULONG maxCch);

#endif //MALAMORE_TELEMETRY_FORMATTER_H