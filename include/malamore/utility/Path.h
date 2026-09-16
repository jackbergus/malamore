//
// Created by Giacomo Bergami on 16/09/2026.
//

#ifndef MALAMORE_PATH_H
#define MALAMORE_PATH_H

//
// Created by Giacomo Bergami on 16/09/2026.
//

// #pragma once
#include "malamore/utility/kernel_compatibility.h"

#ifdef __cplusplus
class MalamorePath {
private:
    WCHAR   m_Buffer[MAX_PATH];
    BOOLEAN m_IsHardwareDevice; // Flag hardware: TRUE se è una periferica pure (\??\, \\.\, \DosDevices\)
    static const WCHAR preferred_separator = L'\\';

    // Rileva se la stringa inserita è un descrittore di periferica hardware o un file
    void AnalyzePathContext();
    void normalize();

public:
    MalamorePath();
    MalamorePath(const WCHAR* p);
    MalamorePath(const MalamorePath& other);

    MalamorePath& operator=(const MalamorePath& other);
    MalamorePath& operator=(const WCHAR* p);

    // Append fluido: Si comporta come file system solo se m_IsHardwareDevice è FALSE
    MalamorePath& operator/=(const MalamorePath& other);

    friend MalamorePath operator/(MalamorePath lhs, const MalamorePath& rhs) {
        lhs /= rhs;
        return lhs;
    }

    const WCHAR* wstring() const { return m_Buffer; }

    // Metodi semantici protetti contro le mutilazioni dei nomi dei driver
    MalamorePath filename() const;
    MalamorePath parent_path() const;
    BOOLEAN empty() const;
    BOOLEAN is_hardware_device() const { return m_IsHardwareDevice; }

    // =========================================================================
    // TRASFORMAZIONI FISICHE UNIVERSALI PER LE PERIFERICHE DI WINDOWS NT
    // =========================================================================
    const WCHAR* ToKernelPath(WCHAR* outBuffer, ULONG maxCch) const;
    const WCHAR* ToWin32Path(WCHAR* outBuffer, ULONG maxCch) const;

    const WCHAR* ToPath(WCHAR* outBuffer, ULONG maxCch) const {
#ifdef LINK_KERNEL
        return ToKernelPath(outBuffer, maxCch);
#else
        return ToWin32Path(outBuffer, maxCch);
#endif
    }
};
#endif


#endif //MALAMORE_PATH_H