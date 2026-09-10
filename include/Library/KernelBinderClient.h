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