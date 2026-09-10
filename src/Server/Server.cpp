/*
* malamore
Copyright (C) 2026 Giacomo Bergami

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */
#include <malamore.h>
#include <iostream>
#include <fstream>
#include <chrono>
#include <string>
#include <intrin.h> // Richiesto per __rdtsc()
#include <../../include/Library/KernelBinderClient.h>

// Funzione di utilità per ottenere il timestamp formattato per il log
std::string GetCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    struct tm buf;
    localtime_s(&buf, &in_time_t);
    char timeStr[26];
    asctime_s(timeStr, sizeof(timeStr), &buf);
    std::string s(timeStr);
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

// Funzione per scrivere i dati nel file di Log
void WriteLog(const std::string& dllPath, ULONG payload, double executionTimeMs, double jitterMicroseconds) {
    std::ofstream logFile("kernel_binder_server.log", std::ios_base::app);
    if (logFile.is_open()) {
        logFile << "[" << GetCurrentTimestamp() << "] "
                << "Richiesta DLL: " << dllPath << " | "
                << "Payload: 0x" << std::hex << payload << std::dec << " | "
                << "Tempo: " << executionTimeMs << " ms | "
                << "Jitter: " << jitterMicroseconds << " us\n";
    }
}

typedef void (*DllTaskFunc)(void*);

void LoadAndExecuteUserDll(SharedDataBlock* sharedMem, LARGE_INTEGER qpcFrequency) {
    std::string dllPath(sharedMem->DllPath);

    // CORREZIONE: Leggi il primo byte dell'array per il log (o gestisci l'array se necessario)
    ULONG inputPayloadSample = sharedMem->Payload[0];

    // 1. Carica dinamicamente la DLL
    HMODULE hDll = LoadLibraryA(sharedMem->DllPath);
    if (hDll == NULL) {
        std::cerr << "[SERVER] Errore caricamento DLL: " << GetLastError() << "\n";
        return;
    }

    // std::string dllPath(sharedMem->DllPath);
    // ULONG inputPayload = sharedMem->Payload;
    // // 1. Carica dinamicamente la DLL
    // HMODULE hDll = LoadLibraryA(sharedMem->DllPath);
    // if (hDll == NULL) {
    //     std::cerr << "[SERVER] Errore caricamento DLL: " << GetLastError() << "\n";
    //     return;
    // }

    DllTaskFunc customTask = (DllTaskFunc)GetProcAddress(hDll, "ExecuteCustomTask");
    if (customTask == nullptr) {
        std::cerr << "[SERVER] Funzione ExecuteCustomTask non trovata.\n";
        FreeLibrary(hDll);
        return;
    }

    // 2. Misurazione ad altissima precisione con TSC e QPC prima dell'esecuzione
    LARGE_INTEGER qpcStart, qpcEnd;
    ULONG64 tscStart = __rdtsc();
    QueryPerformanceCounter(&qpcStart);

    // ESECUZIONE DEL TASK (Target stimato: 10.0 ms)
    customTask(sharedMem);

    // Misurazione subito dopo l'esecuzione
    QueryPerformanceCounter(&qpcEnd);
    ULONG64 tscEnd = __rdtsc();

    // 3. Calcolo del tempo effettivo in Millisecondi
    double elapsedMs = (double)(qpcEnd.QuadPart - qpcStart.QuadPart) * 1000.0 / (double)qpcFrequency.QuadPart;
    ULONG64 totalCycles = tscEnd - tscStart;

    // 4. Calcolo del Jitter (Scostamento dal tempo desiderato di 10.0 ms)
    // Trasformiamo la differenza assoluta in Microsecondi (us)
    double targetMs = 10.0;
    double jitterUs = std::abs(elapsedMs - targetMs) * 1000.0;

    // 5. Output in tempo reale a console
    std::cout << "\n--------------------------------------------------\n";
    std::cout << "[SERVER] Eseguito: " << dllPath << "\n";
    std::cout << "[SERVER] Cicli CPU consumati (TSC): " << totalCycles << "\n";
    std::cout << "[SERVER] Tempo effettivo: " << elapsedMs << " ms\n";

    if (jitterUs > 100.0) { // Warning se il jitter supera i 100 microsecondi
        std::cout << "[⚠️ JITTER RILEVATO]: " << jitterUs << " us di ritardo/anticipo!\n";
    } else {
        std::cout << "[✅ DETERMINISTICO]: Jitter minimo di " << jitterUs << " us\n";
    }
    std::cout << "--------------------------------------------------\n";

    // 6. Salvataggio persistente nel Log
    WriteLog(dllPath, inputPayloadSample, elapsedMs, jitterUs);

    FreeLibrary(hDll);
}

int main() {
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    LARGE_INTEGER qpcFrequency;
    QueryPerformanceFrequency(&qpcFrequency);

    HANDLE hServerEvent = CreateEventW(NULL, FALSE, FALSE, L"Global\\KernelBinderServerEvent");
    HANDLE hClientEvent = CreateEventW(NULL, FALSE, FALSE, L"Global\\KernelBinderClientEvent");
    HANDLE hDevice = CreateFileW(L"\\\\.\\KernelBinderLink", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hDevice == INVALID_HANDLE_VALUE) return 1;

    PVOID pSharedAddress = nullptr;
    DWORD bytesReturned = 0;
    DeviceIoControl(hDevice, IOCTL_MAP_SHARED_MEMORY, NULL, 0, &pSharedAddress, sizeof(PVOID), &bytesReturned, NULL);

    SharedDataBlock* sharedMem = reinterpret_cast<SharedDataBlock*>(pSharedAddress);

    {
        // USO DEL WRAPPER: Da questo blocco in poi, la memoria condivisa è bloccata nella RAM fisica
        // Il compilatore trova PinnedMemory dentro KernelBinderClient.h
        // e il linker lo associa al codice compilato in KernelBinderClient.cpp
        PinnedMemory lockSharedMemory(sharedMem, sizeof(SharedDataBlock));

        std::cout << "[SERVER] Monitoraggio Jitter attivo. Memoria protetta da RAII wrapper...\n";

        while (true) {
            WaitForSingleObject(hServerEvent, INFINITE);
            if (sharedMem->ExitSignal) break;

            if (sharedMem->TaskReady) {
                LoadAndExecuteUserDll(sharedMem, qpcFrequency);
                sharedMem->TaskReady = 0;
                sharedMem->TaskCompleted = 1;
                SetEvent(hClientEvent);
            }
        }

        // Uscendo da questo scope, lockSharedMemory viene distrutto
        // e chiama automaticamente al suo interno VirtualUnlock
    }

    CloseHandle(hDevice);
    return 0;
}
