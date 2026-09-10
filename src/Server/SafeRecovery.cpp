//
// Created by giaco on 10/09/2026.
//

#include "Server/SafeRecovery.h"
#include <Library/KernelBufferChannel.h>
#include <iostream>

SafeRecovery::SafeRecovery(const std::string& taskName)
    : m_TaskName(taskName), m_ConsecutiveViolations(0), m_IsQuarantined(false) {}

bool SafeRecovery::EvaluateMetrics(double elapsedUs, ULONG maxWcetUs) {
    if (elapsedUs > maxWcetUs) {
        m_ConsecutiveViolations++;
        std::cerr << "[🚨 WATCHDOG INLINE] Violazione WCET su '" << m_TaskName
                  << "': " << elapsedUs << " us (Max ammesso: " << maxWcetUs << " us)\n";

        // Se un task viola il tempo massimo stimato per più di 3 cicli consecutivi, scatta la quarantena
        if (m_ConsecutiveViolations >= 3) {
            m_IsQuarantined = true;
            std::cerr << "[⚠️ SAFE RECOVERY] Sfratto del task '" << m_TaskName
                      << "' per instabilità hardware. Stato: QUARANTENA.\n";
        }
        return false;
    }
    m_ConsecutiveViolations = 0;
    return true;
}

void SafeRecovery::InjectFallbackPayload(BinderParcel* parcel, void* pOutChannel) {
    std::cerr << "[🛡️ SAFE RECOVERY] Iniezione payload terapeutico di Fallback (Last Known Good / Safe Code 0xFF)\n";

    // 1. Ripristina lo stato logico della transazione nel parcel affinché il client non rimanga appeso
    if (parcel) {
        parcel->PayloadData[0] = 0xFF; // Safe error state byte
        parcel->DataLength = 1;
    }

    // 2. Se c'è un canale I/O a valle, lo sblocchiamo iniettando un byte neutro di emergenza
    if (pOutChannel) {
        KernelBufferChannel output(pOutChannel);
        UCHAR fallbackByte = 0xFF;
        output.Write(&fallbackByte, 1);
    }
}
