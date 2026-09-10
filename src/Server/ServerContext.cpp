//
// Created by giaco on 10/09/2026.
//

#include "Server/ServerContext.h"
#include <stdexcept>
#include <iostream>
#include <string>
#include "Library/KernelBinderClient.h"

ServerContext::ServerContext(const std::string& nodeName, HANDLE hSharedParcel, HANDLE hLaunchEvent)
    : m_hDevice(INVALID_HANDLE_VALUE), m_pParcel(nullptr), m_pInChannel(nullptr), m_pOutChannel(nullptr),
      m_hInMapping(nullptr), m_hOutMapping(nullptr)
{
    m_NodeName = std::wstring(nodeName.begin(), nodeName.end());
    m_hLaunchEvent = hLaunchEvent;
    m_hCompleteEvent = CreateEventW(NULL, FALSE, FALSE, (L"Global\\CompleteEv_" + m_NodeName).c_str());

    m_hDevice = CreateFileW(L"\\\\.\\KernelBinderLink", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_hDevice == INVALID_HANDLE_VALUE || !m_hCompleteEvent) {
        throw std::runtime_error("ServerContext: Impossibile inizializzare le risorse hardware base.");
    }

    m_pParcel = reinterpret_cast<BinderParcel*>(MapViewOfFile(hSharedParcel, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BinderParcel)));
    if (!m_pParcel) {
        throw std::runtime_error("ServerContext: Mappatura transazione fallita.");
    }
}

ServerContext::~ServerContext() {
    if (m_pParcel) UnmapViewOfFile(m_pParcel);
    if (m_pInChannel) UnmapViewOfFile(m_pInChannel);
    if (m_pOutChannel) UnmapViewOfFile(m_pOutChannel);
    if (m_hInMapping) CloseHandle(m_hInMapping);
    if (m_hOutMapping) CloseHandle(m_hOutMapping);
    if (m_hDevice != INVALID_HANDLE_VALUE) CloseHandle(m_hDevice);
    if (m_hCompleteEvent) CloseHandle(m_hCompleteEvent);
}

bool ServerContext::OpenChannels() {
    m_hInMapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, (L"Global\\Chan_NodoProduttore_" + m_NodeName).c_str());
    m_hOutMapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, (L"Global\\Chan_" + m_NodeName + L"_NodoConsumatore").c_str());

    if (m_hInMapping) m_pInChannel = MapViewOfFile(m_hInMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(InterProcessChannelData));
    if (m_hOutMapping) m_pOutChannel = MapViewOfFile(m_hOutMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(InterProcessChannelData));
    return true;
}

void ServerContext::SignalCompletion() {
    if (m_hCompleteEvent) SetEvent(m_hCompleteEvent);
}

bool ServerContext::WaitLaunch(DWORD timeoutMs) {
    return WaitForSingleObject(m_hLaunchEvent, timeoutMs) == WAIT_OBJECT_0;
}
