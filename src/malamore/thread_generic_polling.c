//
// Created by Giacomo Bergami on 14/09/2026.
//

#include <malamore.h>

VOID CorePollingRoutineGeneric(_In_ PVOID Context) {
    PTHREAD_CONFIG config = (PTHREAD_CONFIG)Context;
    PCONTESTO_SERVIZIO contesto = config->ContestoServizio;
    KIRQL oldIrql;
    LARGE_INTEGER startTime, currentTime, nextPeriodTime;
    GROUP_AFFINITY previousAffinity;

    KeSetSystemGroupAffinityThread(&config->Affinity, &previousAffinity);

    startTime = KeQueryPerformanceCounter(NULL);
    nextPeriodTime.QuadPart = startTime.QuadPart + config->PhaseOffsetTicks;

    while (!contesto->TerminateThreads) {
        do {
            YieldProcessor();
            currentTime = KeQueryPerformanceCounter(NULL);
        } while (currentTime.QuadPart < nextPeriodTime.QuadPart && !contesto->TerminateThreads);

        if (contesto->TerminateThreads) break;

        // Finestra di elaborazione deterministica
        KeRaiseIrql(HIGH_LEVEL, &oldIrql);

        CCHAR bitPosition = RtlFindLeastSignificantBit(config->Affinity.Mask);
        ULONG coreId = (bitPosition != -1) ? (ULONG)bitPosition : 0;

        if (config->IsTaskA && contesto->TaskA != NULL) {
            contesto->TaskA(coreId, contesto);
        } else if (!config->IsTaskA && contesto->TaskB != NULL) {
            contesto->TaskB(coreId, contesto);
        }

        KeLowerIrql(oldIrql);
        nextPeriodTime.QuadPart += config->PeriodTicks;
    }

    KeRevertToUserGroupAffinityThread(&previousAffinity);
    ExFreePoolWithTag(config, 'ThCf');
    PsTerminateSystemThread(STATUS_SUCCESS);
}
