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

#include "Server/EdfScheduler.h"
#include <algorithm>

EdfScheduler::EdfScheduler() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    m_TicksPerUs = (double)freq.QuadPart / 1000000.0;
}

void EdfScheduler::RegisterTask(const TaskRealtimeMeta& meta, ULONG64 currentTicks, double ticksPerUs) {
    EdfTaskNode node{ meta, currentTicks, currentTicks + (meta.PeriodUs * ticksPerUs), SafeRecovery(meta.TaskName) };
    m_TaskQueue.push_back(node);
}

void EdfScheduler::RunDispatcherTick(BinderParcel* pParcel, void* pIn, void* pOut, DllTaskManager& taskManager) {
    if (m_TaskQueue.empty()) return;

    // Ordinamento EDF (Earliest Deadline First)
    std::sort(m_TaskQueue.begin(), m_TaskQueue.end(), [](const EdfTaskNode& a, const EdfTaskNode& b) {
        return a.AbsoluteDeadlineTicks < b.AbsoluteDeadlineTicks;
    });

    LARGE_INTEGER qpcNow;
    QueryPerformanceCounter(&qpcNow);

    for (auto& task : m_TaskQueue) {
        // Se il task è in quarantena per violazioni precedenti, lo saltiamo (Inline Escape)
        if (task.RecoveryEngine.IsQuarantined()) {
            // Iniettiamo comunque il payload di emergenza per non interrompere la pipeline a valle
            task.RecoveryEngine.InjectFallbackPayload(pParcel, pOut);

            // Ricalcoliamo il prossimo rilascio per non mandare in loop lo scheduler
            task.NextReleaseTicks = qpcNow.QuadPart + (task.Meta.PeriodUs * m_TicksPerUs);
            task.AbsoluteDeadlineTicks = task.NextReleaseTicks + (task.Meta.PeriodUs * m_TicksPerUs);
            continue;
        }

        if ((ULONG64)qpcNow.QuadPart >= task.NextReleaseTicks) {
            LARGE_INTEGER start, end;
            QueryPerformanceCounter(&start);

            // ESECUZIONE ISOLATA DEL TASK DLL
            taskManager.Execute(pParcel, pIn, pOut);

            QueryPerformanceCounter(&end);

            // VALIDAZIONE INLINE DEL WATCHDOG HARDWARE
            double elapsedUs = (double)(end.QuadPart - start.QuadPart) / m_TicksPerUs;

            if (!task.RecoveryEngine.EvaluateMetrics(elapsedUs, task.Meta.EstimatedWcetUs)) {
                // In caso di sgarro temporale, attiva la contromisura immediata
                task.RecoveryEngine.InjectFallbackPayload(pParcel, pOut);
            }

            // Imposta la prossima scadenza periodica rigida
            task.NextReleaseTicks = start.QuadPart + (task.Meta.PeriodUs * m_TicksPerUs);
            task.AbsoluteDeadlineTicks = task.NextReleaseTicks + (task.Meta.PeriodUs * m_TicksPerUs);
            break;
        }
    }
}
