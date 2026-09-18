//
// Created by Giacomo Bergami on 15/09/2026.
//

// #include <ntstrsafe.h>
#include <malamore/utility/telemetry_formatter.h>

/**
 * Verifica se il buffer contiene esclusivamente caratteri ASCII stampabili
 */
BOOLEAN IsBufferPrintable(const UCHAR* buffer, ULONG length) {
    if (buffer == NULL || length == 0) return FALSE;

    for (ULONG i = 0; i < length; ++i) {
        UCHAR c = buffer[i];

        // Controlla se il carattere è stampabile (ASCII 32 - 126)
        // o se fa parte dei caratteri di controllo di spaziatura standard
        if (!((c >= 0x20 && c <= 0x7E) || c == '\n' || c == '\r' || c == '\t' || c == '\0')) {
            return FALSE; // Trovato carattere binario/invisibile
        }
    }
    return TRUE;
}

/**
 * Converte il timestamp hardware a 64-bit nel formato stringa ISO richiesto:
 * YYYY-MM-DD HH:MM:SS.NNNNNNNNN
 */

ULONGLONG UnifiedGetSystemBootTimeMs() {
    // --- RING 0 (KERNEL MODE) ---
    LARGE_INTEGER systemTime;
    ULONGLONG interruptTime;

    // Get exact system wall-clock (UTC) in 100-ns intervals since 1601
    KeQuerySystemTimePrecise(&systemTime);

    // Get system uptime in 100-ns intervals (ignores sleep states, matching QPC behavior)
    interruptTime = KeQueryInterruptTime();

    // Boot Time = Current System Time - Uptime
    ULONGLONG bootTimeNs100 = systemTime.QuadPart - interruptTime;

    // Convert Windows Epoch (1601) to Unix Epoch (1970)
    // 116444736000000000 = 100-ns intervals between 1601 and 1970
    ULONGLONG unixBootTimeNs100 = bootTimeNs100 - 116444736000000000ULL;

    return unixBootTimeNs100 / 10000; // Convert 100-ns to milliseconds
}


NTSTATUS ReconstructFromSubsequentTickKernel(const QpcSnapshot* baseSnapshot, LONGLONG currentTick, char* outBuffer, size_t bufferSize) {
    if (!baseSnapshot || !outBuffer || bufferSize == 0) return STATUS_INVALID_PARAMETER;

    // 1. Calcola il delta dei tick rispetto allo snapshot iniziale
    LONGLONG deltaTicks = currentTick - baseSnapshot->qpcTicks.QuadPart;
    LONGLONG freq = baseSnapshot->qpcFrequency.QuadPart;

    // 2. Calcola i secondi interi e il resto dei tick
    LONGLONG deltaSeconds = deltaTicks / freq;
    LONGLONG remainderTicks = deltaTicks % freq;

    // 3. Converti il resto dei tick in nanosecondi usando aritmetica intera
    // Moltiplichiamo prima per 1.000.000.000 per mantenere la massima precisione
    LONGLONG deltaNanosecondsFraction = (remainderTicks * 1000000000LL) / freq;

    // 4. Calcola il tempo dello snapshot iniziale (Boot Time + Tick Iniziale)
    LONGLONG baseTicks = baseSnapshot->qpcTicks.QuadPart;
    LONGLONG baseSeconds = baseTicks / freq;
    LONGLONG baseRemainderTicks = baseTicks % freq;
    LONGLONG baseNanosecondsFraction = (baseRemainderTicks * 1000000000LL) / freq;

    // 5. Somma i tempi (Tempo Base + Delta Corrente)
    LONGLONG totalSecondsSinceEpoch = (baseSnapshot->bootTimeMsSinceEpoch / 1000) + baseSeconds + deltaSeconds;
    LONGLONG totalNanosecondsFraction = (baseSnapshot->bootTimeMsSinceEpoch % 1000 * 1000000) + baseNanosecondsFraction + deltaNanosecondsFraction;

    // Gestione dell'overflow dei nanosecondi nei secondi interi
    if (totalNanosecondsFraction >= 1000000000LL) {
        totalSecondsSinceEpoch += (totalNanosecondsFraction / 1000000000LL);
        totalNanosecondsFraction = totalNanosecondsFraction % 1000000000LL;
    }

    // 6. Converti il tempo totale in formato FILETIME di Windows (intervalli da 100-ns dal 1601)
    // 116444736000000000ULL è la costante Unix Epoch -> Windows Epoch
    LONGLONG windowsNs100 = (totalSecondsSinceEpoch * 10000000LL) + (totalNanosecondsFraction / 100) + 116444736000000000ULL;

    LARGE_INTEGER rawTime;
    rawTime.QuadPart = windowsNs100;

    // 7. Scomponi il tempo nelle strutture Year/Month/Day Hour:Minute:Second
    TIME_FIELDS tf;
    RtlTimeToTimeFields(&rawTime, &tf);

    // I nanosecondi finali da stampare sono la frazione calcolata originariamente (fino a 9 cifre)
    // Ripristiniamo la precisione esatta sotto i 100ns usando l'ultimo resto
    LONGLONG exactNanoseconds = (totalNanosecondsFraction / 100) * 100 + (totalNanosecondsFraction % 100);

    // 8. Scrittura sicura nel buffer stringa (Ring 0)
    return RtlStringCchPrintfA(outBuffer, bufferSize,
        "%04d/%02d/%02d %02d:%02d:%02d.%09lld",
        tf.Year, tf.Month, tf.Day, tf.Hour, tf.Minute, tf.Second, exactNanoseconds);
}

