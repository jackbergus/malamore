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

NTSTATUS FormatKernelTimestamp(ULONG64 kernelTimestamp, char* outBuffer, ULONG maxCch);

/*
#include <ntddk.h>
#include <ntstrsafe.h>*/

struct QpcSnapshot {
    ULONGLONG bootTimeMsSinceEpoch; 
    LARGE_INTEGER qpcTicks;         
    LARGE_INTEGER qpcFrequency;     
};

// Funzione Ring 0 (Kernel) per ricostruire e formattare il tempo da un tick successivo
NTSTATUS ReconstructFromSubsequentTickKernel(const QpcSnapshot* baseSnapshot, LARGE_INTEGER currentTick, char* outBuffer, size_t bufferSize) {
    if (!baseSnapshot || !outBuffer || bufferSize == 0) return STATUS_INVALID_PARAMETER;

    // 1. Calcola il delta dei tick rispetto allo snapshot iniziale
    LONGLONG deltaTicks = currentTick.QuadPart - baseSnapshot->qpcTicks.QuadPart;

    // 2. Converti il delta dei tick in secondi totali (con virgola per non perdere precisione)
    double deltaSeconds = (double)deltaTicks / (double)baseSnapshot->qpcFrequency.QuadPart;
    
    // Calcoliamo i millisecondi totali e teniamo il resto in nanosecondi per la stringa
    LONGLONG deltaMs = (LONGLONG)(deltaSeconds * 1000.0);
    LONGLONG deltaNsRemainder = (LONGLONG)((deltaSeconds * 1000000000.0) - (deltaMs * 1000000.0));

    // 3. Calcola il tempo assoluto iniziale in millisecondi (Boot Time + Tick Iniziale)
    double baseSeconds = (double)baseSnapshot->qpcTicks.QuadPart / (double)baseSnapshot->qpcFrequency.QuadPart;
    LONGLONG baseMs = (LONGLONG)(baseSeconds * 1000.0);
    LONGLONG baseNsRemainder = (LONGLONG)((baseSeconds * 1000000000.0) - (baseMs * 1000000.0));
    
    LONGLONG baseTotalMs = baseSnapshot->bootTimeMsSinceEpoch + baseMs;

    // 4. Somma il tempo base e il delta corrente
    LONGLONG finalTotalMs = baseTotalMs + deltaMs;
    LONGLONG finalNsRemainder = baseNsRemainder + deltaNsRemainder;
    
    // Gestione dell'eventuale overflow dei nanosecondi nei millisecondi
    if (finalNsRemainder >= 1000000) {
        finalTotalMs += (finalNsRemainder / 1000000);
        finalNsRemainder = finalNsRemainder % 1000000;
    }

    // 5. Converti i millisecondi totali nel formato FILETIME di Windows (intervalli da 100-ns dal 1601)
    // 116444736000000000ULL è la costante per passare da Unix Epoch a Windows Epoch
    LONGLONG windowsNs100 = (finalTotalMs * 10000) + 116444736000000000ULL;
    LARGE_INTEGER rawTime;
    rawTime.QuadPart = windowsNs100;

    // Se preferisci l'ora locale rispetto a UTC, scommenta la riga sotto:
    // LARGE_INTEGER localTime;
    // ExSystemTimeToLocalTime(&rawTime, &localTime);
    // rawTime = localTime;

    // 6. Scomponi il tempo nelle strutture Year/Month/Day Hour:Minute:Second
    TIME_FIELDS tf;
    RtlTimeToTimeFields(&rawTime, &tf);

    // I nanosecondi finali nel formato finale uniscono il raggruppamento dei millisecondi e il resto rimasto
    LONGLONG totalNanosecondsFraction = (tf.Milliseconds * 1000000) + finalNsRemainder;

    // 7. Scrittura sicura nel buffer stringa (Ring 0)
    return RtlStringCchPrintfA(outBuffer, bufferSize, 
        "%04d/%02d/%02d %02d:%02d:%02d.%09lld",
        tf.Year, tf.Month, tf.Day, tf.Hour, tf.Minute, tf.Second, totalNanosecondsFraction);
}



#endif //MALAMORE_TELEMETRY_FORMATTER_H
