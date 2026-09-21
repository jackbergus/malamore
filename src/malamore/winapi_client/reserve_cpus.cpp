//
// Created by Giacomo Bergami on 16/09/2026.
// https://github.com/valleyofdoom/ReservedCpuSets/blob/main/ReservedCpuSetsDLL/ReservedCpuSetsDLL.cpp
//

#include <iostream>
#include <Windows.h>
#include <string>
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "advapi32.lib")

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

 int tryToReserveHighPerfCPUs(unsigned long mask) {
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

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

// --- Constants and Types ---

struct CpuInfo {
    int cpuIndex;
    int coreIndex;
    int group;
    int efficiencyClass;
};

struct DeviceInfo {
    std::wstring name;
    std::wstring policy;
    std::wstring cores;
    std::wstring deviceId;
    std::wstring regPath;
    bool selected = false;
};

const std::vector<std::wstring> INCLUDE_CLASS_GUIDS = {
    L"{4d36e968-e325-11ce-bfc1-08002be10318}", // Display
    L"{4d36e972-e325-11ce-bfc1-08002be10318}", // Net
    L"{4d36e96c-e325-11ce-bfc1-08002be10318}", // Media
    L"{36fc9e60-c465-11cf-8056-444553540000}"  // USB
};

const std::vector<std::wstring> AUDIO_BUS_SERVICES = { L"HDAudBus", L"IntcAudioBus" };

std::map<int, std::wstring> POLICY_NAMES = {
    {0, L"MachineDefault"},
    {1, L"AllCloseProcessors"},
    {2, L"OneCloseProcessor"},
    {3, L"AllProcessors"},
    {4, L"SpecifiedProcessors"},
    {5, L"SpreadMessages"},
    {6, L"Steered (system)"}
};

// --- Utilities ---

std::wstring stringToWString(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::vector<wchar_t> buf(len);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, buf.data(), len);
    return std::wstring(buf.data());
}

std::string wstringToString(const std::wstring& s) {
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, NULL, 0, NULL, NULL);
    std::vector<char> buf(len);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, buf.data(), len, NULL, NULL);
    return std::string(buf.data());
}

std::wstring convertToCoreList(uint64_t mask) {
    if (mask == 0) return L"-";
    std::vector<int> set;
    for (int i = 0; i < 64; ++i) {
        if ((mask >> i) & 1) set.push_back(i);
    }
    if (set.empty()) return L"-";

    std::wstring result = L"";
    int start = set[0];
    int prev = set[0];
    for (size_t i = 1; i <= set.size(); ++i) {
        if (i == set.size() || set[i] != prev + 1) {
            if (!result.empty()) result += L",";
            if (start == prev) result += std::to_wstring(start);
            else result += std::to_wstring(start) + L"-" + std::to_wstring(prev);
            if (i < set.size()) {
                start = set[i];
                prev = set[i];
            }
        } else {
            prev = set[i];
        }
    }
    return result;
}

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

uint64_t convertToMask(const std::vector<uint8_t>& bytes) {
    uint64_t mask = 0;
    size_t len = std::min(bytes.size(), (size_t)8);
    memcpy(&mask, bytes.data(), len);
    return mask;
}

std::vector<CpuInfo> getCpuTopology() {
    std::vector<CpuInfo> cpus;
    DWORD len = 0;
    GetSystemCpuSetInformation(NULL, 0, &len, GetCurrentProcess(), 0);
    if (len == 0) return cpus;

    std::vector<uint8_t> buffer(len);
    if (GetSystemCpuSetInformation((PSYSTEM_CPU_SET_INFORMATION)buffer.data(), len, &len, GetCurrentProcess(), 0)) {
        uint8_t* ptr = buffer.data();
        while (ptr < buffer.data() + len) {
            PSYSTEM_CPU_SET_INFORMATION info = (PSYSTEM_CPU_SET_INFORMATION)ptr;
            if (info->Type == CpuSetInformation) {
                cpus.push_back({
                    (int)info->CpuSet.LogicalProcessorIndex,
                    (int)info->CpuSet.CoreIndex,
                    (int)info->CpuSet.Group,
                    (int)info->CpuSet.EfficiencyClass
                });
            }
            ptr += info->Size;
        }
    }
    return cpus;
}

// --- Registry Logic ---

std::wstring getRegValueString(HKEY hKey, const std::wstring& subKey, const std::wstring& valueName) {
    wchar_t buffer[512];
    DWORD size = sizeof(buffer);
    if (RegGetValueW(hKey, subKey.c_str(), valueName.c_str(), RRF_RT_REG_SZ, NULL, buffer, &size) == ERROR_SUCCESS) {
        std::wstring val(buffer);
        if (val.find(L"@") == 0) {
            size_t pos = val.find(L";");
            if (pos != std::wstring::npos && pos + 1 < val.length()) {
                return val.substr(pos + 1);
            }
        }
        return val;
    }
    return L"";
}

#ifndef MAX_DEVICE_ID_LEN
#define MAX_DEVICE_ID_LEN   (200)
#endif


