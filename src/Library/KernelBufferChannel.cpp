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