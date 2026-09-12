//
// Created by giaco on 12/09/2026.
//

#include <malamore.h>

VOID CoreAPollingRoutine(_In_ PVOID Context) {
    PTHREAD_CONFIG config = (PTHREAD_CONFIG)Context;
    PCONTESTO_SERVIZIO contesto = config->ContestoServizio;
    KIRQL oldIrql;
    LARGE_INTEGER startTime, currentTime, nextPeriodTime;
    GROUP_AFFINITY previousAffinity;

    // Vincola l'affinità hardware al core specificato dal client
    KeSetSystemGroupAffinityThread(&config->Affinity, &previousAffinity);

    LONGLONG periodTicks = (TASK_PERIOD_MS * contesto->TimerFrequency.QuadPart) / 1000;

    startTime = KeQueryPerformanceCounter(NULL);
    nextPeriodTime.QuadPart = startTime.QuadPart + config->PhaseOffsetTicks;

    while (!contesto->TerminateThreads) {
        // 1. Attesa deterministica a PASSIVE_LEVEL senza cedere il controllo (Spinning controllato)
        do {
            YieldProcessor();
            currentTime = KeQueryPerformanceCounter(NULL);
        } while (currentTime.QuadPart < nextPeriodTime.QuadPart && !contesto->TerminateThreads);

        if (contesto->TerminateThreads) break;

        // 2. Finestra attiva: Congela il core portandolo a HIGH_LEVEL
        KeRaiseIrql(HIGH_LEVEL, &oldIrql);

        // 3. Estrae l'ID del core reale dalla maschera di affinità
        CCHAR bitPosition = RtlFindLeastSignificantBit(config->Affinity.Mask);
        ULONG coreId = (bitPosition != -1) ? (ULONG)bitPosition : 0;

        // 4. Invocazione diretta del Task dinamico con passaggio del payload opaco
        if (contesto->TaskDaEseguire != NULL) {
            contesto->TaskDaEseguire(coreId, contesto->MessaggioParametrizzazione);
        }

        // 5. Rilascio dinamico immediato dell'IRQL (Resetta la tolleranza del Watchdog dell'OS)
        KeLowerIrql(oldIrql);

        nextPeriodTime.QuadPart += periodTicks;
    }

    // Ripristina lo stato originale prima della terminazione del thread
    KeRevertToUserGroupAffinityThread(&previousAffinity);
    ExFreePoolWithTag(config, 'ThCf');
    PsTerminateSystemThread(STATUS_SUCCESS);
}
