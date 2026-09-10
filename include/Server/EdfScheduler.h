//
// Created by giaco on 10/09/2026.
//

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