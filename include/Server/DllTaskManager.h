//
// Created by giaco on 10/09/2026.
//

#ifndef MALAMORE_DLLTASKMANAGER_H
#define MALAMORE_DLLTASKMANAGER_H
// #pragma once
#include <windows.h>
#include <string>

typedef void (*GenericTaskFunc)(void* parcel, void* inputChannel, void* outputChannel);

class DllTaskManager {
private:
    std::string m_DllPath;
    HMODULE m_hDll;
    GenericTaskFunc m_TaskFunction;

public:
    DllTaskManager(const std::string& dllPath);
    ~DllTaskManager();

    bool LoadTask();
    bool Execute(void* pParcel, void* pInChannel, void* pOutChannel);
    void Unload();
};

#endif //MALAMORE_DLLTASKMANAGER_H