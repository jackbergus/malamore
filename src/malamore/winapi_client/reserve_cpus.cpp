//
// Created by Giacomo Bergami on 16/09/2026.
// https://github.com/valleyofdoom/ReservedCpuSets/blob/main/ReservedCpuSetsDLL/ReservedCpuSetsDLL.cpp
//

#include <iostream>
#include <Windows.h>
#include <string>

typedef enum _SYSTEM_INFORMATION_CLASS {
    SystemAllowedCpuSetsInformation = 168,
    SystemCpuSetInformation = 175,
    SystemCpuSetTagInformation = 176,
} SYSTEM_INFORMATION_CLASS;

#pragma comment(lib, "ntdll.lib")
extern "C" NTSTATUS NTAPI NtSetSystemInformation(_In_ SYSTEM_INFORMATION_CLASS SystemInformationClass, _In_reads_bytes_opt_(SystemInformationLength) PVOID SystemInformation, _In_ ULONG SystemInformationLength);

template<typename T>
int ParseCPUSet(T* pSet, const char* text, int& count) {
    bool last = false;

    for (int i = 0; !last && i < 32; i++) {
        auto comma = strchr(text, ',');

        if (comma == text) {
            return 1;
        }

        if (comma == nullptr) {
            comma = text + strlen(text);
            last = true;
        }

        size_t len = comma - text;
        bool hex = false;

        if (len > 2 && (::strncmp(text, "0x", 2) == 0 || ::strncmp(text, "0X", 2) == 0)) {
            hex = true;
        }

        int value = std::stoi(std::string(text + (hex ? 2 : 0), comma), nullptr, hex ? 16 : 10);
        pSet[i] = value;
        text = comma + 1;
        count = i + 1;
    }

    return 0;
}

int SetPrivilege(HANDLE hToken, PCTSTR lpszPrivilege, bool bEnablePrivilege) {
    TOKEN_PRIVILEGES tp;
    LUID luid;

    if (!::LookupPrivilegeValue(nullptr, lpszPrivilege, &luid)) {
        return 1;
    }

    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = bEnablePrivilege ? SE_PRIVILEGE_ENABLED : 0;

    // enable the privilege or disable all privileges.

    if (!::AdjustTokenPrivileges(hToken, false, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr)) {
        return 1;
    }

    if (::GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        return 1;
    }

    return 0;
}

 int SetSystemCpuSet(unsigned long mask) {
    std::string str = std::to_string(mask);
    const char* affinity = str.c_str();

    HANDLE hToken = nullptr;
    ::OpenProcessToken(::GetCurrentProcess(), TOKEN_ALL_ACCESS, &hToken);

    if (SetPrivilege(hToken, SE_INC_BASE_PRIORITY_NAME, 1)) {
        std::cerr << "Access denied. Try running from an elevated command prompt.\n";
        return -1;
    }
    ::CloseHandle(hToken);

    ULONG cpuSet[64];
    ULONG64 systemCpuSet[20];
    int count = 0;

    if (ParseCPUSet(systemCpuSet, affinity, count)) {
        std::cerr << "System CPU set list error\n";
        return -2;
    }

    auto pSet = systemCpuSet;
    if (count == 1 && cpuSet[0] == 0) {
        pSet = nullptr;
        count = 0;
    }

    if (::NtSetSystemInformation(SystemAllowedCpuSetsInformation, pSet, count * sizeof(ULONG64))) {
        std::cerr << "Failed to change system CPU set\n";
        return -3;
    }

    return 1;
}

#include <windows.h>
#include <comdef.h>
#include <WbemIdl.h>
#include <iostream>

#pragma comment(lib, "wbemuuid.lib")

bool AutomateSoftRealTimeCoresWinIOT(LONG nCpus) {
    // 1. Inizializzazione della barriera hardware COM sul thread corrente
    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        std::cerr << "[MALAMORE COM] Fallimento CoInitializeEx: " << std::hex << hr << std::endl;
        return false;
    }

    // 2. Configurazione dei parametri di sicurezza del processo
    hr = CoInitializeSecurity(
        NULL, -1, NULL, NULL,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE, // Permette l'impersonificazione dei permessi SYSTEM
        NULL, EOAC_NONE, NULL
    );

    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::cerr << "[MALAMORE COM] Fallimento CoInitializeSecurity: " << std::hex << hr << std::endl;
        CoUninitialize();
        return false;
    }

    IWbemLocator* pLoc = NULL;
    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hr)) {
        std::cerr << "[MALAMORE WMI] Impossibile istanziare IWbemLocator." << std::endl;
        CoUninitialize();
        return false;
    }

    IWbemServices* pSvc = NULL;
    // Connessione al namespace MDM specificato nello script PowerShell
    hr = pLoc->ConnectServer(
        _bstr_t(L"ROOT\\CIMV2\\MDM\\DMMAP"),
        NULL, NULL, 0, NULL, 0, 0, &pSvc
    );

    if (FAILED(hr)) {
        std::cerr << "[MALAMORE WMI] Impossibile connettersi al namespace MDM. Errore: " << std::hex << hr << std::endl;
        pLoc->Release();
        CoUninitialize();
        return false;
    }

    // Sganciamo la sbarra di autenticazione per le chiamate successive
    hr = CoSetProxyBlanket(
        pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE
    );

    IEnumWbemClassObject* pEnumerator = NULL;
    bstr_t className = L"MDM_WindowsIoT_SoftRealTimeProperties01";

    // 3. Estrazione dell'oggetto CIM memorizzato nel sistema
    hr = pSvc->CreateInstanceEnum(className, WBEM_FLAG_RETURN_IMMEDIATELY | WBEM_FLAG_FORWARD_ONLY, NULL, &pEnumerator);
    if (FAILED(hr)) {
        std::cerr << "[MALAMORE CIM] Impossibile interrogare la classe SoftRealTimeProperties." << std::endl;
        pSvc->Release();
        pLoc->Release();
        CoUninitialize();
        return false;
    }

    IWbemClassObject* pclsObj = NULL;
    ULONG uReturn = 0;
    bool success = false;

    // Recuperiamo la prima istanza valida registrata sulla MFT logica di WMI
    hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
    if (SUCCEEDED(hr) && uReturn > 0) {

        // Modifichiamo il registro: impostiamo la proprietà SetRTCores sul valore geometrico 3
        VARIANT vtProp;
        vtProp.vt = VT_I4; // Tipo di dato: Intero a 32 bit (LONG)
        vtProp.lVal = nCpus;   // Numero di core Real-Time dedicati richiesti

        // Equivalente C++ di: $obj.SetRTCores = nCpus
        hr = pclsObj->Put(L"SetRTCores", 0, &vtProp, 0);

        if (SUCCEEDED(hr)) {
            // Sincronizzazione dei metadati: inviamo l'istanza modificata all'I/O Manager di Windows
            // Equivalente C++ di: Set-CimInstance -CimInstance $obj
            hr = pSvc->PutInstance(pclsObj, WBEM_FLAG_UPDATE_ONLY, NULL, NULL);
            if (SUCCEEDED(hr)) {
                std::cout << "[MALAMORE 🔥] Core SoftRealTime configurati stabilmente a 3 sul silicio!" << std::endl;
                success = true;
            } else {
                std::cerr << "[MALAMORE 🚨] PutInstance fallita: WMI ha rifiutato la configurazione hardware hardware. Codice: " << std::hex << hr << std::endl;
            }
        }
        pclsObj->Release();
    }

    // Svuotamento dei descrittori logici per evitare perdite di memoria
    pEnumerator->Release();
    pSvc->Release();
    pLoc->Release();
    CoUninitialize();

    return success;
}

