#include "Client/KernelBinderClient.h"
#include <iostream>

int main() {
    try {
        std::cout << "[CLIENT EXECUTABLE] Inizializzazione della libreria statica...\n";

        // Istanzia la classe compilata separatamente
        KernelBinderClient clientLib;

        std::cout << "Connessione riuscita! Premere INVIO per richiedere l'esecuzione alla DLL...\n";
        std::cin.get();

        uint8_t inputSample = 0x1F;
        uint8_t responseSample = 0;

        if (clientLib.SendTask("C:\\Tasks\\MyTask.dll", &inputSample, 1, &responseSample)) {
            std::cout << "[CLIENT EXECUTABLE] Task completato! Risposta DLL: 0x"
                      << std::hex << (int)responseSample << std::dec << "\n";
        }

    } catch (const std::runtime_error& e) {
        std::cerr << "Errore: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
