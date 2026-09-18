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

#include "argparse/argparse.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "nlohmann/json.hpp"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "advapi32.lib")

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

std::vector<DeviceInfo> scanPciDevices(bool showAll) {
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

// --- UI Components ---

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

void runAutomatic(int nCores, bool showAll) {
    auto devices = scanPciDevices(showAll);
    auto cpus = getCpuTopology();
    uint64_t mask = 0;
    for (int i = 0; i < std::min((int)cpus.size(), nCores); ++i) {
        mask |= (1ULL << cpus[i].cpuIndex);
    }

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

int main(int argc, char* argv[]) {
    argparse::ArgumentParser program("interrupt-affinity-utility");

    program.add_argument("--show-all").help("Show all PCI devices").default_value(false).implicit_value(true);
    program.add_argument("--reset").help("Reset affinity to default").default_value(false).implicit_value(true);
    program.add_argument("--auto").help("Automatically pin to lowest N cores").scan<'i', int>();
    program.add_argument("--restore").help("Restore from JSON file");

    try {
        program.parse_args(argc, argv);
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    if (program.present("--restore")) {
        restoreBackup(program.get<std::string>("--restore"));
    } else if (program.present("--auto")) {
        runAutomatic(program.get<int>("--auto"), program.get<bool>("--show-all"));
    } else {
        runInteractive(program.get<bool>("--show-all"), program.get<bool>("--reset"));
    }

    return 0;
}