bool AutomateCoreIsolationWin11Pro(int coresToIsolate) {
    // 1. Calcoliamo la maschera esadecimale dei core da mantenere attivi per il sistema operativo.
    // Se la macchina ha 12 core logici (indici 0-11) e vogliamo isolarne 3 per il Real-Time (9, 10, 11),
    // Windows deve usare solo i core da 0 a 8. Maschera binaria: 0000011111111111 = 0x1FF.

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int totalCores = sysInfo.dwNumberOfProcessors; // Nel tuo caso: 12 logici

    if (totalCores <= coresToIsolate) {
        std::cerr << "[MALAMORE] Numero di core totali insufficiente per l'isolamento richiesto." << std::endl;
        return false;
    }

    // Calcoliamo quanti core lasciare a Windows
    int osCores = totalCores - coresToIsolate; // 12 - 3 = 9 core per l'OS

    // Generiamo la maschera di bit (es: 9 bit accesi = 0x1FF)
    ULONG_PTR osAffinityMask = (1ULL << osCores) - 1;

    // Convertiamo la maschera in stringa esadecimale per bcdedit
    char hexMaskStr[32];
    sprintf_s(hexMaskStr, "0x%llX", (unsigned long long)osAffinityMask);

    std::cout << "[MALAMORE] Core totali rilevati: " << totalCores << std::endl;
    std::cout << "[MALAMORE] Isolamento richiesto: " << coresToIsolate << " core." << std::endl;
    std::cout << "[MALAMORE] Calcolo AffinityMask per Windows 11 Pro: " << hexMaskStr << std::endl;

    // 2. Esecuzione dei comandi BCD di Windows NT tramite le API native d'attivazione
    // Il comando "bcdedit /set groupaffinity" definisce quali core logici il Kernel
    // può popolare per i processi standard. Escludendo gli ultimi 3, li isoliamo per Malamore!
    // std::string cmd1 = "bcdedit /set groupaffinity " + std::string(hexMaskStr);
    // std::string cmd2 = "bcdedit /set groupaware on";

     // =========================================================================
    // CORREZIONE STRUTTURALE SINTASSI BCDEDIT: INIEZIONE DEL TARGET {current}
    // =========================================================================
    // Il flag /set esige {current}. Limitando numproc a 9, forziamo il Kernel NT
    // a non toccare ed a non schedulare DPC o processi ordinari sugli ultimi 3 core (9, 10, 11),
    // che rimarranno di proprietà esclusiva della staffetta ad alta priorità di Malamore!
    std::string cmd = "bcdedit /set {current} numproc " + std::to_string(osCores);

    std::cout << "[MALAMORE] Esecuzione comando: " << cmd << std::endl;

    // Eseguiamo il comando amministrativo sul database di boot
    int result = system(cmd.c_str());

    if (result == 0) {
        std::cout << "[MALAMORE 🔥] Sbarra hardware impostata con successo su Windows 11 Pro!" << std::endl;
        std::cout << "[MALAMORE ⚠️] Riavvia il computer per rendere effettivo l'isolamento dei 3 core logici." << std::endl;
        return true;
    }

    // Eseguiamo i comandi in modalità silenziosa ed amministrativa
    // int r1 = system(cmd1.c_str());
    // int r2 = system(cmd2.c_str());
    //
    // if (r1 == 0 && r2 == 0) {
    //     std::cout << "[MALAMORE 🔥] Sbarra hardware impostata con successo su Windows 11 Pro!" << std::endl;
    //     std::cout << "[MALAMORE ⚠️] Riavvia il sistema operativo per rendere effettivo l'isolamento sul silicio." << std::endl;
    //     return true;
    // }

    std::cerr << "[MALAMORE 🚨] Fallimento bcdedit. Accertati di eseguire il programma come AMMINISTRATORE." << std::endl;
    return false;
}
