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

#ifndef MALAMORE_DLLTASKMANAGER_H
#define MALAMORE_DLLTASKMANAGER_H
// #pragma once
#include <windows.h>
#include <string>

typedef void (*GenericTaskFunc)(void* parcel, void* inputChannel, void* outputChannel);

class DllTaskManager {
private:
    std::string m_DllPath;
    HMODULE m_hDll;
    GenericTaskFunc m_TaskFunction;

public:
    DllTaskManager(const std::string& dllPath);
    ~DllTaskManager();

    bool LoadTask();
    bool Execute(void* pParcel, void* pInChannel, void* pOutChannel);
    void Unload();
};

#endif //MALAMORE_DLLTASKMANAGER_H