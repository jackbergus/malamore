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