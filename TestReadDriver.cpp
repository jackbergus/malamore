//
// Created by Giacomo Bergami on 16/09/2026.
//

#include "malamore/utility/StdStream.h"

/**
 * This main function is mainly done to check whether I can read something from the stream.
 * It only makes sense reading
 */
int main(void) {
    MalamoreIStream stream;
    MalamorePath path(L"\\\\.\\FSCC0");

    if (NT_SUCCESS(stream.Open(path, StreamFlags::Read | StreamFlags::Write))) {
        char buffer[1024];
        ULONG letti = 0;

        // Caso A: Lettura deterministica periodica con timeout (Es. per i 3.33ms del loop hardware)
        NTSTATUS statusTimed = stream.ReadWithTimeout(buffer, sizeof(buffer), 500, &letti);
        KdPrint(("STATUS #1: %s\n", NtStatusToString(statusTimed)));

        // Caso B: Lettura bloccante su un canale asincrono (Es. attesa di un comando di configurazione)
        // Il thread si addormenta in totale sicurezza a PASSIVE_LEVEL finché i dati non arrivano
        NTSTATUS statusBlock = stream.ReadBlocking(buffer, sizeof(buffer), &letti);
        KdPrint(("STATUS #1: %s\n", NtStatusToString(statusBlock)));

        stream.Close();
    } else {
        KdPrint(("Cannot open device! Something fishy is happening"));
    }
}
