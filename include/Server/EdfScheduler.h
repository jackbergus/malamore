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

#ifndef MALAMORE_EDFSCHEDULER_H
#define MALAMORE_EDFSCHEDULER_H
#pragma once
#include <malamore.h>
#include "Server/SafeRecovery.h"
#include "Server/DllTaskManager.h"
#include <vector>

struct EdfTaskNode {
    TaskRealtimeMeta Meta;
    ULONG64 NextReleaseTicks;
    ULONG64 AbsoluteDeadlineTicks;
    SafeRecovery RecoveryEngine;
};

class EdfScheduler {
private:
    std::vector<EdfTaskNode> m_TaskQueue;
    double m_TicksPerUs;

public:
    EdfScheduler();
    ~EdfScheduler() = default;

    void RegisterTask(const TaskRealtimeMeta& meta, ULONG64 currentTicks, double ticksPerUs);
    void RunDispatcherTick(BinderParcel* pParcel, void* pIn, void* pOut, DllTaskManager& taskManager);
    bool IsEmpty() const { return m_TaskQueue.empty(); }
};

#endif //MALAMORE_EDFSCHEDULER_H