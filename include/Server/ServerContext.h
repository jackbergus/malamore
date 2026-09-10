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