//
// Created by giaco on 10/09/2026.
//

#ifndef MALAMORE_PINNEDMEMORY_H
#define MALAMORE_PINNEDMEMORY_H

#include <windows.h>
#include <string>
#include <functional>

class PinnedMemory {
private:
    PVOID m_Address;
    SIZE_T m_Size;
    bool m_IsLocked;
public:
    PinnedMemory(PVOID address, SIZE_T size);
    ~PinnedMemory();
    PinnedMemory(const PinnedMemory&) = delete;
    PinnedMemory& operator=(const PinnedMemory&) = delete;
};

#endif //MALAMORE_PINNEDMEMORY_H