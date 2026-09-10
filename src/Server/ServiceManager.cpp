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
#include <windows.h>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <mutex>

struct TaskAllocation {
    std::string TaskName;
    double Utilization;
};

struct NodeContext {
    std::string Name;
    PROCESS_INFORMATION ProcInfo;
    DWORD AssignedCoreIndex;
    HANDLE hSharedParcelMapping;
    HANDLE hLaunchEvent;
    bool IsSharedCore;
    std::vector<TaskAllocation> AllocatedTasks;
};

std::map<std::string, NodeContext> g_NodesTable;
std::map<std::string, HANDLE> g_ChannelsTable;
std::vector<bool> g_AvailableCores;
std::vector<double> g_CoreUtilization;
DWORD g_TotalCores = 0;
std::mutex g_RegistryMutex;
DWORD g_LastSharedCoreUsed = 1;

void InitializeCpuTopology() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    g_TotalCores = sysInfo.dwNumberOfProcessors;
    g_AvailableCores.resize(g_TotalCores, true);
    g_CoreUtilization.resize(g_TotalCores, 0.0);
    g_AvailableCores[0] = false;
    g_CoreUtilization[0] = 1.0;
    std::cout << "[SERVICE MANAGER] Rilevati " << g_TotalCores << " core logici. Isolamento Core 0 attivo.\n";
}

void HandleSpawnNode(BinderParcel* parcel) {
    std::lock_guard<std::mutex> lock(g_RegistryMutex);
    std::string name(parcel->TargetNodeName);
    std::string dll(parcel->DllPath);

    int targetCore = -1;
    bool isShared = false;

    // Se ci sono core dedicati vuoti, li assegniamo direttamente
    for (DWORD i = 1; i < g_TotalCores; ++i) {
        if (g_AvailableCores[i]) {
            targetCore = i;
            g_AvailableCores[i] = false;
            break;
        }
    }

    // Altrimenti, forziamo il multitasking multiprocesso M:N co-allocando sui core esistenti
    if (targetCore == -1) {
        isShared = true;
        targetCore = g_LastSharedCoreUsed;
        g_LastSharedCoreUsed = (g_LastSharedCoreUsed % (g_TotalCores - 1)) + 1;
    }

    std::wstring wName = std::wstring(name.begin(), name.end());
    std::wstring wDll = std::wstring(dll.begin(), dll.end());

    NodeContext ctx;
    ctx.Name = name;
    ctx.AssignedCoreIndex = targetCore;
    ctx.IsSharedCore = isShared;
    ctx.hSharedParcelMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(BinderParcel), NULL);
    ctx.hLaunchEvent = CreateEventW(NULL, FALSE, FALSE, (L"Global\\LaunchEv_" + wName).c_str());

    auto* pMem = (BinderParcel*)MapViewOfFile(ctx.hSharedParcelMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BinderParcel));
    std::memcpy(pMem, parcel, sizeof(BinderParcel));
    UnmapViewOfFile(pMem);

    STARTUPINFOW si = { sizeof(si) };
    std::wstring cmd = L"KernelServer.exe " + wName + L" " + wDll + L" " +
                       std::to_wstring((ULONG_PTR)ctx.hSharedParcelMapping) + L" " +
                       std::to_wstring((ULONG_PTR)ctx.hLaunchEvent);

    // CORREZIONE: Passa 'cmd.data()' invece dell'oggetto std::wstring crudo
    BOOL success = CreateProcessW(
        NULL,
        cmd.data(), // <--- Sostituito qui per la conversione corretta in LPWSTR
        NULL,
        NULL,
        TRUE,
        REALTIME_PRIORITY_CLASS | CREATE_NEW_CONSOLE | CREATE_SUSPENDED,
        NULL,
        NULL,
        &si,
        &ctx.ProcInfo
    );

    if (success) {
        DWORD_PTR affinityMask = (DWORD_PTR)1 << targetCore;
        SetProcessAffinityMask(ctx.ProcInfo.hProcess, affinityMask);
        ResumeThread(ctx.ProcInfo.hThread);
        g_NodesTable[name] = ctx;
        parcel->ResponseStatus = STATUS_BINDER_OK;
        std::cout << "[SERVICE MANAGER] Spawnato '" << name << "' sul Core " << targetCore << (isShared ? " [CONDIVISO]" : " [DEDICATO]") << "\n";
    } else {
        if (!isShared) g_AvailableCores[targetCore] = true;
        parcel->ResponseStatus = STATUS_BINDER_ERROR;
    }
}