BOOLEAN fillInSnapshot(struct QpcSnapshot *datum) {
    if (!datum) {
        return FALSE;
    }

    // --- PART 1: Collect Data ---
    // Capture boot time before/during collection
    ULONGLONG bootTimeMs = UnifiedGetSystemBootTimeMs();

    // Query the frequency (constant per boot, but good to store)
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);

    // Capture the target ticks (e.g., your event profile tick)
    LARGE_INTEGER targetTicks;
    QueryPerformanceCounter(&targetTicks);

    // Simulate saving this data to a file/database and loading it back later
    datum->bootTimeMsSinceEpoch = bootTimeMs;
    datum->qpcTicks = targetTicks;
    datum->qpcFrequency = frequency;
    return TRUE;
}

// NTSTATUS FormatKernelTimestamp(ULONG64 kernelTimestamp, char* outBuffer, ULONG maxCch) {
//     LARGE_INTEGER systemTime;
//     LARGE_INTEGER localTime;
//     TIME_FIELDS timeFields;
//
//     // 1. Il timestamp passato viene convertito in LARGE_INTEGER per l'Object Manager
//     systemTime.QuadPart = static_cast<LONGLONG>(kernelTimestamp);
//
//     // 2. Conversione del tempo di sistema nel tempo locale della macchina virtuale
//     ExSystemTimeToLocalTime(&systemTime, &localTime);
//
//     // 3. Decompressione del tempo locale nei campi di calendario (Anno, Mese, Giorno, ecc.)
//     RtlTimeToTimeFields(&localTime, &timeFields);
//
//     // 4. Calcolo dei nanosecondi residui estratti dalla frazione di secondo del timestamp
//     // Windows gestisce unità da 100 nanosecondi, quindi moltiplichiamo il resto per 100
//     ULONG nanoseconds = (systemTime.QuadPart % 10000000E64) * 100;
//
//     // 5. Formattazione sicura a Ring 0 tramite ntstrsafe
//     return RtlStringCchPrintfA(
//         outBuffer, maxCch,
//         "%04d-%02d-%02d %02d:%02d:%02d.%09lu",
//         timeFields.Year, timeFields.Month, timeFields.Day,
//         timeFields.Hour, timeFields.Minute, timeFields.Second,
//         nanoseconds
//     );
// }

