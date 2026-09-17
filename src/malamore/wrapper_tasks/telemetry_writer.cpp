//
// Created by giaco on 16/09/2026.
//

#include <malamore/wrapper_tasks/telemetry_writer.h>

#include "malamore/threads/thread_pool.h"

VOID TelemetrySink(struct UnifiedThreadContext *Context, struct FutureResultParcel *OutResult) {
    UNREFERENCED_PARAMETER(Context);
    LARGE_INTEGER nextPeriodOfTime, currentTime;

    INIT_SYNCH_FROM_CONTEXT(&nextPeriodOfTime, Context, periodTicks);
    // THREAD_PERIOD_LOOP_UNTIL_ALIVE(&nextPeriodOfTime, &currentTime, Context, Timestamp);
    while ((&nextPeriodOfTime) && (&currentTime) && (Context) && KernelAtomicRead(
           &((struct UnifiedThreadContext *) Context)->KeepRunning) == 1) {
        do {
            _mm_pause();
            *((LARGE_INTEGER *) &currentTime) = KeQueryPerformanceCounter(NULL);
        } while ((((LARGE_INTEGER *) &currentTime)->QuadPart < ((LARGE_INTEGER *) &nextPeriodOfTime)->QuadPart) && (
                     KernelAtomicRead(&((struct UnifiedThreadContext *) Context)->KeepRunning) == 1));
        if (KernelAtomicRead(&((struct UnifiedThreadContext *) Context)->KeepRunning) == 0) { break; }
        const ULONG64 Timestamp = (((LARGE_INTEGER *) &currentTime)->QuadPart * 1000000ULL) / ((struct UnifiedThreadContext
                                      *) Context)->PerfCounterFrequency;
        if (isTelemetryQueueEmpty()) {
            // Se la coda è vuota, rilascia temporaneamente la CPU con un piccolo stallo preventivo
            LARGE_INTEGER interval;
            LARGE_INTEGER tmp = KeQueryPerformanceCounter(NULL);
            if (tmp.QuadPart + 10 < (nextPeriodOfTime.QuadPart + periodTicks)) {
                interval.QuadPart = nextPeriodOfTime.QuadPart + periodTicks- tmp.QuadPart;
                // Waiting for some time, after the sampling interval.
                KeDelayExecutionThread(KernelMode, FALSE, &interval);
            }
            continue;
        }
        dumpBinaryTelemetryToFile();
        if (&nextPeriodOfTime)
            ((LARGE_INTEGER *) &nextPeriodOfTime)->QuadPart += ((ULONG64) periodTicks);
    }
}