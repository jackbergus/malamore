#include <malamore/winapi_client/reserve_cpus.h>
#include <iostream>
#include <Windows.h>

#ifdef min
#undef min
#endif


int main() {
    // Osserva: nella macchina corrente, i core bassi sono quelli ad alte prestazioni, mentre quelli alti sono quelli
    // a prestazioni minori. Quindi, quello che faremo è impostare tutti i driver sulle CPU alte, mentre riservare il
    // più possibile i processori bassi.
    // ID parziale della tua GPU (trovato a runtime o passato come configurazione)
    // std::wstring myGameGpu = L"VEN_10DE"; // Esempio: NVIDIA
    std::cout << "Spostamento degli interrupt hardware sconosciuti sui core di sicurezza..." << std::endl;
    uint64_t ncores = 14;

    /// Step 1. Retrieving the number of
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    uint64_t totalCores = (uint64_t)sysInfo.dwNumberOfProcessors; // Nel tuo caso: 12 logici

    // Computing the masks, forsooth.
    unsigned long mask_low = 0;
    unsigned long mask_high = 0;
    for (int i = 0; i < totalCores; ++i) {
        if (i >= totalCores-ncores) {
            mask_high |= (1ULL << i);
        } else if (i < ncores) {
            mask_low |= (1ULL << i);
        }
    }

    //AutomateCoreIsolationWin11Pro(final_cores); --> Removed. This actually disables the CPU rather than reserving them!
    tryToReserveHighPerfCPUs(mask_low);
    setInterruptMaskGlobally(mask_high);
    cherryPickDeviceToHighMask(mask_high, true);
    std::cout << "Fatto. Nota: Le modifiche hardware di Windows richiedono un riavvio dei dispositivi o del PC per essere lette dal Kernel." << std::endl;
    return 0;
}