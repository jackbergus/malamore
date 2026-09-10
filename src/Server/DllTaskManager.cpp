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