std::vector<DeviceInfo> scanPciDevices(bool showAll = true) {
    std::vector<DeviceInfo> devices;
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(&GUID_DEVCLASS_SYSTEM, L"PCI", NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) return devices;

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &devInfoData); ++i) {
        wchar_t deviceId[MAX_DEVICE_ID_LEN];
        SetupDiGetDeviceInstanceIdW(deviceInfoSet, &devInfoData, deviceId, MAX_DEVICE_ID_LEN, NULL);

        wchar_t friendlyName[512] = { 0 };
        if (!SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &devInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)friendlyName, sizeof(friendlyName), NULL)) {
            SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &devInfoData, SPDRP_DEVICEDESC, NULL, (PBYTE)friendlyName, sizeof(friendlyName), NULL);
        }
        std::wstring name = friendlyName;
        if (name.find(L"@") == 0) {
            size_t pos = name.find(L";");
            if (pos != std::wstring::npos) name = name.substr(pos + 1);
        }

        wchar_t classGuidStr[64];
        SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &devInfoData, SPDRP_CLASSGUID, NULL, (PBYTE)classGuidStr, sizeof(classGuidStr), NULL);

        wchar_t serviceName[256] = { 0 };
        SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &devInfoData, SPDRP_SERVICE, NULL, (PBYTE)serviceName, sizeof(serviceName), NULL);

        if (!showAll) {
            bool match = false;
            for (const auto& g : INCLUDE_CLASS_GUIDS) if (_wcsicmp(g.c_str(), classGuidStr) == 0) match = true;
            for (const auto& s : AUDIO_BUS_SERVICES) if (_wcsicmp(s.c_str(), serviceName) == 0) match = true;
            if (!match) continue;
        }

        std::wstring regPath = L"SYSTEM\\CurrentControlSet\\Enum\\" + std::wstring(deviceId) + L"\\Device Parameters\\Interrupt Management\\Affinity Policy";

        std::wstring policy = L"Default";
        std::wstring cores = L"-";
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD dp = 0, dpSize = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"DevicePolicy", NULL, NULL, (LPBYTE)&dp, &dpSize) == ERROR_SUCCESS) {
                policy = POLICY_NAMES.count(dp) ? POLICY_NAMES[dp] : L"Unknown (" + std::to_wstring(dp) + L")";
            }
            BYTE aso[8] = { 0 };
            DWORD asoSize = sizeof(aso);
            DWORD type = 0;
            if (RegQueryValueExW(hKey, L"AssignmentSetOverride", NULL, &type, aso, &asoSize) == ERROR_SUCCESS) {
                std::vector<uint8_t> v(aso, aso + asoSize);
                uint64_t m = convertToMask(v);
                cores = convertToCoreList(m);
            }
            RegCloseKey(hKey);
        }

        devices.push_back({ name, policy, cores, deviceId, regPath });
    }
    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return devices;
}

// --- Persistence ---

void saveBackup(const std::vector<DeviceInfo>& selected, const std::string& filename) {
    json j = json::array();
    for (const auto& d : selected) {
        HKEY hKey;
        json devJson = { {"path", wstringToString(d.regPath)}, {"name", wstringToString(d.name)} };
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, d.regPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD dp = 0, dpSize = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"DevicePolicy", NULL, NULL, (LPBYTE)&dp, &dpSize) == ERROR_SUCCESS) {
                devJson["DevicePolicy"] = dp;
            }
            BYTE aso[8] = { 0 };
            DWORD asoSize = sizeof(aso);
            if (RegQueryValueExW(hKey, L"AssignmentSetOverride", NULL, NULL, aso, &asoSize) == ERROR_SUCCESS) {
                std::vector<uint8_t> v(aso, aso + asoSize);
                devJson["AssignmentSetOverride"] = v;
            }
            RegCloseKey(hKey);
        }
        j.push_back(devJson);
    }
    std::ofstream o(filename);
    o << j.dump(4);
}

void restoreBackup(const std::string& filename) {
    std::ifstream i(filename);
    if (!i.is_open()) {
        std::cerr << "Could not open backup file." << std::endl;
        return;
    }
    json j;
    i >> j;
    for (const auto& item : j) {
        std::wstring path = stringToWString(item["path"]);
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            if (item.contains("DevicePolicy")) {
                DWORD dp = item["DevicePolicy"];
                RegSetValueExW(hKey, L"DevicePolicy", 0, REG_DWORD, (const BYTE*)&dp, sizeof(DWORD));
            } else {
                RegDeleteValueW(hKey, L"DevicePolicy");
            }
            if (item.contains("AssignmentSetOverride")) {
                std::vector<uint8_t> v = item["AssignmentSetOverride"];
                RegSetValueExW(hKey, L"AssignmentSetOverride", 0, REG_BINARY, v.data(), (DWORD)v.size());
            } else {
                RegDeleteValueW(hKey, L"AssignmentSetOverride");
            }
            RegCloseKey(hKey);
            std::wcout << L"Restored: " << stringToWString(item["name"]) << std::endl;
        }
    }
}

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"



using namespace ftxui;

