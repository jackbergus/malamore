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

    auto hProcess = GetCurrentProcess();
    auto hThread = GetCurrentThread();

    // 1. Elevate the entire process to REALTIME priority class.
    // NOTE: This requires the application to be run as Administrator.
    SetPriorityClass(hProcess, REALTIME_PRIORITY_CLASS);

    // 2. Set the thread priority to TIME_CRITICAL.
    // Combined with REALTIME_PRIORITY_CLASS, this gives you thread priority 31 (highest possible).
    SetThreadPriority(hThread, THREAD_PRIORITY_TIME_CRITICAL);

    // 3. Disable priority boosting (prevents Windows from dynamically changing priority)
    SetThreadPriorityBoost(GetCurrentThread(), TRUE);

    // Call this when your real-time task starts
    /*You can use the Windows Power Management API to prevent the system from entering low-power states while your application is running:*/
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
    // =========================================================================
    // SEMPLIFICAZIONE RISOLUTIVA: DISATTIVAZIONE DEL BOOST SENZA NTDLL
    // =========================================================================
    // Questa API Win32 documentata sostituisce interamente la NtSetInformationThread.
    // Passando TRUE (DisablePriorityBoost = TRUE), ordiniamo al Kernel NT di
    // bloccare il bilanciamento dinamico della priorità di questo thread.
    // if (!SetThreadPriorityBoost(hThread, TRUE)) {
    //     KdPrint(("[MALAMORE] Impossibile disattivare il Priority Boost." ));
    // }
    // // 2. Mascheramento e isolamento dell'affinità hardware (Core Isolati)
    // // DWORD_PTR affinityMask = (1ULL << targetCoreIndex); //
    // if (!SetProcessAffinityMask(hProcess, Context->AffinityMask)) { //
    //     KdPrint(("[MALAMORE] SetProcessAffinityMask fallita" ));
    // }
    // DWORD_PTR oldAffinity = SetThreadAffinityMask(hThread, Context->AffinityMask); //
    // if (oldAffinity == 0) {
    //     KdPrint(("[MALAMORE] SetProcessAffinityMask error for the second time" ));
    // }
    // PROCESSOR_NUMBER idealProcessor;
    // RtlZeroMemory(&idealProcessor, sizeof(PROCESSOR_NUMBER));
    // idealProcessor.Number = 1; // Assegnazione rigida del core fisico
    // SetThreadIdealProcessorEx(GetCurrentThread(), &idealProcessor, NULL);

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
    // MalamoreIStream driver;
    // status = driver.Open(Context->DevicePath, StreamFlags::Read | StreamFlags::Write);
    // if (!NT_SUCCESS(status)) {
    //     KdPrint(("[MALAMORE 🚨] Impossibile aprire la risorsa '%ws'. Errore: %s\n", Context->DevicePath, NtStatusToString(status)));
    //     KeRevertToUserGroupAffinityThread(&previousAffinity);
    //     PsTerminateSystemThread(status);
    //     return;
    // }

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
            *((LARGE_INTEGER *) &currentTime) = KeQueryPerformanceCounter(nullptr);
        } while ((((LARGE_INTEGER *) &currentTime)->QuadPart < ((LARGE_INTEGER *) &nextPeriodTime)->QuadPart) && (
                     KernelAtomicRead(&((struct UnifiedThreadContext *) Context)->KeepRunning) == 1));
        if (KernelAtomicRead(&((struct UnifiedThreadContext *) Context)->KeepRunning) == 0) {
            break;
        }
        // RtlZeroMemory(idata, HARDWARE_PAYLOAD_SIZE);
        // CONFIGURAZIONE DEL TIMEOUT RIGIDO DEL KERNEL
        bytesActuallyRead = 0;
        status = Context->DevicePath->ReadWithTimeout(idata, sizeof(idata), Context->MaxBudgetUs, &bytesActuallyRead);

        // unsigned long long messageId = 0;
        // sscanf analizza la stringa cercando di accoppiare esattamente il testo statico
        // ed estrae il numero tramite il quantificatore %llu
        // int result = sscanf_s(reinterpret_cast<char const *>(idata), "Msg #%llu\n", &messageId);

        if (status == STATUS_SUCCESS) {
            *((LARGE_INTEGER *) &currentTime) = KeQueryPerformanceCounter(nullptr);
            status = CorePool_RouteMessageSrcToDest(currentTime.QuadPart, Context->ProcessId, Context->TargetThreadId, idata, bytesActuallyRead);
            if (!NT_SUCCESS(status)) {
                KdPrint(("There was an issue while attempting to send the response at timestamp %llu\n", currentTime.QuadPart));
            } else {
            }
        } else if (status == STATUS_TIMEOUT) {
        } else {
             KdPrint(("ERROR: %s does not allow me to continue reading: \n", NtStatusToString(status)));
        }
        if (&nextPeriodTime)
            ((LARGE_INTEGER *) &nextPeriodTime)->QuadPart += ((ULONG64) periodTicks);
    }

    // driver.Close();
    KeRevertToUserGroupAffinityThread(&previousAffinity);
}

