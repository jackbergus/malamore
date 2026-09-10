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
//
// Created by giaco on 10/09/2026.
//

#include "Server/DllTaskManager.h"
#include <iostream>

DllTaskManager::DllTaskManager(const std::string& dllPath)
    : m_DllPath(dllPath), m_hDll(nullptr), m_TaskFunction(nullptr) {}

DllTaskManager::~DllTaskManager() {
    Unload();
}

bool DllTaskManager::LoadTask() {
    if (m_hDll) return true;
    m_hDll = LoadLibraryA(m_DllPath.c_str());
    if (!m_hDll) return false;

    m_TaskFunction = (GenericTaskFunc)GetProcAddress(m_hDll, "ExecuteCustomTask");
    if (!m_TaskFunction) {
        Unload();
        return false;
    }
    return true;
}

bool DllTaskManager::Execute(void* pParcel, void* pInChannel, void* pOutChannel) {
    if (!m_TaskFunction) return false;
    m_TaskFunction(pParcel, pInChannel, pOutChannel);
    return true;
}

void DllTaskManager::Unload() {
    if (m_hDll) {
        FreeLibrary(m_hDll);
        m_hDll = nullptr;
        m_TaskFunction = nullptr;
    }
}
