//
// Created by giaco on 10/09/2026.
//

#include <malamore.h>
#include <Library/PinnedMemory.h>
#include <Library/KernelBufferChannel.h>

KernelBufferChannel::KernelBufferChannel(void* addr) : m_ChannelDataAddress(addr), m_AsyncCallback(nullptr) {}
int KernelBufferChannel::Write(const void* buf, size_t count) {
    auto* data = reinterpret_cast<InterProcessChannelData*>(m_ChannelDataAddress);
    if (!data || count > 256) return -1;
    PinnedMemory lock(data, sizeof(InterProcessChannelData));
    if (data->MessageCount >= 10) return 0;
    LONG currentWrite = data->WriteIndex;
    UCHAR* target = &data->Buffer[(currentWrite * 256) % CHANNEL_BUFFER_SIZE];
    std::memcpy(target, buf, count);
    InterlockedExchange(&data->WriteIndex, (currentWrite + 1) % 10);
    InterlockedIncrement(&data->MessageCount);
    return static_cast<int>(count);
}
int KernelBufferChannel::Read(void* outBuf, size_t maxCount) {
    auto* data = reinterpret_cast<InterProcessChannelData*>(m_ChannelDataAddress);
    if (!data || data->MessageCount == 0) return 0;
    PinnedMemory lock(data, sizeof(InterProcessChannelData));
    LONG currentRead = data->ReadIndex;
    UCHAR* source = &data->Buffer[(currentRead * 256) % CHANNEL_BUFFER_SIZE];
    size_t toCopy = (maxCount < 256) ? maxCount : 256;
    std::memcpy(outBuf, source, toCopy);
    InterlockedExchange(&data->ReadIndex, (currentRead + 1) % 10);
    InterlockedDecrement(&data->MessageCount);
    return static_cast<int>(toCopy);
}
void KernelBufferChannel::ReadAsync(AsyncCallback cb) { m_AsyncCallback = cb; }
void KernelBufferChannel::DispatchAsync() {
    if (!m_AsyncCallback) return;
    UCHAR temp[256] = {0};
    int readBytes = Read(temp, 256);
    if (readBytes > 0) m_AsyncCallback(temp, readBytes);
}