// CMakeLists.txt della DLL: add_library(MyTask SHARED task.cpp)
#include <windows.h>
#include <chrono>
#include <malamore.h>
#include <Client/KernelBinderClient.h>

// // Struttura identica a quella condivisa per interpretare i dati
// #pragma pack(push, 1)
// struct SharedDataBlock {
//     volatile ULONG TaskReady;
//     volatile ULONG TaskCompleted;
//     volatile ULONG ExitSignal;
//     ULONG DataLength;
//     UCHAR Payload;
// };
// #pragma pack(pop)


extern "C" __declspec(dllexport) void ExecuteCustomTask(void* pSharedMem) {
    SharedDataBlock* data = reinterpret_cast<SharedDataBlock*>(pSharedMem);

    // Esempio: un buffer locale interno alla DLL che deve essere protetto a basso livello
    uint8_t localBuffer[4096];

    {
        // Utilizzo del wrapper condiviso: blocca il buffer locale nella RAM fisica
        PinnedMemory lockLocalBuffer(localBuffer, sizeof(localBuffer));

        auto start = std::chrono::high_resolution_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::high_resolution_clock::now() - start).count() < 10) {

            // Lavora sul buffer protetto senza paura di subire page-fault legati allo swap
            localBuffer[0] = 0xFF;
            _mm_pause();
                   }

        // All'interno di ExecuteCustomTask nella DLL:
        data->Payload[0] = 0xCF; // Scrive la risposta nel primo elemento del vettore
        data->DataLength = 1;

        // Il buffer locale viene sbloccato automaticamente qui in modo invisibile all'utente
    }
}