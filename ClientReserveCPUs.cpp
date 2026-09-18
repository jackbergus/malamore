#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <iostream>
#include <string>
#include <vector>
#include <malamore/winapi_client/reserve_cpus.h>
#pragma comment(lib, "setupapi.lib")

// Funzione per inviare il comando PnP di "Restart" (Disabilita + Abilita) al dispositivo
bool RestartDeviceProgrammatically(HDEVINFO hDevInfo, SP_DEVINFO_DATA& devInfoData) {
    SP_PROPCHANGE_PARAMS propChangeParams = { 0 };

    // Configura i parametri per fermare il dispositivo
    propChangeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    propChangeParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    propChangeParams.Scope = DICS_FLAG_GLOBAL;
    propChangeParams.StateChange = DICS_STOP; // Ferma il driver

    // Applica lo stop
    if (!SetupDiSetClassInstallParamsW(hDevInfo, &devInfoData, &propChangeParams.ClassInstallHeader, sizeof(propChangeParams))) {
        return false;
    }
    if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo, &devInfoData)) {
        return false;
    }

    // Configura i parametri per riavviare il dispositivo
    propChangeParams.StateChange = DICS_START; // Avvia il driver (rileggerà il registro!)

    if (!SetupDiSetClassInstallParamsW(hDevInfo, &devInfoData, &propChangeParams.ClassInstallHeader, sizeof(propChangeParams))) {
        return false;
    }
    if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hDevInfo, &devInfoData)) {
        return false;
    }

    return true;
}

// Funzione che scrive l'affinità nel registro per un dato Device Instance ID
void MoveInterruptAwayFromGameCores(const std::wstring& deviceId, DWORD64 safeCpuMask) {
    std::wstring path = L"SYSTEM\\CurrentControlSet\\Enum\\" + deviceId + L"\\Device Parameters\\Interrupt Management\\Affinity Policy";
    HKEY hKey;
    
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        DWORD devicePolicy = 4; // IrqPolicySpecifiedProcessors
        RegSetValueExW(hKey, L"DevicePolicy", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&devicePolicy), sizeof(DWORD));
        RegSetValueExW(hKey, L"AssignmentSetOverride", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&safeCpuMask), sizeof(DWORD64));
        RegCloseKey(hKey);
    }
}

bool IsolateGameCoresProgrammatically() {
    // Maschera sicura: es. 0x1 (Solo Core 0 gestirà gli interrupt di tutto il resto del PC)
    DWORD64 safeCpuMask = 0x1;
    uint64_t countImpossible = 0;

    // Prendiamo tutti i dispositivi hardware presenti nel sistema
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
        wchar_t instanceId[MAX_DEVICE_ID_LEN];
        if (CM_Get_Device_IDW(devInfoData.DevInst, instanceId, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
            std::wstring currentDevice(instanceId);

            // // SALVAGUARDIA: Non tocchiamo la periferica di gioco (es. la GPU dedicata)
            // if (currentDevice.find(myGameGpuId) != std::wstring::npos) {
            //     continue;
            // }

            // Applica la restrizione a qualsiasi altra periferica di sistema sconosciuta
            MoveInterruptAwayFromGameCores(currentDevice, safeCpuMask);
            // 2. Riavvia il dispositivo al volo per applicare la modifica
            if (RestartDeviceProgrammatically(hDevInfo, devInfoData)) {
                std::wcout << L"Dispositivo riavviato con successo: " << currentDevice << std::endl;
            } else {
                countImpossible++;
                std::wcout << L"Impossibile riavviare al volo: " << currentDevice << L" (Richiederà il riavvio del PC)" << std::endl;
            }
        }
    }
    SetupDiDestroyDeviceInfoList(hDevInfo);
    return countImpossible > 0;
}

int main() {
    // ID parziale della tua GPU (trovato a runtime o passato come configurazione)
    // std::wstring myGameGpu = L"VEN_10DE"; // Esempio: NVIDIA
    std::cout << "Spostamento degli interrupt hardware sconosciuti sui core di sicurezza..." << std::endl;

    uint64_t ncores = 3;
    uint64_t final_cores = 0;
    unsigned long mask = 0;
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int totalCores = sysInfo.dwNumberOfProcessors; // Nel tuo caso: 12 logici
    for (int i = totalCores - 1; i >= 0 && ncores > 0; i--) {
        mask = mask | (1 << i);
        final_cores++;
        ncores--;
    }
    AutomateSoftRealTimeCoresWinIOT(final_cores);
    //AutomateCoreIsolationWin11Pro(final_cores);
    SetSystemCpuSet(mask);

    IsolateGameCoresProgrammatically();
    std::cout << "Fatto. Nota: Le modifiche hardware di Windows richiedono un riavvio dei dispositivi o del PC per essere lette dal Kernel." << std::endl;
    return 0;
}