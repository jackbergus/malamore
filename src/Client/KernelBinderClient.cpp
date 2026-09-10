#include "Client/KernelBinderClient.h"
#include <malamore.h>
#include <stdexcept>
#include <iostream>

// --- Implementazione PinnedMemory ---
PinnedMemory::PinnedMemory(PVOID address, SIZE_T size) : m_Address(address), m_Size(size), m_IsLocked(false) {
    if (m_Address && m_Size > 0) {
        if (VirtualLock(m_Address, m_Size)) m_IsLocked = true;
    }
}
PinnedMemory::~PinnedMemory() {
    if (m_IsLocked && m_Address) VirtualUnlock(m_Address, m_Size);
}

// --- Implementazione KernelBinderClient ---
KernelBinderClient::KernelBinderClient() 
    : m_hDevice(INVALID_HANDLE_VALUE), m_hServerEvent(nullptr), m_hClientEvent(nullptr), m_SharedMem(nullptr) 
{
    m_hServerEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Global\\KernelBinderServerEvent");
    m_hClientEvent = OpenEventW(SYNCHRONIZE, FALSE, L"Global\\KernelBinderClientEvent");
    m_hDevice = CreateFileW(L"\\\\.\\KernelBinderLink", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (!m_hServerEvent || !m_hClientEvent || m_hDevice == INVALID_HANDLE_VALUE) {
        Cleanup();
        throw std::runtime_error("Libreria: Connessione fallita. Server o Driver non attivi.");
    }

    PVOID pSharedAddress = nullptr;
    DWORD bytesReturned = 0;
    BOOL success = DeviceIoControl(m_hDevice, IOCTL_MAP_SHARED_MEMORY, NULL, 0, &pSharedAddress, sizeof(PVOID), &bytesReturned, NULL);

    if (!success || !pSharedAddress) {
        Cleanup();
        throw std::runtime_error("Libreria: Mappatura memoria condivisa fallita.");
    }
    m_SharedMem = reinterpret_cast<SharedDataBlock*>(pSharedAddress);
}

KernelBinderClient::~KernelBinderClient() {
    Cleanup();
}

bool KernelBinderClient::SendTask(const std::string& dllPath, const uint8_t* data, ULONG length, uint8_t* outResponse) {
    if (!m_SharedMem || length > 1024) return false;

    strcpy_s(m_SharedMem->DllPath, MAX_PATH, dllPath.c_str());
    m_SharedMem->DataLength = length;
    m_SharedMem->TaskCompleted = 0;
    
    if (data && length > 0) {
        memcpy((void*)m_SharedMem->Payload, data, length);
    }

    m_SharedMem->TaskReady = 1;
    SetEvent(m_hServerEvent); // Notifica tramite il kernel

    WaitForSingleObject(m_hClientEvent, INFINITE); // Attesa sincrona deterministica

    if (m_SharedMem->TaskCompleted) {
        if (outResponse) {
            *outResponse = m_SharedMem->Payload[0]; // Legge il byte di risposta
        }
        return true;
    }
    return false;
}

void KernelBinderClient::TerminateServer() {
    if (m_SharedMem && m_hServerEvent) {
        m_SharedMem->ExitSignal = 1;
        SetEvent(m_hServerEvent);
    }
}

void KernelBinderClient::Cleanup() {
    if (m_hDevice != INVALID_HANDLE_VALUE) { CloseHandle(m_hDevice); m_hDevice = INVALID_HANDLE_VALUE; }
    if (m_hServerEvent) { CloseHandle(m_hServerEvent); m_hServerEvent = nullptr; }
    if (m_hClientEvent) { CloseHandle(m_hClientEvent); m_hClientEvent = nullptr; }
    m_SharedMem = nullptr;
}
