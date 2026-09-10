//
// Created by giaco on 10/09/2026.
//

#ifndef MALAMORE_ASYNCCALLBACK_H
#define MALAMORE_ASYNCCALLBACK_H

#include <windows.h>
#include <string>
#include <functional>

using AsyncCallback = std::function<void(const void* data, size_t size)>;
class KernelBufferChannel {
private:
    void* m_ChannelDataAddress;
    AsyncCallback m_AsyncCallback;
public:
    KernelBufferChannel(void* sharedMemoryAddress);
    int Write(const void* buffer, size_t count);
    int Read(void* outBuffer, size_t maxCount);
    void ReadAsync(AsyncCallback callback);
    void DispatchAsync();
};


#endif //MALAMORE_ASYNCCALLBACK_H