#include <Library/KernelBinderClient.h>
#include <iostream>

int main() {
    try {
        std::cout << "[CLIENT EXECUTABLE] Inizializzazione della Binder Proxy...\n";
        KernelBinderClient binder;

        binder.SpawnNode("NodoProduttore", "C:\\Tasks\\DllProduttore.dll");
        binder.SpawnNode("NodoConsumatore", "C:\\Tasks\\DllConsumatore.dll");
        binder.ConnectNodes("NodoProduttore", "NodoConsumatore");

        // Registrazione su un nodo condiviso per testare la saturazione e il rilascio EDF
        binder.SpawnNode("NodoCondiviso", "C:\\Tasks\\DllTaskCondiviso.dll");
        binder.RegisterPeriodicTask("NodoCondiviso", "Telemetria_1", 20, 4); // T=20us, C=4us (U=20%)
        binder.RegisterPeriodicTask("NodoCondiviso", "Filtro_2", 40, 8);     // T=40us, C=8us (U=20%)

        std::cout << "\nInfrastruttura Binder e nodi EDF pronti. Premi INVIO per calcolo...\n";
        std::cin.get();

        uint8_t inputVal = 8;
        uint8_t responseBuffer[256] = {0};
        ULONG responseLength = sizeof(responseBuffer);

        binder.SignalNode("NodoProduttore", &inputVal, 1, NULL, NULL);

        if (binder.SignalNode("NodoConsumatore", NULL, 0, responseBuffer, &responseLength)) {
            std::cout << "\n--------------------------------------------------\n";
            std::cout << "[CLIENT SUCCESS] Risposta: " << reinterpret_cast<char*>(responseBuffer) << "\n";
            std::cout << "--------------------------------------------------\n";
        }

        std::cout << "Premi INVIO per distruggere il Nodo Condiviso e liberare le risorse computazionali EDF...\n";
        std::cin.get();

        if (binder.DestroyNode("NodoCondiviso")) {
            std::cout << "[CLIENT] Nodo Condiviso rimosso. Carico CPU rilasciato e core riallocabili.\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Errore: " << e.what() << "\n";
    }
    return 0;
}
