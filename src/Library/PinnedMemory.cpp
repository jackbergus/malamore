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

PinnedMemory::PinnedMemory(PVOID address, SIZE_T size) : m_Address(address), m_Size(size), m_IsLocked(false) {
    if (m_Address && m_Size > 0) { if (VirtualLock(m_Address, m_Size)) m_IsLocked = true; }
}
PinnedMemory::~PinnedMemory() { if (m_IsLocked && m_Address) VirtualUnlock(m_Address, m_Size); }