void runInteractive(bool showAll, bool reset) {
    auto devices = scanPciDevices(showAll);
    if (devices.empty()) {
        std::cout << "No devices found." << std::endl;
        return;
    }

    int selected_device = 0;
    std::vector<std::string> device_entries;
    for (const auto& d : devices) {
        device_entries.push_back(wstringToString(d.name + L" [" + d.policy + L"]"));
    }

    auto screen = ScreenInteractive::Fullscreen();

    auto menu = Menu(&device_entries, &selected_device);
    auto toggle_device = [&]() {
        devices[selected_device].selected = !devices[selected_device].selected;
    };

    auto component = Container::Vertical({
        Renderer(menu, [&] {
            return vbox({
                text("Select devices (Space to toggle, Enter to continue):") | bold,
                menu->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 15),
                text("Selected: " + std::to_string(std::count_if(devices.begin(), devices.end(), [](const DeviceInfo& d){return d.selected;})))
            });
        }),
    });

    component |= CatchEvent([&](Event event) {
        if (event == Event::Character(' ')) {
            toggle_device();
            return true;
        }
        if (event == Event::Return) {
            screen.ExitLoopClosure()();
            return true;
        }
        return false;
    });

    screen.Loop(component);

    std::vector<DeviceInfo> selectedDevices;
    for (auto& d : devices) if (d.selected) selectedDevices.push_back(d);

    if (selectedDevices.empty()) return;

    uint64_t mask = 0;
    if (!reset) {
        auto cpus = getCpuTopology();
        std::vector<std::string> cpu_entries;
        std::vector<bool> cpu_selected(cpus.size(), false);
        for (const auto& c : cpus) {
            std::string type = (c.efficiencyClass > 0) ? "P-Core" : "E-Core";
            cpu_entries.push_back("CPU " + std::to_string(c.cpuIndex) + " (" + type + ")");
        }

        int selected_cpu = 0;
        auto cpu_menu = Menu(&cpu_entries, &selected_cpu);
        auto cpu_component = Container::Vertical({
            Renderer(cpu_menu, [&] {
                return vbox({
                    text("Select CPU cores for interrupts (Space to toggle, Enter to apply):") | bold,
                    cpu_menu->Render() | vscroll_indicator | frame | size(HEIGHT, LESS_THAN, 15),
                });
            }),
        });

        cpu_component |= CatchEvent([&](Event event) {
            if (event == Event::Character(' ')) {
                cpu_selected[selected_cpu] = !cpu_selected[selected_cpu];
                return true;
            }
            if (event == Event::Return) {
                screen.ExitLoopClosure()();
                return true;
            }
            return false;
        });

        screen.Loop(cpu_component);

        for (size_t i = 0; i < cpus.size(); ++i) {
            if (cpu_selected[i]) mask |= (1ULL << cpus[i].cpuIndex);
        }
    }

    // Apply changes
    saveBackup(selectedDevices, "affinity_backup.json");
    for (const auto& d : selectedDevices) {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, d.regPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            if (reset) {
                RegDeleteValueW(hKey, L"DevicePolicy");
                RegDeleteValueW(hKey, L"AssignmentSetOverride");
                std::wcout << L"Reset: " << d.name << std::endl;
            } else {
                DWORD dp = 4;
                RegSetValueExW(hKey, L"DevicePolicy", 0, REG_DWORD, (const BYTE*)&dp, sizeof(DWORD));
                RegSetValueExW(hKey, L"AssignmentSetOverride", 0, REG_BINARY, (const BYTE*)&mask, sizeof(uint64_t));
                std::wcout << L"Pinned: " << d.name << L" to " << convertToCoreList(mask) << std::endl;
            }
            RegCloseKey(hKey);
        }
    }
}

void cherryPickDeviceToHighMask(uint64_t mask, bool showAll) {
    auto devices = scanPciDevices(showAll);
    auto cpus = getCpuTopology();
    // uint64_t mask = 0;
    // // for (int i = 0; i < std::min((int)cpus.size(), nCores); ++i) {
    // //     mask |= (1ULL << cpus[i].cpuIndex);
    // // }
    // // mask = ~mask;
    // // uint64_t mask = 0;
    // // for (int i = 0; i < std::min((int)cpus.size(), nCores); ++i) {
    // //     mask |= (1ULL << cpus[i].cpuIndex);
    // // }
    // // mask = ~mask;
    // for (int i = ((int)cpus.size()) - 1; i >= 0 && nCores > 0; i--) {
    //     mask = mask | (1 << cpus[i].cpuIndex);
    //     // final_cores++;
    //     nCores--;
    // }
    saveBackup(devices, "affinity_backup_auto.json");
    for (const auto& d : devices) {
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, d.regPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            DWORD dp = 4;
            RegSetValueExW(hKey, L"DevicePolicy", 0, REG_DWORD, (const BYTE*)&dp, sizeof(DWORD));
            RegSetValueExW(hKey, L"AssignmentSetOverride", 0, REG_BINARY, (const BYTE*)&mask, sizeof(uint64_t));
            RegCloseKey(hKey);
        }
    }
    std::cout << "Applied mask 0x" << std::hex << mask << " to all devices." << std::endl;
}