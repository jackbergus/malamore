//
// Created by giaco on 16/09/2026.
//

#include <malamore/wrapper_tasks/telemetry_writer.h>

#include "malamore/threads/thread_pool.h"

VOID TelemetrySink(struct UnifiedThreadContext *Context, struct FutureResultParcel *OutResult) {
    UNREFERENCED_PARAMETER(Context);
    IO_STATUS_BLOCK ioStatus;
    UNREFERENCED_PARAMETER(Context);
    NTSTATUS status;
    LARGE_INTEGER nextPeriodOfTime, currentTime;

    // Allocazione fissa sullo stack del thread (Massima velocità a DISPATCH/PASSIVE, no heap)
    char timeString[80];
    char logLine[8064];
    RtlZeroMemory(timeString, sizeof(timeString));
    RtlZeroMemory(logLine, sizeof(logLine));

    INIT_SYNCH_FROM_CONTEXT(&nextPeriodOfTime, Context, periodTicks);

    THREAD_PERIOD_LOOP_UNTIL_ALIVE(&nextPeriodOfTime, &currentTime, Context, Timestamp);
        if (isTelemetryQueueEmpty()) {
            // Se la coda è vuota, rilascia temporaneamente la CPU con un piccolo stallo preventivo
            LARGE_INTEGER interval;
            interval.QuadPart = -100 * 1000; // 10 millisecondi di sleep relativo
            KeDelayExecutionThread(KernelMode, FALSE, &interval);
            continue;
        }


    THREAD_NEXT_PERIOD_AFTER_TICKS(&nextPeriodOfTime, periodTicks);

    while ((KernelAtomicRead(&Context->KeepRunning) == 1) || (!isTelemetryQueueEmpty())) {


        struct DataChannelParcel* msg = RealTimeIPC_Top(&g_PoolRegistry.TelemetryChannel);

        // 1. Formatta il timestamp nella stringa cronologica richiesta
        status = FormatKernelTimestamp(msg->Timestamp, timeString, sizeof(timeString));
        if (!NT_SUCCESS(status)) {
            KdPrint(("[MALAMORE 🚨] Error printing the time string\n"));
            InterlockedOr(&g_PoolRegistry.TerminateDiskThread, 1);
        } else {
            // 2. DISCRIMINAZIONE DEL PAYLOAD: VERIFICA SE STAMPABILE O BINARIO
            BOOLEAN isPrintable = IsBufferPrintable(msg->PayloadData, msg->DataLength);
            if (isPrintable) {
                // CASO A: Contenuto stampabile -> Formato testo quotato con salto riga
                // Intestazione fissa dell'output: [Node=X, Time=YYYY-MM-DD HH:MM:SS.NNNNNNNNN] "content"\n
                status = RtlStringCchPrintfA(
                    logLine, MAX_LOG_LINE_SIZE,
                    "[Node=%lu, Time=%s] \"%.*s\"\r\n",
                    msg->SourceNodeId, timeString, msg->PayloadData
                );
            } else {
                // CASO B: Contenuto binario/non stampabile -> Rappresentazione Esadecimale (Hex Number)
                // Prepariamo l'intestazione fissa iniziale della stringa di log
                status = RtlStringCchPrintfA(
                    logLine, MAX_LOG_LINE_SIZE,
                    "[Node=%lu, Time=%s] ",
                    msg->SourceNodeId, timeString
                );
            }
            if (!NT_SUCCESS(status)) {
                KdPrint(("[MALAMORE 🚨] Error printing the info part string\n"));
                InterlockedOr(&g_PoolRegistry.TerminateDiskThread, 1);
            } else {
                if (!isPrintable) {
                    // Calcoliamo lo spazio rimanente nella stringa dopo l'intestazione fissa
                    size_t currentLength = 0;
                    RtlStringCchLengthA(logLine, MAX_LOG_LINE_SIZE, &currentLength);

                    char* hexAppendPtr = logLine + currentLength;
                    size_t remainingCch = MAX_LOG_LINE_SIZE - currentLength;

                    // Appendiamo sequenzialmente la rappresentazione esadecimale di ogni singolo byte
                    for (ULONG i = 0; i < MAX_LOG_LINE_SIZE && remainingCch > 4; ++i) {
                        status = RtlStringCchPrintfA(hexAppendPtr, remainingCch, "%02X", buffer[i]);
                        if (!NT_SUCCESS(status)) {
                            KdPrint(("[MALAMORE 🚨] Error appending the hexadecimal part\n"));
                            break;
                        }

                        hexAppendPtr += 2;
                        remainingCch -= 2;
                    }
                    if (!NT_SUCCESS(status)) {
                        continue;
                    }

                    // Aggiungiamo la terminazione del ritorno a capo richiesto per lo stream del file log
                    status = RtlStringCchPrintfA(hexAppendPtr, remainingCch, "\r\n");
                    if (!NT_SUCCESS(status)) {
                        KdPrint(("[MALAMORE 🚨] Cannot append the terminating string, continuing\n"));
                        continue;
                    }
                }
                if (g_PoolRegistry.hFileHandle != NULL) {
                    // 3. SCRITTURA EFFETTIVA E LINEARE SUL FILE SINK DI RING 0
                    size_t finalLineLength = 0;
                    status = RtlStringCchLengthA(logLine, MAX_LOG_LINE_SIZE, &finalLineLength);
                    if (!NT_SUCCESS(status)) {
                        KdPrint(("[MALAMORE 🚨] Cannot determine the string lenth, for some odd reason, continuing\n"));
                        continue;
                    }

                    // Scrittura sincrona protetta: operando a PASSIVE_LEVEL nel DiskWriterThreadRoutine,
                    // l'I/O non genera alcuna violazione di contesto o stallo catastrofico.
                    status = ZwWriteFile(
                        hFileHandle,
                        NULL, NULL, NULL, // Event, ApcRoutine, ApcContext
                        &ioStatus,
                        logLine,          // Il buffer di testo formattato condizionalmente
                        static_cast<ULONG>(finalLineLength),
                        NULL, NULL        // ByteOffset, Key
                    );
                } else {
                    KdPrint(("[MALAMORE 🚨] The file handler for the telemetry task is cleared, I cannot write.\n"));
                    continue;
                }
            }
        }
    }
}