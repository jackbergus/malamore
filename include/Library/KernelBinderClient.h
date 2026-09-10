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

#pragma once
#include <windows.h>
#include <string>

class KernelBinderClient {
private:
    HANDLE m_hDevice;
    void Cleanup();
public:
    KernelBinderClient();
    ~KernelBinderClient();
    bool SpawnNode(const std::string& nodeName, const std::string& dllPath);
    bool RegisterPeriodicTask(const std::string& nodeName, const std::string& taskName, ULONG periodUs, ULONG wcetUs);
    bool ConnectNodes(const std::string& sourceNode, const std::string& targetNode);
    bool SignalNode(const std::string& nodeName, const uint8_t* data, ULONG length, uint8_t* outResponse,
    ULONG* outLength);
    bool DestroyNode(const std::string& nodeName);
};