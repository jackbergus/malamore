#pragma once
#include <windows.h>
#include <string>

// Pre-dichiarazione della struttura dati per evitare inclusioni circolari
struct SharedDataBlock;

// =========================================================================
// CLASSE 1: Wrapper RAII universale per il blocco delle pagine (VirtualLock)
// =========================================================================
class PinnedMemory {
private:
    PVOID m_Address;
    SIZE_T m_Size;
    bool m_IsLocked;

public:
    PinnedMemory(PVOID address, SIZE_T size);
    ~PinnedMemory();

    // Impediamo la copia per evitare sblocchi prematuri
    PinnedMemory(const PinnedMemory&) = delete;
    PinnedMemory& operator=(const PinnedMemory&) = delete;
};

// =========================================================================
// CLASSE 2: Wrapper RAII principale per l'interazione IPC con il Driver
// =========================================================================
class KernelBinderClient {
private:
    HANDLE m_hDevice;
    HANDLE m_hServerEvent;
    HANDLE m_hClientEvent;
    SharedDataBlock* m_SharedMem;

    void Cleanup();

public:
    KernelBinderClient();
    ~KernelBinderClient();

    bool SendTask(const std::string& dllPath, const uint8_t* data, ULONG length, uint8_t* outResponse);
    void TerminateServer();

    KernelBinderClient(const KernelBinderClient&) = delete;
    KernelBinderClient& operator=(const KernelBinderClient&) = delete;
};
