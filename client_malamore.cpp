#include <windows.h>
#include <iostream>

#include "malamore/client.h"

/**
 * Registra un nuovo compito sul sistema delegando il core allocation al driver
 */
void InviaConfigurazioneTask(HANDLE hDevice, ULONG periodoUs, ULONG wcetUs, const char* nomeTask) {
    ServiceParams params = { 0 };
    params.PeriodUs = periodoUs;
    params.EstimatedWcetUs = wcetUs;

    // Scrive il nome della routine bersaglio nel buffer lineare
    strcpy_s(params.TaskName, 64, nomeTask);

    DWORD bytesReturned = 0;

    // Invia la transazione sincrona di Ring 3
    BOOL success = DeviceIoControl(
        hDevice,
        IOCTL_INVIA_PARAMETRI,
        &params, sizeof(ServiceParams),
        &params, sizeof(ServiceParams),
        &bytesReturned,
        NULL
    );

    if (success) {
        std::cout << "[CLIENT] Vincoli temporali inviati con successo per: " << nomeTask << " (Core allocato automaticamente dal driver)\n";
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_GEN_FAILURE) {
            std::cout << "[CLIENT 🚨] Saturazione: Il pool hardware del driver non ha core disponibili nella percentuale di sicurezza!\n";
        } else {
            std::cout << "[CLIENT 🚨] Errore di comunicazione hardware. Win32 Error: " << err << "\n";
        }
    }
}

int main() {
    HANDLE hDevice = CreateFileW(L"\\\\.\\KernelBinderLink", GENERIC_READ | FILE_WRITE_DATA, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        std::cout << "Impossibile aprire il link al driver. Assicurati che Malamore.sys sia caricato.\n";
        return 1;
    }

    // Il client si limita a spingere i task in sequenza. Il driver li spalmerà in automatico sui core liberi!
    InviaConfigurazioneTask(hDevice, 10000, 20, "TaskLettura");
    InviaConfigurazioneTask(hDevice, 20000, 40, "TaskElaborazione");

    // Innesco simultaneo della timeline sincronizzata su tutti i tandem configurati
    DWORD bytes = 0;
    DeviceIoControl(hDevice, IOCTL_AVVIA_SERVIZIO, NULL, 0, NULL, 0, &bytes, NULL);

    std::cout << "Tandem attivati. Premi un tasto per fermare i core...\n";
    std::cin.get();

    DeviceIoControl(hDevice, IOCTL_FERMA_SERVIZIO, NULL, 0, NULL, 0, &bytes, NULL);
    CloseHandle(hDevice);
    return 0;
}
