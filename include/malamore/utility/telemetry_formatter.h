//
// Created by Giacomo Bergami on 15/09/2026.
//

#ifndef MALAMORE_TELEMETRY_FORMATTER_H
#define MALAMORE_TELEMETRY_FORMATTER_H


#include <malamore/utility/kernel_compatibility.h>

/**
 * Verifica se il buffer contiene esclusivamente caratteri ASCII stampabili
 */

BOOLEAN IsBufferPrintable(const UCHAR* buffer, ULONG length);

/**
 * Converte il timestamp hardware a 64-bit nel formato stringa ISO richiesto:
 * YYYY-MM-DD HH:MM:SS.NNNNNNNNN
 */

// NTSTATUS FormatKernelTimestamp(ULONG64 kernelTimestamp, char* outBuffer, ULONG maxCch);

// ==========================================
// 1. GET SYSTEM BOOT TIME (Ring-Dependent)
// ==========================================
ULONGLONG UnifiedGetSystemBootTimeMs();


struct QpcSnapshot {
    ULONGLONG bootTimeMsSinceEpoch;
    LARGE_INTEGER qpcTicks;
    LARGE_INTEGER qpcFrequency;
};

// Funzione Ring 0 (Kernel) per ricostruire e formattare il tempo da un tick successivo
NTSTATUS ReconstructFromSubsequentTickKernel(const QpcSnapshot* baseSnapshot, LONGLONG currentTick, char* outBuffer, size_t bufferSize);


BOOLEAN fillInSnapshot(struct QpcSnapshot* datum);

#endif //MALAMORE_TELEMETRY_FORMATTER_H
