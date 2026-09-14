#include <windows.h>
#include <iostream>
#include <chrono>
#include <thread>

#define PARAM_COUNT 10
#define FSCTL_DEVICE_TYPE 0x00008000
#define IOCTL_AVVIA_SERVIZIO   CTL_CODE(FSCTL_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_INVIA_PARAMETRI  CTL_CODE(FSCTL_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_FERMA_SERVIZIO   CTL_CODE(FSCTL_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#include <malamore.h>

int main() {
    HANDLE hDriver = CreateFileA("\\\\.\\MalamoreOrchestratorLink", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDriver == INVALID_HANDLE_VALUE) return 1;

    ULONG staticServiceId = 555;
    SERVIZIO_CONFIG_INPUT config;
    RtlZeroMemory(&config, sizeof(config));

    config.ServizioId = staticServiceId;
    strcpy_s(config.NomeTaskA, "Task_Pinco");
    config.PeriodoTaskAMs = 5;
    config.DurataMediaTaskAUs = 500;

    strcpy_s(config.NomeTaskB, "Task_Pallino");
    config.PeriodoTaskBMs = 2;
    config.DurataMediaTaskBUs = 300;

    DWORD bytes;
    // Chiamata IOCTL sbloccata per utenti non-admin
    if (!DeviceIoControl(hDriver, IOCTL_AVVIA_SERVIZIO, &config, sizeof(config), NULL, 0, &bytes, NULL)) {
        // Se ritorna FALSE e l'errore è legato alle risorse, significa che non ci sono coppie di core libere
        std::cerr << "Impossibile avviare il servizio: core insufficienti o task non validi." << std::endl;
        CloseHandle(hDriver); return 1;
    }

    std::cout << "Servizio inizializzato con successo. I Core sono stati scelti autonomamente dal Driver.\nStreaming attivo..." << std::endl;

    auto startTime = std::chrono::steady_clock::now();
    PACKET_PARAMETRI pacchetto = { staticServiceId, {0} };
    LONG counter = 0;

    while (std::chrono::steady_clock::now() - startTime < std::chrono::minutes(1)) {
        for (int i = 0; i < PARAM_COUNT; i++) pacchetto.Parametri[i] = counter + i;
        DeviceIoControl(hDriver, IOCTL_INVIA_PARAMETRI, &pacchetto, sizeof(pacchetto), NULL, 0, &bytes, NULL);
        counter++;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    DeviceIoControl(hDriver, IOCTL_FERMA_SERVIZIO, &staticServiceId, sizeof(ULONG), NULL, 0, &bytes, NULL);
    CloseHandle(hDriver);
    std::cout << "Servizio arrestato e core rilasciati al pool del Driver." << std::endl;
    return 0;
}