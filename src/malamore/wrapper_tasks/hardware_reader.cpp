//
// Created by Giacomo Bergami on 15/09/2026.
//

#include "malamore/wrapper_tasks/hardware_reader.h"

#include "malamore/threads/thread_pool.h"
#include "malamore/threads/thread_shared.h"
#include "malamore/utility/streambuf/MalamoreIStream.h"

#ifdef LINK_UWP_IOT
// Header nativi obbligatori per sbloccare le caratteristiche Real-Time in UWP / IoT Core
#include <avrt.h>
#pragma comment(lib, "Avrt.lib")
#endif


void HardwareReaderThreadRoutine(struct UnifiedThreadContext* Context, struct FutureResultParcel* ) {
    if (!Context) {
        KdPrint(("HardwareReaderContext] Invalid configuration (nullptr)\n"));
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
        return;
    }

#ifdef LINK_KERNEL
    // =========================================================================
    // MODALITÀ KERNEL (RING 0): Elevazione alla massima priorità hardware
    // =========================================================================
    // Imposta la priorità 31 sul thread di sistema corrente.
    // Disattiva Round-Robin e preemption ordinaria di Windows!
    KeSetPriorityThread(KeGetCurrentThread(), HIGH_REALTIME_PRIORITY);

    GROUP_AFFINITY targetAffinity, previousAffinity;
    RtlZeroMemory(&targetAffinity, sizeof(GROUP_AFFINITY));
    targetAffinity.Mask = Context->AffinityMask;
    targetAffinity.Group = 0;
    KeSetSystemGroupAffinityThread(&targetAffinity, &previousAffinity);

#else

#ifdef LINK_UWP_IOT
    // =========================================================================
    // MODALITÀ WINDOWS IOT CORE (UWP / C++/CX): AvRT Thread Characteristics
    // =========================================================================
    // In IoT Core, per aggirare i blocchi dell'App Container, registriamo il thread
    // presso il sottosistema multimediale pro-RealTime del Kernel di Windows NT (MMCSS).
    DWORD taskIndex = 0;
    // Chiediamo la sbarra "Pro Audio" o "Realtime", che forza la priorità 31 sul silicio
    HANDLE hAvrtTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    if (hAvrtTask != NULL) {
        // Eleviamo la priorità del task all'interno della classe Real-Time di IoT Core
        AvSetMmThreadPriority(hAvrtTask, AVRT_PRIORITY_CRITICAL);
    }

    // Anche in UWP, l'affinità hardware si imposta tramite le API dei thread
    DWORD_PTR affinityMask = static_cast<DWORD_PTR>(Context->AffinityMask);
    SetThreadAffinityMask(GetCurrentThread(), affinityMask); //

#endif
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

    PROCESSOR_NUMBER idealProcessor;
    RtlZeroMemory(&idealProcessor, sizeof(PROCESSOR_NUMBER));
    // Calcoliamo l'indice del core estraendolo logaritmicamente dalla maschera di bit
    DWORD coreIndex = 0;
    _BitScanForward64(&coreIndex, Context->AffinityMask);
    idealProcessor.Number = static_cast<BYTE>(coreIndex);
    SetThreadIdealProcessorEx(hThread, &idealProcessor, NULL);
#endif

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
            // const ULONG64 Timestamp = (((LARGE_INTEGER *) &currentTime)->QuadPart * 1000000ULL) / ((struct UnifiedThreadContext
            //                   *) Context)->PerfCounterFrequency;
            *((LARGE_INTEGER *) &currentTime) = KeQueryPerformanceCounter(nullptr);
            status = CorePool_RouteMessageSrcToDest(currentTime.QuadPart, Context->ProcessId, Context->TargetThreadId, idata, bytesActuallyRead);
            if (!NT_SUCCESS(status)) {
                KdPrint(("There was an issue while attempting to send the response at timestamp %llu\n", currentTime.QuadPart));
            } else {
                // KdPrint(("Msg: %p", idata));
            }
        }
        else if (status == STATUS_TIMEOUT) {
            // Timeout, continuing to navigate the system
            // if (result == 1) {
            //     KdPrint(("===> %llu", messageId));
            // }
        } else {
            // if (result == 1) {
            //     KdPrint(("ERROR: ===> %llu", messageId));
            // }
             KdPrint(("ERROR: %s does not allow me to continue reading: \n", NtStatusToString(status)));
        }
        if (&nextPeriodTime)
            ((LARGE_INTEGER *) &nextPeriodTime)->QuadPart += ((ULONG64) periodTicks);
    }

    // driver.Close();
    KeRevertToUserGroupAffinityThread(&previousAffinity);
}

#ifdef ALTERNATIVE_WITH_CYCLIC_THREADS
// All'interno di src/malamore/wrapper_tasks/hardware_reader.cpp

