/// https://raw.githubusercontent.com/vadyaravadim/interrupt-affinity-utility/refs/heads/main/interrupt-affinity-utility.ps1
/// https://www.youtube.com/watch?v=KuCzZRUfLh0
#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <regstr.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "advapi32.lib")

using json = nlohmann::json;

// --- Constants and Types ---

struct CpuInfo {
    int cpu;
    int core;
    int group;
    int effClass;
};

struct DeviceRow {
    std::wstring name;
    std::wstring policy;
    std::wstring cores;
    std::wstring deviceId;
    std::wstring regPath;
    HKEY hKeyBase;
};

const std::vector<std::wstring> INCLUDE_CLASS_GUIDS = {
    L"{4d36e968-e325-11ce-bfc1-08002be10318}", // Display (GPU)
    L"{4d36e972-e325-11ce-bfc1-08002be10318}", // Net (NIC)
    L"{4d36e96c-e325-11ce-bfc1-08002be10318}", // Media (sound cards)
    L"{36fc9e60-c465-11cf-8056-444553540000}"  // USB host controllers
};

const std::vector<std::wstring> AUDIO_BUS_SERVICES = { L"HDAudBus", L"IntcAudioBus" };

std::map<DWORD, std::wstring> POLICY_NAMES = {
    {0, L"MachineDefault"},
    {1, L"AllCloseProcessors"},
    {2, L"OneCloseProcessor"},
    {3, L"AllProcessors"},
    {4, L"SpecifiedProcessors"},
    {5, L"SpreadMessages"},
    {6, L"Steered (system)"}
};

// --- Helper Functions ---

std::wstring getDeviceName(HDEVINFO deviceInfoSet, PSP_DEVINFO_DATA deviceInfoData) {
    wchar_t buffer[MAX_PATH];
    if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, deviceInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)buffer, sizeof(buffer), NULL)) {
        return std::wstring(buffer);
    }
    if (SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, deviceInfoData, SPDRP_DEVICEDESC, NULL, (PBYTE)buffer, sizeof(buffer), NULL)) {
        std::wstring desc(buffer);
        if (desc.length() > 0 && desc[0] == L'@') {
            size_t pos = desc.find(L';');
            if (pos != std::wstring::npos && pos + 1 < desc.length()) {
                return desc.substr(pos + 1);
            }
        }
        return desc;
    }
    return L"";
}

std::vector<CpuInfo> getCpuTopology() {
    std::vector<CpuInfo> cpus;
    DWORD len = 0;
    GetSystemCpuSetInformation(NULL, 0, &len, GetCurrentProcess(), 0);
    if (len == 0) return cpus;

    std::vector<BYTE> buffer(len);
    if (GetSystemCpuSetInformation((PSYSTEM_CPU_SET_INFORMATION)buffer.data(), len, &len, GetCurrentProcess(), 0)) {
        size_t pos = 0;
        while (pos < len) {
            PSYSTEM_CPU_SET_INFORMATION info = (PSYSTEM_CPU_SET_INFORMATION)&buffer[pos];
            if (info->Size == 0) break;
            if (info->Type == CpuSetInformation) {
                CpuInfo cpu;
                cpu.cpu = info->CpuSet.Id; // Logical index
                cpu.core = info->CpuSet.CoreIndex;
                cpu.group = info->CpuSet.Group;
                cpu.effClass = info->CpuSet.EfficiencyClass;
                cpus.push_back(cpu);
            }
            pos += info->Size;
        }
    }
    return cpus;
}