void HandleRegisterPeriodicTask(BinderParcel* parcel) {
    std::lock_guard<std::mutex> lock(g_RegistryMutex);
    std::string name(parcel->TargetNodeName);
    std::string taskName(parcel->TaskMeta.TaskName);

    if (g_NodesTable.find(name) != g_NodesTable.end()) {
        NodeContext& ctx = g_NodesTable[name];
        DWORD core = ctx.AssignedCoreIndex;

        double taskUtilization = 0.0;
        if (parcel->TaskMeta.PeriodUs > 0) {
            taskUtilization = (double)parcel->TaskMeta.EstimatedWcetUs / (double)parcel->TaskMeta.PeriodUs;
        }

        // TEOREMA DI AMMISSIBILITÀ EDF CONTROLLATO ALL'ATTO DELLA REGISTRAZIONE
        if (g_CoreUtilization[core] + taskUtilization > 1.0) {
            std::cerr << "[🚨 EDF REJECTION] Task '" << taskName << "' rifiutato sul Core " << core
                      << ". Carico attuale: " << (g_CoreUtilization[core] * 100.0) << "% + Richiesto: "
                      << (taskUtilization * 100.0) << "% > 100%\n";
            parcel->ResponseStatus = STATUS_BINDER_CORE_EXHAUSTED;
            return;
        }

        // Accetta l'allocazione e incrementa la telemetria del core
        g_CoreUtilization[core] += taskUtilization;
        TaskAllocation alloc = { taskName, taskUtilization };
        ctx.AllocatedTasks.push_back(alloc);

        auto* pMem = (BinderParcel*)MapViewOfFile(ctx.hSharedParcelMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BinderParcel));
        std::memcpy(&pMem->TaskMeta, &parcel->TaskMeta, sizeof(TaskRealtimeMeta));
        pMem->CommandType = CMD_REGISTER_PERIODIC_TASK;
        UnmapViewOfFile(pMem);

        SetEvent(ctx.hLaunchEvent);
        parcel->ResponseStatus = STATUS_BINDER_OK;
        std::cout << "[SERVICE MANAGER] Task '" << taskName << "' registrato con successo. Carico totale Core "
                  << core << ": " << (g_CoreUtilization[core] * 100.0) << "%\n";
    }
}

void HandleDestroyNode(BinderParcel* parcel) {
    std::lock_guard<std::mutex> lock(g_RegistryMutex);
    std::string name(parcel->TargetNodeName);

    if (g_NodesTable.find(name) != g_NodesTable.end()) {
        NodeContext& ctx = g_NodesTable[name];
        DWORD core = ctx.AssignedCoreIndex;

        // DE-ALLOCAZIONE E DECREMENTO DEL CARICO COMPUTAZIONALE EDF
        double totalFreedUtilization = 0.0;
        for (const auto& task : ctx.AllocatedTasks) {
            totalFreedUtilization += task.Utilization;
            std::cout << "[SERVICE MANAGER] Rimozione task '" << task.TaskName << "' dal core " << core << "\n";
        }

        g_CoreUtilization[core] -= totalFreedUtilization;
        if (g_CoreUtilization[core] < 0.0) g_CoreUtilization[core] = 0.0;

        // Se era un core dedicato, lo reimpostiamo come completamente libero
        if (!ctx.IsSharedCore) {
            g_AvailableCores[core] = true;
        }

        // Spegnimento forzato dell'eseguibile a Ring 3
        TerminateProcess(ctx.ProcInfo.hProcess, 0);

        CloseHandle(ctx.ProcInfo.hProcess);
        CloseHandle(ctx.ProcInfo.hThread);
        CloseHandle(ctx.hSharedParcelMapping);
        CloseHandle(ctx.hLaunchEvent);

        g_NodesTable.erase(name);
        parcel->ResponseStatus = STATUS_BINDER_OK;
        std::cout << "[SERVICE MANAGER] Nodo '" << name << "' terminato. Carico residuo Core "
                  << core << ": " << (g_CoreUtilization[core] * 100.0) << "%\n";
    } else {
        parcel->ResponseStatus = STATUS_BINDER_ERROR;
    }
}

