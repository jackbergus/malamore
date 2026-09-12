//
// Created by giaco on 12/09/2026.
//
#include <malamore.h>
#include "shared_defs.h"

VOID CoreBPollingRoutine(_In_ PVOID Context) {
    PTHREAD_CONFIG config = (PTHREAD_CONFIG)Context;
    PCONTESTO_SERVIZIO contesto = config->ContestoServizio;
    KIRQL oldIrql;
    LARGE_INTEGER startTime, currentTime, nextPeriodTime;
    GROUP_AFFINITY previousAffinity;

    KeSetSystemGroupAffinityThread(&config->Affinity, &previousAffinity);

    LONGLONG periodTicks = (TASK_PERIOD_MS * contesto->TimerFrequency.QuadPart) / 1000;

    startTime = KeQueryPerformanceCounter(NULL);
    nextPeriodTime.QuadPart = startTime.QuadPart + config->PhaseOffsetTicks;

    while (!contesto->TerminateThreads) {
        // 1. Attesa sfasata a PASSIVE_LEVEL
        do {
            YieldProcessor();
            currentTime = KeQueryPerformanceCounter(NULL);
        } while (currentTime.QuadPart < nextPeriodTime.QuadPart && !contesto->TerminateThreads);

        if (contesto->TerminateThreads) break;

        // 2. Finestra attiva: Innalza l'IRQL a HIGH_LEVEL
        KeRaiseIrql(HIGH_LEVEL, &oldIrql);

        CCHAR bitPosition = RtlFindLeastSignificantBit(config->Affinity.Mask);
        ULONG coreId = (bitPosition != -1) ? (ULONG)bitPosition : 0;

        // 3. Invocazione della stessa funzione polimorfica sul Core B
        if (contesto->TaskDaEseguire != NULL) {
            contesto->TaskDaEseguire(coreId, contesto->MessaggioParametrizzazione);
        }

        // 4. Abbassa l'IRQL per permettere lo scaricamento dei DPC
        KeLowerIrql(oldIrql);

        nextPeriodTime.QuadPart += periodTicks;
    }

    KeRevertToUserGroupAffinityThread(&previousAffinity);
    ExFreePoolWithTag(config, 'ThCf');
    PsTerminateSystemThread(STATUS_SUCCESS);
}
