#include "malamore.h"

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
        } while (currentTime.QuadPart < nextPeriodTime.QuadPart &&
                 !contesto->TerminateThreads);

        if (contesto->TerminateThreads) break;

        // Finestra deterministica isolata a HIGH_LEVEL
        KeRaiseIrql(HIGH_LEVEL, &oldIrql);

        CCHAR bitPosition = RtlFindLeastSignificantBit(config->Affinity.Mask);
        ULONG coreId = (bitPosition != -1) ? (ULONG)bitPosition : 0;

        // Entrambi i core eseguono lo stesso identico codice di task
        if (contesto->TaskCodice != NULL) {
            contesto->TaskCodice(coreId, contesto);
        }

        KeLowerIrql(oldIrql);
        nextPeriodTime.QuadPart += config->PeriodTicks;
    }

    KeRevertToUserGroupAffinityThread(&previousAffinity);
    ExFreePoolWithTag(config, 'ThCf');
    PsTerminateSystemThread(STATUS_SUCCESS);
}