void HandleConnectNodes(BinderParcel* parcel) {
    std::lock_guard<std::mutex> lock(g_RegistryMutex);
    std::string src(parcel->SourceNodeName);
    std::string dst(parcel->TargetNodeName);
    std::wstring wSrc = std::wstring(src.begin(), src.end());
    std::wstring wDst = std::wstring(dst.begin(), dst.end());
    std::wstring chName = L"Global\\Chan_" + wSrc + L"_" + wDst;

    HANDLE hChannel = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(InterProcessChannelData), chName.c_str());
    g_ChannelsTable[src + "_" + dst] = hChannel;
    parcel->ResponseStatus = STATUS_BINDER_OK;
}

void HandleSignalNode(BinderParcel* parcel) {
    std::lock_guard<std::mutex> lock(g_RegistryMutex);
    std::string name(parcel->TargetNodeName);
    if (g_NodesTable.find(name) != g_NodesTable.end()) {
        NodeContext& ctx = g_NodesTable[name];
        auto* pMem = (BinderParcel*)MapViewOfFile(ctx.hSharedParcelMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BinderParcel));
        pMem->CommandType = CMD_SIGNAL_NODE;
        std::memcpy(pMem->PayloadData, parcel->PayloadData, parcel->DataLength);
        pMem->DataLength = parcel->DataLength;
        UnmapViewOfFile(pMem);

        SetEvent(ctx.hLaunchEvent);

        HANDLE hCompleteEv = OpenEventW(SYNCHRONIZE, FALSE, (L"Global\\CompleteEv_" + std::wstring(name.begin(), name.end())).c_str());
        if (hCompleteEv) {
            WaitForSingleObject(hCompleteEv, INFINITE);
            pMem = (BinderParcel*)MapViewOfFile(ctx.hSharedParcelMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BinderParcel));
            std::memcpy(parcel->PayloadData, pMem->PayloadData, pMem->DataLength);
            parcel->DataLength = pMem->DataLength;
            UnmapViewOfFile(pMem);
            CloseHandle(hCompleteEv);
        }
        parcel->ResponseStatus = STATUS_BINDER_OK;
    }
}

int main() {
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    InitializeCpuTopology();

    HANDLE hDriver = CreateFileW(L"\\\\.\\KernelBinderLink", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE hManagerEvent = CreateEventW(NULL, FALSE, FALSE, L"Global\\BinderManagerEvent");
    DeviceIoControl(hDriver, IOCTL_REGISTER_SERVICE_MANAGER, &hManagerEvent, sizeof(HANDLE), NULL, 0, NULL, NULL);

    std::cout << "[BINDER SYSTEM DAEMON] In ascolto con validazione EDF attiva...\n";

    while (true) {
        WaitForSingleObject(hManagerEvent, INFINITE);
        BinderParcel parcel;
        DWORD bytes;
        DeviceIoControl(hDriver, IOCTL_BINDER_COMMAND, NULL, 0, &parcel, sizeof(BinderParcel), &bytes, NULL);

        if (parcel.CommandType == CMD_SPAWN_NODE) HandleSpawnNode(&parcel);
        else if (parcel.CommandType == CMD_CONNECT_NODES) HandleConnectNodes(&parcel);
        else if (parcel.CommandType == CMD_REGISTER_PERIODIC_TASK) HandleRegisterPeriodicTask(&parcel);
        else if (parcel.CommandType == CMD_SIGNAL_NODE) HandleSignalNode(&parcel);
        else if (parcel.CommandType == CMD_DESTROY_NODE) HandleDestroyNode(&parcel);

        DeviceIoControl(hDriver, IOCTL_BINDER_COMMAND, &parcel, sizeof(BinderParcel), NULL, 0, &bytes, NULL);
    }
    return 0;
}