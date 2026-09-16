//
// Created by Giacomo Bergami on 16/09/2026.
//

#include "malamore/threads/ThreadGuard.h"



ThreadGuard::ThreadGuard(struct UnifiedThreadInterface* threadSlot, PFN_FUTURE_THREAD_ROUTINE  routine)
    : fields(nullptr, nullptr), m_IsStarted(FALSE)
{
    this->fields.Slot = threadSlot;
    this->fields.RealRoutine = routine;
    if (this->fields.Slot != nullptr) {
        this->fields.Slot->ThreadHandle = NULL;
        this->fields.Slot->IsCompleted = 0;
        RtlZeroMemory(&this->fields.Slot->Context, sizeof(struct UnifiedThreadContext));
        RtlZeroMemory(&this->fields.Slot->FutureResult, sizeof(struct FutureResultParcel));
    }
}

ThreadGuard::~ThreadGuard() {
    if (m_IsStarted) {
        Stop();
    }
}

// =========================================================================
// COPIA DI SICUREZZA DIRETTA TRA STRUTTURE COMPATTE DEL THUNK
// =========================================================================
#ifdef LINK_KERNEL
VOID ThreadGuard::SystemThreadBridge(_In_ PVOID Context) {
    ThreadBridgePayload* payload = reinterpret_cast<ThreadBridgePayload*>(Context);
    if (payload && payload->Slot && payload->RealRoutine) {
        struct FutureResultParcel staticParcel;
        RtlZeroMemory(&staticParcel, sizeof(struct FutureResultParcel));

        // 1. Invocazione della routine che popola il buffer del parcel locale allo stack del thread
        payload->RealRoutine(&payload->Slot->Context, &staticParcel);

        // 2. Controllo dei limiti fisici e copia protetta nel bersaglio dello slot statico
        if (staticParcel.DataLength > 0) {
            ULONG safeLength = (staticParcel.DataLength < FUTURE_BUFFER_SIZE) ? staticParcel.DataLength : FUTURE_BUFFER_SIZE;

            RtlCopyMemory(payload->Slot->FutureResult.DataBuffer, staticParcel.DataBuffer, safeLength);
            InterlockedExchange(&payload->Slot->FutureResult.DataLength, static_cast<LONG>(safeLength));
        }

        KeMemoryBarrier(); // Sincronizzazione atomica hardware a livello di core CPU
        InterlockedExchange(&payload->Slot->IsCompleted, 1); // Il Future è ora leggibile da .Get()
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}
#else
DWORD WINAPI ThreadGuard::UserThreadBridge(LPVOID Context) {
    ThreadBridgePayload* payload = reinterpret_cast<ThreadBridgePayload*>(Context);
    if (payload && payload->Slot && payload->RealRoutine) {
        struct FutureResultParcel staticParcel;
        RtlZeroMemory(&staticParcel, sizeof(struct FutureResultParcel));

        payload->RealRoutine(&payload->Slot->Context, &staticParcel);

        if (staticParcel.DataLength > 0) {
            ULONG safeLength = (staticParcel.DataLength < FUTURE_BUFFER_SIZE) ? staticParcel.DataLength : FUTURE_BUFFER_SIZE;

            RtlCopyMemory(payload->Slot->FutureResult.DataBuffer, staticParcel.DataBuffer, safeLength);
            InterlockedExchange(&payload->Slot->FutureResult.DataLength, static_cast<LONG>(safeLength));
        }

        MemoryBarrier();
        InterlockedExchange(&payload->Slot->Context.KeepRunning, 0);
        InterlockedExchange(&payload->Slot->IsCompleted, 1);
    }
    return 0;
}
#endif

NTSTATUS ThreadGuard::Start() {
    auto& m_ThreadSlot = this->fields.Slot;
    auto& m_Routine = this->fields.RealRoutine;
    if (m_ThreadSlot == nullptr || m_Routine == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    if (m_IsStarted) {
        return STATUS_SUCCESS;
    }

    m_ThreadSlot->Context.KeepRunning = 1;
    m_ThreadSlot->IsCompleted = 0;
    RtlZeroMemory(&m_ThreadSlot->FutureResult, sizeof(struct FutureResultParcel));

#ifdef LINK_KERNEL
    NTSTATUS status = PsCreateSystemThread(&m_ThreadSlot->ThreadHandle, THREAD_ALL_ACCESS, NULL, NULL, NULL, SystemThreadBridge, &bridgePayload);
#else
    DWORD threadId = 0;
    HANDLE hThread = CreateThread(NULL, 0, UserThreadBridge, &this->fields, 0, &threadId);
    NTSTATUS status = (hThread != NULL) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
    if (NT_SUCCESS(status)) m_ThreadSlot->ThreadHandle = hThread;
#endif

    if (NT_SUCCESS(status)) {
        m_IsStarted = TRUE;
    } else {
        m_ThreadSlot->Context.KeepRunning = 0;
        m_ThreadSlot->ThreadHandle = NULL;
    }

    return status;
}

VOID ThreadGuard::Join() {
    auto& m_ThreadSlot = this->fields.Slot;
    auto& m_Routine = this->fields.RealRoutine;
    if (m_ThreadSlot == nullptr || !m_IsStarted) return;

    while (InterlockedOr(&m_ThreadSlot->Context.KeepRunning, 0) == 1) {
        YieldProcessor();
    }
}

VOID ThreadGuard::Stop() {
    auto& m_ThreadSlot = this->fields.Slot;
    auto& m_Routine = this->fields.RealRoutine;
    if (m_ThreadSlot == nullptr || !m_IsStarted || m_ThreadSlot->ThreadHandle == NULL) {
        return;
    }

    InterlockedExchange(&m_ThreadSlot->Context.KeepRunning, 0);

#ifdef LINK_KERNEL
    PVOID threadObject = nullptr;
    NTSTATUS status = ObReferenceObjectByHandle(
        m_ThreadSlot->ThreadHandle,
        THREAD_ALL_ACCESS,
        *PsThreadType,
        KernelMode,
        &threadObject,
        NULL
    );

    if (NT_SUCCESS(status)) {
        KeWaitForSingleObject(threadObject, Executive, KernelMode, FALSE, NULL);
        ObDereferenceObject(threadObject);
    }
    ZwClose(m_ThreadSlot->ThreadHandle);
#else
    WaitForSingleObject(m_ThreadSlot->ThreadHandle, INFINITE);
    CloseHandle(m_ThreadSlot->ThreadHandle);
#endif

    m_ThreadSlot->ThreadHandle = NULL;
    m_IsStarted = FALSE;
}

