#include "malamore/thread_generic_polling.h"
#include <intrin.h> // Richiesto per _BitScanForward64

static void PreloadCoreInput(InterProcessChannelData* pInChannel, MessageInputMap* inMap) {
    if (!pInChannel || !inMap) return;

    BinderParcel packet;
    while (KernelBufferChannelRead(pInChannel, &packet, sizeof(BinderParcel)) == STATUS_SUCCESS) {
        if (packet.CommandType == CMD_SIGNAL_NODE && packet.DataLength > 0) {
            ULONG senderId = packet.SourceNodeId % MAX_NODES_IN_SYSTEM;
            inMap->Nodes[senderId].Dimension = packet.DataLength;
            inMap->Nodes[senderId].Buffer = packet.PayloadData;
        }
    }
}

static void FlushCoreOutput(InterProcessChannelData* pOutChannel, MessageOutputMap* outMap, ULONG sourceNodeId) {
    if (!pOutChannel || !outMap) return;

    for (ULONG i = 0; i < MAX_NODES_IN_SYSTEM; ++i) {
        if (outMap->Nodes[i].Dimension == 0 || outMap->Nodes[i].Buffer == NULL) continue;

        BinderParcel packet;
        RtlZeroMemory(&packet, sizeof(BinderParcel));

        packet.CommandType = CMD_SIGNAL_NODE;
        packet.SourceNodeId = sourceNodeId;
        packet.TargetNodeId = i;

        ULONG bytesToCopy = (outMap->Nodes[i].Dimension < MAX_PAYLOAD_SIZE) ? outMap->Nodes[i].Dimension : MAX_PAYLOAD_SIZE;
        RtlCopyMemory(packet.PayloadData, outMap->Nodes[i].Buffer, bytesToCopy);
        packet.DataLength = bytesToCopy;

        KernelBufferChannelWrite(pOutChannel, &packet, sizeof(BinderParcel));

        outMap->Nodes[i].Dimension = 0;
        outMap->Nodes[i].Buffer = NULL;
    }
}

/**
 * CORE POLLING ROUTINE GENERIC (RING 0 TANDEM ENGINE - PRE-WHILE BARRIER ALLIGNMENT)
 */
