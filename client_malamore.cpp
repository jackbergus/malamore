#include <windows.h>
#include <iostream>
#include <chrono>
#include <thread>

#define PARAM_COUNT 10
#define FSCTL_DEVICE_TYPE 0x00008000
#define IOCTL_AVVIA_SERVIZIO   CTL_CODE(FSCTL_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_INVIA_PARAMETRI  CTL_CODE(FSCTL_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_FERMA_SERVIZIO   CTL_CODE(FSCTL_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

struct SERVIZIO_CONFIG_INPUT {
    ULONG ServizioId;
    char NomeTask[32];
    ULONG PeriodoTaskMs;
    ULONG DurataMediaTaskUs;
};

struct PACKET_PARAMETRI {
    ULONG ServizioId;
    LONG Parametri[PARAM_COUNT];
};

int main() {
    HANDLE hDriver = CreateFileA("\\\\.\\MalamoreOrchestratorLink", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDriver == INVALID_HANDLE_VALUE) return 1;

    DWORD bytes;

    // --- LANCIO PRIMO TASK IN TANDEM ("Task_Pinco", ID 101) ---
    SERVIZIO_CONFIG_INPUT config1 = { 101, "Task_Pinco", 5, 500 };
    if (!DeviceIoControl(hDriver, IOCTL_AVVIA_SERVIZIO, &config1, sizeof(config1), NULL, 0, &bytes, NULL)) {
        std::cerr << "Risorse insufficienti per il Task 101!" << std::endl;
    } else {
        std::cout << "Task_Pinco (101) duplicato con successo su Core A e Core B sfasati." << std::endl;
    }

    // --- LANCIO SECONDO TASK IN PARALLELO ("Task_Pallino", ID 102) ---
    SERVIZIO_CONFIG_INPUT config2 = { 102, "Task_Pallino", 3, 300 };
    if (!DeviceIoControl(hDriver, IOCTL_AVVIA_SERVIZIO, &config2, sizeof(config2), NULL, 0, &bytes, NULL)) {
        std::cerr << "Risorse insufficienti per il Task 102!" << std::endl;
    } else {
        std::cout << "Task_Pallino (102) duplicato con successo su una seconda coppia di core." << std::endl;
    }

    std::cout << "Streaming parametri attivo per 1 minuto..." << std::endl;
    auto startTime = std::chrono::steady_clock::now();
    PACKET_PARAMETRI p1 = { 101, {0} };
    PACKET_PARAMETRI p2 = { 102, {0} };
    LONG counter = 0;

    while (std::chrono::steady_clock::now() - startTime < std::chrono::minutes(1)) {
        for (int i = 0; i < PARAM_COUNT; i++) {
            p1.Parametri[i] = counter + i;
            p2.Parametri[i] = (counter * 2) + i;
        }
        // Alimenta entrambi i task attivi nel kernel
        DeviceIoControl(hDriver, IOCTL_INVIA_PARAMETRI, &p1, sizeof(p1), NULL, 0, &bytes, NULL);
        DeviceIoControl(hDriver, IOCTL_INVIA_PARAMETRI, &p2, sizeof(p2), NULL, 0, &bytes, NULL);

        counter++;
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }

    // Spegnimento controllato di entrambi i servizi
    ULONG id1 = 101, id2 = 102;
    DeviceIoControl(hDriver, IOCTL_FERMA_SERVIZIO, &id1, sizeof(ULONG), NULL, 0, &bytes, NULL);
    DeviceIoControl(hDriver, IOCTL_FERMA_SERVIZIO, &id2, sizeof(ULONG), NULL, 0, &bytes, NULL);

    CloseHandle(hDriver);
    std::cout << "Tutti i task terminati e i core liberati." << std::endl;
    return 0;
}
