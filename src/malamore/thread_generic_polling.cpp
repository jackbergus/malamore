#include "malamore/thread_generic_polling.h"
#include <intrin.h> // Richiesto tassativamente per gli intrinseci MSVC come _BitScanForward64

static void PreloadCoreInput(InterProcessChannelData* pInChannel, MessageInputMap* inMap) {
    if (!pInChannel || !inMap) return;

    BinderParcel packet;

    // Estrae continuativamente i messaggi dal Ring Buffer non-pagabile
    while (KernelBufferChannelRead(pInChannel, &packet, sizeof(BinderParcel)) == STATUS_SUCCESS) {
        if (packet.CommandType == CMD_SIGNAL_NODE && packet.DataLength > 0) {
            ULONG senderId = packet.SourceNodeId % MAX_NODES_IN_SYSTEM;

            // Assegnazione nel pool statico di Ring 0 senza vettori dinamici
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

        // Pulisce lo stadio per il ciclo successivo
        outMap->Nodes[i].Dimension = 0;
        outMap->Nodes[i].Buffer = NULL;
    }
}

/**
 * CORE POLLING ROUTINE GENERIC (RING 0 TANDEM ENGINE - HIGH/LOW PRIORITY DISPATCHER)
 */
VOID CorePollingRoutineGeneric(_In_ PVOID Context) {
    CorePollingContext* config = (CorePollingContext*)Context;
    if (!config) {
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
        return;
    }

    KIRQL oldIrql = PASSIVE_LEVEL;
    LARGE_INTEGER startTime, currentTime, nextPeriodTime;
    KAFFINITY previousAffinity;
    ULONG coreId = 0;
    unsigned long index = 0;

    MessageInputMap inputMap;
    MessageOutputMap outputMap;

    RtlZeroMemory(&inputMap, sizeof(MessageInputMap));
    RtlZeroMemory(&outputMap, sizeof(MessageOutputMap));

    // =========================================================================
    // CORREZIONE MSVC: CALCOLO DEL CORE ID CON _BITSCANFORWARD64
    // =========================================================================
    if (_BitScanForward64(&index, config->AffinityMask)) {
        coreId = (ULONG)index;
    }

    // =========================================================================
    // CORREZIONE WDK API: IMPOSTAZIONE AFFINITÀ CON KESETSYSTEMAFFINITYTHREAD
    // =========================================================================
    // Salva la maschera precedente e blinda il thread corrente sulla maschera target
    previousAffinity = KeSetSystemAffinityThread(config->AffinityMask);

    // Sincronizzazione iniziale basata sul clock hardware nativo del kernel
    startTime = KeQueryPerformanceCounter(NULL);
    nextPeriodTime.QuadPart = startTime.QuadPart + config->PhaseOffsetTicks;

    // =========================================================================
    // LOOP PRINCIPALE IN POLLING ATTIVO (RUNNING A RING 0)
    // =========================================================================
    while (KernelAtomicRead(&config->KeepRunning)) {

        // --- ARCHITETTURA A TANDEM: ATTESA ATTIVA COOPERATIVA VIA YIELD HARDWARE ---
        do {
            YieldProcessor(); // Esegue lo stallo controllato della pipeline hardware della CPU
            currentTime = KeQueryPerformanceCounter(NULL);
        } while (currentTime.QuadPart < nextPeriodTime.QuadPart &&
                 KernelAtomicRead(&config->KeepRunning));

        if (!KernelAtomicRead(&config->KeepRunning)) break;

        // GESTIONE DELLA PRIORITÀ HIGH/LOW
        BOOLEAN elevateToHigh = (KernelAtomicRead(&config->IsHighPriority) == 1);

        if (elevateToHigh) {
            // FINESTRA DETERMINISTICA ISOLATA A HIGH_LEVEL: Blocca interruzioni e scheduler NT
            KeRaiseIrql(HIGH_LEVEL, &oldIrql);
        } else {
            // Task a bassa priorità: gira a DISPATCH_LEVEL per consentire l'interbiforcazione dei DPC sani
            KeRaiseIrql(DISPATCH_LEVEL, &oldIrql);
        }

        // PRE-CARICAMENTO DELLE CODE DAL NONPAGED POOL (INGRESSO)
        PreloadCoreInput(config->pInChannelData, &inputMap);

        LARGE_INTEGER taskStart, taskEnd, freq;
        taskStart = KeQueryPerformanceCounter(&freq);

        // ESECUZIONE DEL CORPO DEL SERVIZIO SUL CORE CPU ISOLATO
        if (config->TaskFunction != NULL) {
            config->TaskFunction(coreId, &inputMap, &outputMap, config->CustomContext);
        }

        taskEnd = KeQueryPerformanceCounter(NULL);

        // ABBASSAMENTO IMMEDIATO DELL'IRQL PER RIPRISTINARE I FLUSSI DI SISTEMA
        KeLowerIrql(oldIrql);

        // WATCHDOG INLINE DEL KERNEL CON DEGRADAZIONE DELLA PRIORITÀ (SAFE RECOVERY)
        ULONG64 elapsedUs = ((taskEnd.QuadPart - taskStart.QuadPart) * 1000000) / freq.QuadPart;
        if (elapsedUs > config->MaxWcetUs) {
            // SANZIONE DI RING 0: Se il Task ad alta priorità sgarra il WCET, viene degradato a LOW (DISPATCH_LEVEL)
            if (KernelAtomicRead(&config->IsHighPriority) == 1) {
                InterlockedExchange(&config->IsHighPriority, 0); // Spostamento atomico hardware a priorità Bassa
            }
        }

        // FLUSHING DEI MESSAGGI IN USCITA VERSO I CORES A VALLE (USCITA)
        FlushCoreOutput(config->pOutChannelData, &outputMap, coreId);

        // Avanzamento rigidamente periodico del tick per mantenere immutata la fase del tandem
        nextPeriodTime.QuadPart += config->PeriodTicks;
    }

    // RIPRISTINO DELL'AFFINITÀ ORIGINALE DEL THREAD PRIMA DELLA TERMINAZIONE
    KeSetSystemAffinityThread(previousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}