VOID HardwareWriterThreadRoutine(struct UnifiedThreadContext* Context, struct FutureResultParcel* ) {
    if (!Context) {
        KdPrint(("HardwareReaderContext] Invalid configuration (nullptr)\n"));
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
        return;
    }

    auto hProcess = GetCurrentProcess();
    auto hThread = GetCurrentThread();

    // 1. Elevate the entire process to REALTIME priority class.
    // NOTE: This requires the application to be run as Administrator.
    SetPriorityClass(hProcess, REALTIME_PRIORITY_CLASS);

    // 2. Set the thread priority to TIME_CRITICAL.
    // Combined with REALTIME_PRIORITY_CLASS, this gives you thread priority 31 (highest possible).
    SetThreadPriority(hThread, THREAD_PRIORITY_TIME_CRITICAL);

    // 3. Disable priority boosting (prevents Windows from dynamically changing priority)
    SetThreadPriorityBoost(GetCurrentThread(), TRUE);

    // Call this when your real-time task starts
    /*You can use the Windows Power Management API to prevent the system from entering low-power states while your application is running:*/
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
    char output_buffer[1024];
    memset(output_buffer, 0, sizeof(output_buffer));

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
    static OVERLAPPED o2;
    static bool initialized = false;
    memset(&o2, 0, sizeof(o2));
    int Length;


    // Allineamento temporale iniziale sulla barriera comune pre-calcolata
    INIT_SYNCH_FROM_CONTEXT(&nextPeriodTime, Context, periodTicks);  //nextPeriodTime.QuadPart = Context->SynchronizedStartTicks;
    UCHAR idata[HARDWARE_PAYLOAD_SIZE];
    uint64_t iterations_count = 0;
    DWORD bytesWritten = 0;

    // =========================================================================
    // LOOP PRINCIPALE DI LETTURA PERIODICA ASINCRONA (PASSIVE_LEVEL - 3.33ms)
    // =========================================================================
    while ((&nextPeriodTime) && (&currentTime) && (Context) && (iterations_count < Context->num_cycles) && KernelAtomicRead(
               &((struct UnifiedThreadContext *) Context)->KeepRunning) == 1) {
        do {
            _mm_pause();
            *((LARGE_INTEGER *) &currentTime) = KeQueryPerformanceCounter(nullptr);
        } while ((((LARGE_INTEGER *) &currentTime)->QuadPart < ((LARGE_INTEGER *) &nextPeriodTime)->QuadPart) && (
                     KernelAtomicRead(&((struct UnifiedThreadContext *) Context)->KeepRunning) == 1));
        if (KernelAtomicRead(&((struct UnifiedThreadContext *) Context)->KeepRunning) == 0) {
            break;
        }
        // RtlZeroMemory(idata, HARDWARE_PAYLOAD_SIZE);
        // CONFIGURAZIONE DEL TIMEOUT RIGIDO DEL KERNEL
        bytesActuallyRead = 0;
        currentTime = KeQueryPerformanceCounter(nullptr);
        Length = snprintf(output_buffer, sizeof(output_buffer), "Msg #%llu @%llu\n", iterations_count, currentTime.QuadPart);
        (VOID)WriteFile(Context->WRITER_DEVICE, output_buffer, Length, &bytesWritten, &o2); //

        iterations_count++;
        if (&nextPeriodTime)
            ((LARGE_INTEGER *) &nextPeriodTime)->QuadPart += ((ULONG64) periodTicks);
    }

    // driver.Close();
    KeRevertToUserGroupAffinityThread(&previousAffinity);
}