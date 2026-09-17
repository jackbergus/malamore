//
// Created by Giacomo Bergami on 15/09/2026.
//

#include "malamore/wrapper_tasks/hardware_reader.h"

#include "malamore/threads/thread_pool.h"
#include "malamore/threads/thread_shared.h"
#include "malamore/utility/streambuf/MalamoreIStream.h"

void HardwareReaderThreadRoutine(struct UnifiedThreadContext* Context, struct FutureResultParcel* ) {
    if (!Context) {
        KdPrint(("HardwareReaderContext] Invalid configuration (nullptr)\n"));
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
        return;
    }

    NTSTATUS status;
    ULONG bytesActuallyRead;

    LARGE_INTEGER freq;
    KeQueryPerformanceCounter(&freq);
    // const ULONG64 periodTicks = (static_cast<ULONG64>(Context->PeriodUs) * Context->PerfCounterFrequency) / 1000000ULL;

    LARGE_INTEGER currentTime, nextPeriodTime;
    GROUP_AFFINITY targetAffinity, previousAffinity;
    ULONG64 sequence = 0;

    // 1. PINNING DEL CORE FISICO
    RtlZeroMemory(&targetAffinity, sizeof(GROUP_AFFINITY));
    targetAffinity.Mask = Context->AffinityMask;
    targetAffinity.Group = 0;
    KeSetSystemGroupAffinityThread(&targetAffinity, &previousAffinity);

    // =========================================================================
    // STEP 1: APERTURA DINAMICA (ACCETTA SIA DRIVERS CHE FILE NEL FILE SYSTEM)
    // =========================================================================
    MalamoreIStream driver;
    status = driver.Open(Context->DevicePath, StreamFlags::Read | StreamFlags::Write);

    if (!NT_SUCCESS(status)) {

        KdPrint(("[MALAMORE 🚨] Impossibile aprire la risorsa '%ws'. Errore: %s\n", Context->DevicePath, NtStatusToString(status)));
        KeRevertToUserGroupAffinityThread(&previousAffinity);
        PsTerminateSystemThread(status);
        return;
    }

    // Allineamento temporale iniziale sulla barriera comune pre-calcolata
    INIT_SYNCH_FROM_CONTEXT(&nextPeriodTime, Context, periodTicks);  //nextPeriodTime.QuadPart = Context->SynchronizedStartTicks;
    UCHAR idata[HARDWARE_PAYLOAD_SIZE];

    // =========================================================================
    // LOOP PRINCIPALE DI LETTURA PERIODICA ASINCRONA (PASSIVE_LEVEL - 3.33ms)
    // =========================================================================
    while ((&nextPeriodTime) && (&currentTime) && (Context) && KernelAtomicRead(
               &((struct UnifiedThreadContext *) Context)->KeepRunning) == 1) {
        do {
            _mm_pause();
            *((LARGE_INTEGER *) &currentTime) = KeQueryPerformanceCounter(0);
        } while ((((LARGE_INTEGER *) &currentTime)->QuadPart < ((LARGE_INTEGER *) &nextPeriodTime)->QuadPart) && (
                     KernelAtomicRead(&((struct UnifiedThreadContext *) Context)->KeepRunning) == 1));
        if (KernelAtomicRead(&((struct UnifiedThreadContext *) Context)->KeepRunning) == 0) {
            break;
        }
        RtlZeroMemory(idata, HARDWARE_PAYLOAD_SIZE);
        // CONFIGURAZIONE DEL TIMEOUT RIGIDO DEL KERNEL
        status = driver.ReadWithTimeout(idata, sizeof(idata), Context->MaxBudgetUs, &bytesActuallyRead);
        if (status == STATUS_SUCCESS) {
            const ULONG64 Timestamp = (((LARGE_INTEGER *) &currentTime)->QuadPart * 1000000ULL) / ((struct UnifiedThreadContext
                              *) Context)->PerfCounterFrequency;
            status = CorePool_RouteMessageSrcToDest(Timestamp, HARDWARE_READER_ID, Context->TargetThreadId, idata, bytesActuallyRead);
            if (!NT_SUCCESS(status)) {
                KdPrint(("There was an issue while attempting to send the response at time %llu\n", Timestamp));
            }
        }
        else if (status == STATUS_TIMEOUT) {
            // Timeout, continuing to navigate the system
        } else {
            KdPrint(("ERROR: there was an error on the driver state, that does not allow me to continue reading"));
        }
        if (&nextPeriodTime)
            ((LARGE_INTEGER *) &nextPeriodTime)->QuadPart += ((ULONG64) periodTicks);
    }

    driver.Close();
    KeRevertToUserGroupAffinityThread(&previousAffinity);
}
