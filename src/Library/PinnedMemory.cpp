//
// Created by giaco on 10/09/2026.
//

#include <malamore.h>
#include <Library/PinnedMemory.h>

PinnedMemory::PinnedMemory(PVOID address, SIZE_T size) : m_Address(address), m_Size(size), m_IsLocked(false) {
    if (m_Address && m_Size > 0) { if (VirtualLock(m_Address, m_Size)) m_IsLocked = true; }
}
PinnedMemory::~PinnedMemory() { if (m_IsLocked && m_Address) VirtualUnlock(m_Address, m_Size); }