void HardwareReaderThreadRoutine2(struct UnifiedThreadContext* Context, struct FutureResultParcel*) {
    if (!Context) {
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
        return;
    }

    NTSTATUS status;
    ULONG bytesActuallyRead;
    LARGE_INTEGER currentTime, nextPeriodTime, now;
    GROUP_AFFINITY targetAffinity, previousAffinity;

    // 1. PINNING RIGIDO DEL THREAD SUL SUO CORE DEDICATO
    RtlZeroMemory(&targetAffinity, sizeof(GROUP_AFFINITY));
    targetAffinity.Mask = Context->AffinityMask;
    targetAffinity.Group = 0;
    KeSetSystemGroupAffinityThread(&targetAffinity, &previousAffinity);

    // MalamoreIStream driver;
    // status = driver.Open(Context->DevicePath, StreamFlags::Read | StreamFlags::Write);
    // if (!NT_SUCCESS(status)) {
    //     KeRevertToUserGroupAffinityThread(&previousAffinity);
    //     PsTerminateSystemThread(status);
    //     return;
    // }

    // Istanziamo il timer hardware ad alta risoluzione per la pausa non attiva
    HANDLE hHighResTimer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (hHighResTimer == NULL) hHighResTimer = CreateWaitableTimerW(NULL, TRUE, NULL);

    // =========================================================================
    // PARAMETRIZZAZIONE GEOMETRICA DELLE M COPIE E DEGLI SFASAMENTI
    // =========================================================================
    const ULONG64 M = Context->CyclesPerThread;      // Numero di cicli completi assegnati a ciascun thread
    const ULONG64 i = Context->ThreadIndex;          // Indice dell'i-esimo thread (da 0 a N-1)

    // Durata hardware di un singolo ciclo logico
    const ULONG64 periodTicks = (static_cast<ULONG64>(((struct UnifiedThreadContext*)Context)->PeriodUs) * ((struct UnifiedThreadContext*)Context)->PerfCounterFrequency) / 1000000ULL;
    const ULONG64 halfPeriodTicks = periodTicks / 2;

    ULONG64 ciclo_id = 0;
    LARGE_INTEGER dueTime;

    // 2. LOGICA DI STARTUP: Se non sono il primo thread (i != 0), mi addormento per (M - i - 2 + M/2) cicli
    if (i > 0) {
        ULONG64 startupSleepCycles = M - i - 2 + (M / 2);
        ULONG64 startupSleepTicks = startupSleepCycles * periodTicks;
        LONGLONG timeIn100ns = (startupSleepTicks * 10000000LL) / Context->PerfCounterFrequency;
        dueTime.QuadPart = -timeIn100ns;
        SetWaitableTimer(hHighResTimer, &dueTime, 0, NULL, NULL, FALSE);
        WaitForSingleObjectEx(hHighResTimer, INFINITE, TRUE); // Cede il core logico senza attesa attiva
    }

    // TODO: all'esecuzione, definisci l'elemento che vuoi allocare, e determinalo di conseguenza.
    // TODO: // Impostiamo l'ancoraggio orario iniziale per questa sessione di computazione
    // TODO: QueryPerformanceCounter(&nextPeriodTime);

    // FINCHÉ NON TERMINO (IO, COSÌ COME GLI ALTRI N-1 THREAD)
    while (Context && KernelAtomicRead(&((struct UnifiedThreadContext *)Context)->KeepRunning) == 1) {

        // ---------------------------------------------------------------------
        // REGOLA 1: Se ho terminato i miei M cicli assegnati -> azzera ciclo_id e dormi
        // ---------------------------------------------------------------------
        if (ciclo_id >= M) {
            ciclo_id = 0; // Azzera l'indice del ciclo locale

            // Calcoliamo la sosta fissa di sfoltimento: M - 2 + M/2 cicli
            ULONG64 burstSleepCycles = M - 2 + (M / 2);
            ULONG64 burstSleepTicks = burstSleepCycles * periodTicks;

            LONGLONG timeIn100ns = (burstSleepTicks * 10000000LL) / Context->PerfCounterFrequency;
            dueTime.QuadPart = -timeIn100ns;

            SetWaitableTimer(hHighResTimer, &dueTime, 0, NULL, NULL, FALSE);
            WaitForSingleObjectEx(hHighResTimer, INFINITE, TRUE); // Sosta profonda, core libero per gli altri

            // Al risveglio riallineiamo la testina temporale al tick corrente della CPU
            QueryPerformanceCounter(&nextPeriodTime);
        }

        // ---------------------------------------------------------------------
        // REGOLA 2: Finché non è il mio turno per inizio ciclo -> attesa attiva a regime
        // ---------------------------------------------------------------------
        do {
            _mm_pause(); // Mantiene la pipeline calda ed il clock del core al massimo
            QueryPerformanceCounter(&now);
        } while ((now.QuadPart < nextPeriodTime.QuadPart) &&
                 (KernelAtomicRead(&((struct UnifiedThreadContext *)Context)->KeepRunning) == 1));

        if (KernelAtomicRead(&((struct UnifiedThreadContext *)Context)->KeepRunning) == 0) {
            break;
        }

        // ---------------------------------------------------------------------
        // REGOLA 3: Computa il ciclo logico ed incrementa il contatore
        // ---------------------------------------------------------------------
        bytesActuallyRead = 0;
        status = Context->DevicePath->ReadWithTimeout(idata, sizeof(idata), Context->MaxBudgetUs, &bytesActuallyRead);

        if (status == STATUS_SUCCESS) {
            currentTime = KeQueryPerformanceCounter(nullptr);
            CorePool_RouteMessageSrcToDest(currentTime.QuadPart, Context->ProcessId, Context->TargetThreadId, idata, bytesActuallyRead);
        }

        // Incrementiamo il ciclo logico locale ed avanziamo la sbarra oraria del singolo passo
        ciclo_id++;
        nextPeriodTime.QuadPart += periodTicks;
    }

    if (hHighResTimer) CloseHandle(hHighResTimer);
    // driver.Close();
    KeRevertToUserGroupAffinityThread(&previousAffinity);
}
#endif