//
// Created by giaco on 10/09/2026.
//

#ifndef MALAMORE_SAFERECOVERY_H
#define MALAMORE_SAFERECOVERY_H
// #pragma once
#include <malamore.h>
#include <string>

class SafeRecovery {
private:
    std::string m_TaskName;
    ULONG m_ConsecutiveViolations;
    bool m_IsQuarantined;

public:
    SafeRecovery(const std::string& taskName);
    ~SafeRecovery() = default;

    bool EvaluateMetrics(double elapsedUs, ULONG maxWcetUs);
    void InjectFallbackPayload(BinderParcel* parcel, void* pOutChannel);
    bool IsQuarantined() const { return m_IsQuarantined; }
    void Reset() { m_IsQuarantined = false; m_ConsecutiveViolations = 0; }
};

#endif //MALAMORE_SAFERECOVERY_H