std::wstring convertToCoreList(unsigned __int64 mask) {
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

bool isAdministrator() {
    BOOL isAdmin = FALSE;
    PSID adminGroup;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

// --- Main Logic ---

int main(int argc, char* argv[]) {
    bool showAll = false;
    bool reset = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-ShowAll") showAll = true;
        if (arg == "-Reset") reset = true;
    }

    if (!isAdministrator()) {
        std::cout << "Not running as Administrator. Please run this program as Administrator." << std::endl;
        system("pause");
        return 1;
    }

    std::cout << "Scanning PCI devices..." << std::endl;

    HDEVINFO deviceInfoSet = SetupDiGetClassDevsW(NULL, L"PCI", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (deviceInfoSet == INVALID_HANDLE_VALUE) return 1;

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
    std::vector<DeviceRow> rows;
    int hiddenCount = 0;

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &deviceInfoData); ++i) {
        std::wstring name = getDeviceName(deviceInfoSet, &deviceInfoData);
        if (name.empty()) continue;

        wchar_t instanceId[MAX_DEVICE_ID_LEN];
        SetupDiGetDeviceInstanceIdW(deviceInfoSet, &deviceInfoData, instanceId, MAX_DEVICE_ID_LEN, NULL);

        HKEY hKey = SetupDiOpenDevRegKey(deviceInfoSet, &deviceInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (hKey == INVALID_HANDLE_VALUE) continue;

        std::wstring imSubKey = L"Interrupt Management\\Affinity Policy";
        HKEY hAffinityKey;
        bool hasAffinity = (RegOpenKeyExW(hKey, imSubKey.c_str(), 0, KEY_READ, &hAffinityKey) == ERROR_SUCCESS);

        if (!showAll) {
            wchar_t classGuid[MAX_PATH];
            SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, SPDRP_CLASSGUID, NULL, (PBYTE)classGuid, sizeof(classGuid), NULL);
            wchar_t service[MAX_PATH];
            SetupDiGetDeviceRegistryPropertyW(deviceInfoSet, &deviceInfoData, SPDRP_SERVICE, NULL, (PBYTE)service, sizeof(service), NULL);

            bool included = false;
            for (const auto& g : INCLUDE_CLASS_GUIDS) if (g == classGuid) included = true;
            for (const auto& s : AUDIO_BUS_SERVICES) if (s == service) included = true;

            if (!included) {
                hiddenCount++;
                RegCloseKey(hKey);
                if (hasAffinity) RegCloseKey(hAffinityKey);
                continue;
            }
        }

        std::wstring policyStr = L"Default";
        std::wstring coresStr = L"-";

        if (hasAffinity) {
            DWORD dwPolicy, dwSize = sizeof(DWORD);
            if (RegQueryValueExW(hAffinityKey, L"DevicePolicy", NULL, NULL, (LPBYTE)&dwPolicy, &dwSize) == ERROR_SUCCESS) {
                if (POLICY_NAMES.count(dwPolicy)) policyStr = POLICY_NAMES[dwPolicy];
                else policyStr = L"Unknown (" + std::to_wstring(dwPolicy) + L")";
            }

            BYTE maskBuf[8] = {0};
            dwSize = 8;
            if (RegQueryValueExW(hAffinityKey, L"AssignmentSetOverride", NULL, NULL, maskBuf, &dwSize) == ERROR_SUCCESS) {
                unsigned __int64 mask = 0;
                memcpy(&mask, maskBuf, dwSize);
                coresStr = convertToCoreList(mask);
            }
            RegCloseKey(hAffinityKey);
        }

        DeviceRow row;
        row.name = name;
        row.policy = policyStr;
        row.cores = coresStr;
        row.deviceId = instanceId;
        row.regPath = L"SYSTEM\\CurrentControlSet\\Enum\\" + std::wstring(instanceId) + L"\\Device Parameters\\" + imSubKey;
        rows.push_back(row);
        RegCloseKey(hKey);
    }
    SetupDiDestroyDeviceInfoList(deviceInfoSet);

    if (rows.empty()) {
        std::cout << "No matching devices found. Try -ShowAll." << std::endl;
        system("pause");
        return 0;
    }

    if (hiddenCount > 0) {
        std::cout << hiddenCount << " more device(s) are hidden by default. Use -ShowAll to include them." << std::endl;
    }

    // --- Selection Logic (Simplified for CLI/Automated Requirement) ---
    // Per instructions: "consider all the devices that might be listed"
    // and "force all interrupts to be handled in the first two low cores being available"
    
    std::vector<CpuInfo> cpus = getCpuTopology();
    std::sort(cpus.begin(), cpus.end(), [](const CpuInfo& a, const CpuInfo& b) {
        return a.cpu < b.cpu;
    });

    unsigned __int64 mask = 0;
    int coresFound = 0;
    for (const auto& c : cpus) {
        if (c.group == 0 && c.cpu < 64) {
            mask |= (1ULL << c.cpu);
            coresFound++;
            if (coresFound >= 2) break;
        }
    }

    std::cout << "Targeting first two available cores (Mask: 0x" << std::hex << mask << std::dec << ")" << std::endl;

    // --- Backup Logic (JSON) ---
    json backup;
    for (const auto& d : rows) {
        json devBackup;
        devBackup["Name"] = std::string(d.name.begin(), d.name.end());
        devBackup["RegPath"] = std::string(d.regPath.begin(), d.regPath.end());
        
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, d.regPath.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD dwVal, dwSize = sizeof(DWORD);
            if (RegQueryValueExW(hKey, L"DevicePolicy", NULL, NULL, (LPBYTE)&dwVal, &dwSize) == ERROR_SUCCESS)
                devBackup["DevicePolicy"] = dwVal;
            
            BYTE bVal[8]; dwSize = 8;
            if (RegQueryValueExW(hKey, L"AssignmentSetOverride", NULL, NULL, bVal, &dwSize) == ERROR_SUCCESS) {
                std::vector<uint8_t> vec(bVal, bVal + dwSize);
                devBackup["AssignmentSetOverride"] = vec;
            }
            RegCloseKey(hKey);
        }
        backup["devices"].push_back(devBackup);
    }

    std::ofstream o("affinity_backup.json");
    o << std::setw(4) << backup << std::endl;
    o.close();

    // --- Apply Changes ---
    int updated = 0;
    int failed = 0;

    for (const auto& d : rows) {
        HKEY hKey;
        LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, d.regPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
        if (status == ERROR_SUCCESS) {
            if (reset) {
                RegDeleteValueW(hKey, L"DevicePolicy");
                RegDeleteValueW(hKey, L"AssignmentSetOverride");
                std::wcout << L"  [RESET] " << d.name << std::endl;
            } else {
                DWORD dwPolicy = 4;
                RegSetValueExW(hKey, L"DevicePolicy", 0, REG_DWORD, (const BYTE*)&dwPolicy, sizeof(DWORD));
                RegSetValueExW(hKey, L"AssignmentSetOverride", 0, REG_BINARY, (const BYTE*)&mask, sizeof(unsigned __int64));
                std::wcout << L"  [PIN " << convertToCoreList(mask) << L"] " << d.name << std::endl;
            }
            RegCloseKey(hKey);
            updated++;
        } else {
            std::wcout << L"  [ERR] " << d.name << L" (Code: " << status << L")" << std::endl;
            failed++;
        }
    }

    std::cout << "\nDone. " << updated << " devices updated." << std::endl;
    if (failed > 0) std::cout << failed << " devices failed." << std::endl;
    std::cout << "Restart devices or REBOOT for changes to take effect." << std::endl;

    system("pause");
    return 0;
}
