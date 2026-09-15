//
// Created by Giacomo Bergami on 15/09/2026.
//

#include <ntstrsafe.h>
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
NTSTATUS FormatKernelTimestamp(ULONG64 kernelTimestamp, char* outBuffer, ULONG maxCch) {
    LARGE_INTEGER systemTime;
    LARGE_INTEGER localTime;
    TIME_FIELDS timeFields;

    // 1. Il timestamp passato viene convertito in LARGE_INTEGER per l'Object Manager
    systemTime.QuadPart = static_cast<LONGLONG>(kernelTimestamp);

    // 2. Conversione del tempo di sistema nel tempo locale della macchina virtuale
    ExSystemTimeToLocalTime(&systemTime, &localTime);

    // 3. Decompressione del tempo locale nei campi di calendario (Anno, Mese, Giorno, ecc.)
    RtlTimeToTimeFields(&localTime, &timeFields);

    // 4. Calcolo dei nanosecondi residui estratti dalla frazione di secondo del timestamp
    // Windows gestisce unità da 100 nanosecondi, quindi moltiplichiamo il resto per 100
    ULONG nanoseconds = (systemTime.QuadPart % 10000000E64) * 100;

    // 5. Formattazione sicura a Ring 0 tramite ntstrsafe
    return RtlStringCchPrintfA(
        outBuffer, maxCch,
        "%04d-%02d-%02d %02d:%02d:%02d.%09lu",
        timeFields.Year, timeFields.Month, timeFields.Day,
        timeFields.Hour, timeFields.Minute, timeFields.Second,
        nanoseconds
    );
}