VOID CorePollingRoutineGeneric(_In_ PVOID Context) {
    CorePollingContext* config = (CorePollingContext*)Context;
    if (!config) {
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
        return;
    }

    KIRQL passiveIrql = PASSIVE_LEVEL;
    LARGE_INTEGER currentTime, nextPeriodTime;
    LARGE_INTEGER highLevelTimeoutTicks;
    ULONG64 maxWcetTicks; // <--- NUOVO: Budget espresso in Ticks interi hardware


    GROUP_AFFINITY targetAffinity, previousAffinity;
    ULONG coreId = 0;
    unsigned long index = 0;

    MessageInputMap inputMap;
    MessageOutputMap outputMap;

    RtlZeroMemory(&inputMap, sizeof(MessageInputMap));
    RtlZeroMemory(&outputMap, sizeof(MessageOutputMap));
    RtlZeroMemory(&targetAffinity, sizeof(GROUP_AFFINITY));
    RtlZeroMemory(&previousAffinity, sizeof(GROUP_AFFINITY));

    if (_BitScanForward64(&index, config->AffinityMask)) {
        coreId = (ULONG)index;
    }

    targetAffinity.Mask = config->AffinityMask;
    targetAffinity.Group = 0;

    KeSetSystemGroupAffinityThread(&targetAffinity, &previousAffinity);

    // CALCOLO DEL BUDGET IN TICKS INTERI (Bypassa l'uso dei double)
    // Formula: Ticks = (Us * Frequenza) / 1.000.000
    maxWcetTicks = (static_cast<ULONG64>(config->MaxWcetUs) * config->PerfCounterFrequency) / 1000000ULL;
    nextPeriodTime.QuadPart = config->SynchronizedStartTicks + config->PhaseOffsetTicks;

    // Innalzamento immediato a DISPATCH_LEVEL per avviare la sincronizzazione oraria protetta
    KeRaiseIrql(DISPATCH_LEVEL, &passiveIrql);

    // =========================================================================
    // BARRIERA DI SINCRO PRE-WHILE: ATTESA DEL SEMAFORO TEMPORALE COMUNE
    // =========================================================================
    // Questo ciclo congela TaskA e TaskB a DISPATCH_LEVEL finché l'orologio hardware
    // del sistema operativo non tocca esattamente il timestamp stabilito dal chiamante.
    do {
        YieldProcessor(); // Cede la pipeline ma blocca l'esecuzione sul core logico
        currentTime = KeQueryPerformanceCounter(NULL);
    } while (currentTime.QuadPart < (LONGLONG)config->SynchronizedStartTicks &&
             KernelAtomicRead(&config->KeepRunning));

    // Se il driver viene arrestato durante la fase di aggancio preliminare, esce pulito
    if (!KernelAtomicRead(&config->KeepRunning)) {
        KeLowerIrql(passiveIrql);
        KeRevertToUserGroupAffinityThread(&previousAffinity);
        PsTerminateSystemThread(STATUS_SUCCESS);
        return;
    }

    // =========================================================================
    // ANCORAGGIO ALLA PRIMA SCADENZA DI FASE INIZIALE SIMMETRICA
    // =========================================================================
    // Ora che siamo al tempo zero esatto del sistema, impostiamo la prima scadenza
    // periodica nominale includendo l'offset di sfasamento (PhaseOffsetTicks).
    nextPeriodTime.QuadPart = config->SynchronizedStartTicks + config->PhaseOffsetTicks;

    // =========================================================================
    // LOOP PRINCIPALE IN POLLING ATTIVO (DISPATCH_LEVEL)
    // =========================================================================
    while (KernelAtomicRead(&config->KeepRunning)) {

        // --- ARCHITETTURA A TANDEM: ATTESA ATTIVA IN DISPATCH_LEVEL FINO AL TICK HARDWARE ---
        // Avviene esclusivamente se siamo nella nostra finestra temporale di riposo (IsHighPriority == 0)
        if (KernelAtomicRead(&config->IsHighPriority) == 0) {
            do {
                YieldProcessor();
                currentTime = KeQueryPerformanceCounter(NULL);
            } while (currentTime.QuadPart < nextPeriodTime.QuadPart &&
                     KernelAtomicRead(&config->KeepRunning) &&
                     KernelAtomicRead(&config->IsHighPriority) == 0);
        }

        if (!KernelAtomicRead(&config->KeepRunning)) break;

        // VERIFICA RIGIDA: Eseguiamo le operazioni solo se la timeline ci ha promosso a HIGH
        if (KernelAtomicRead(&config->IsHighPriority) == 1) {

            // 1. ISOLAMENTO DETERMINISTICO DEL CORE AD ALTA PRIORITÀ
            KIRQL tandemIrql = DISPATCH_LEVEL;
            KeRaiseIrql(HIGH_LEVEL, &tandemIrql); // Sale da DISPATCH_LEVEL a HIGH_LEVEL

            // 2. ARMO DEL WATCHDOG SOFTWARE TEMPORIZZATO DI BUDGET MASSIMO (Timeout Assoluto)
            LARGE_INTEGER taskStart;
            taskStart = KeQueryPerformanceCounter(NULL);
            // CONFIGURAZIONE WATCHDOG TEMPORIZZATO (Intero puro a 64 bit)
            highLevelTimeoutTicks.QuadPart = taskStart.QuadPart + maxWcetTicks;


            // 3. PRE-CARICAMENTO DELLE CODE DAL NONPAGED POOL
            PreloadCoreInput(config->pInChannelData, &inputMap);

            // 4. ESECUZIONE DEL CORPO DEL SERVIZIO SUL CORE CPU ISOLATO
            if (config->TaskFunction != NULL) {
                config->TaskFunction(coreId, &inputMap, &outputMap, config->CustomContext);
            }

            // 5. GESTIONE AUTOMATICA TIMEOUT: SE SCADE IL TEMPO MASSIMO, PASSA A LOW LEVEL
            LARGE_INTEGER taskEnd;
            taskEnd = KeQueryPerformanceCounter(NULL);

            // CONFRONTO INTERO VELOCE DEL TIMEOUT (Zero Jitter, Zero float)
            if (taskEnd.QuadPart > highLevelTimeoutTicks.QuadPart) {
                // Sforamento assoluto del tempo massimo in HIGH_LEVEL: commutazione a Low Level
                InterlockedExchange(&config->IsHighPriority, 0);
            }

            // 6. ABBASSAMENTO IMMEDIATO A LIVELLO DISPATCH_LEVEL PER LA FASE DI USCITA
            KeLowerIrql(DISPATCH_LEVEL);

            // 7. STADIO DI FLUSHING IN USCITA (Esegue a DISPATCH_LEVEL)
            FlushCoreOutput(config->pOutChannelData, &outputMap, coreId);
        }

        // Avanzamento rigidamente periodico del tick per mantenere immutata la fase del tandem sincronizzato
        nextPeriodTime.QuadPart += config->PeriodTicks;
    }

    // Ripristino finale dell'IRQL nominale del sistema e dell'affinità prima di chiudere il System Thread
    KeLowerIrql(passiveIrql);
    KeRevertToUserGroupAffinityThread(&previousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}
