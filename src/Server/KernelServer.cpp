//
// Created by giaco on 10/09/2026.
//

#include <malamore.h>
#include "Server/ServerContext.h"
#include "Server/DllTaskManager.h"
#include "Server/EdfScheduler.h"
#include <Library/PinnedMemory.h>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 5) return 1;

    std::string nodeName = argv[1];
    std::string dllPath = argv[2];
    HANDLE hSharedParcel = (HANDLE)std::stoull(argv[3]);
    HANDLE hLaunchEvent = (HANDLE)std::stoull(argv[4]);

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    double ticksPerUs = (double)freq.QuadPart / 1000000.0;

    try {
        // 1. INIZIALIZZAZIONE CONTESTO DI MEMORIA E CONFIGURAZIONE CANALI
        ServerContext context(nodeName, hSharedParcel, hLaunchEvent);
        context.OpenChannels();

        DllTaskManager taskManager(dllPath);
        taskManager.LoadTask();

        EdfScheduler edfEngine;

        // 2. POLLING ATTIVO CONTINUO A JITTER SUB-MICROSECONDO
        while (true) {
            // Controlla se arrivano comandi asincroni dal ServiceManager (Registrazioni / Transazioni)
            if (context.WaitLaunch(0)) {
                BinderParcel* pParcel = context.GetParcel();

                if (pParcel->CommandType == CMD_REGISTER_PERIODIC_TASK) {
                    edfEngine.RegisterTask(pParcel->TaskMeta, __rdtsc(), ticksPerUs);
                    pParcel->CommandType = 0; // Segnala ricezione avvenuta
                }
                else if (pParcel->CommandType == CMD_SIGNAL_NODE) {
                    // Chiamata sincrona reattiva immediata protetta da PinnedMemory RAII
                    {
                        PinnedMemory lock(pParcel, sizeof(BinderParcel));
                        taskManager.Execute(pParcel, context.GetInputChannel(), context.GetOutputChannel());
                    }
                    context.SignalCompletion();
                    pParcel->CommandType = 0;
                }
            }

            if (edfEngine.IsEmpty()) {
                _mm_pause();
                continue;
            }

            // 3. ESECUZIONE DEL TICK DELLO SCHEDULER EDF
            edfEngine.RunDispatcherTick(
                context.GetParcel(),
                context.GetInputChannel(),
                context.GetOutputChannel(),
                taskManager
            );

            _mm_pause(); // Ottimizza l'esecuzione nel ciclo a consumo 100%
        }

    } catch (const std::exception& e) {
        std::cerr << "[WORKER CRASH] Errore di esecuzione a basso livello: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
