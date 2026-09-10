//
// Created by giaco on 10/09/2026.
//

#ifndef MALAMORE_SERVERCONTEXT_H
#define MALAMORE_SERVERCONTEXT_H

#include <malamore.h>
#include <string>

class ServerContext {
private:
    std::wstring m_NodeName;
    HANDLE m_hDevice;
    HANDLE m_hLaunchEvent;
    HANDLE m_hCompleteEvent;
    BinderParcel* m_pParcel;
    void* m_pInChannel;
    void* m_pOutChannel;
    HANDLE m_hInMapping;
    HANDLE m_hOutMapping;

public:
    ServerContext(const std::string& nodeName, HANDLE hSharedParcel, HANDLE hLaunchEvent);
    ~ServerContext();

    bool OpenChannels();
    void SignalCompletion();
    bool WaitLaunch(DWORD timeoutMs);

    BinderParcel* GetParcel() const { return m_pParcel; }
    void* GetInputChannel() const { return m_pInChannel; }
    void* GetOutputChannel() const { return m_pOutChannel; }
};

#endif //MALAMORE_SERVERCONTEXT_H