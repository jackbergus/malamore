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