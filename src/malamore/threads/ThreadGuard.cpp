//
// Created by Giacomo Bergami on 16/09/2026.
//

#include "malamore/threads/ThreadGuard.h"

ThreadGuard::ThreadGuard(struct UnifiedThreadInterface* threadSlot, PKSTART_ROUTINE routine)
    : m_ThreadSlot(threadSlot), m_Routine(routine), m_IsStarted(FALSE)
{
    if (m_ThreadSlot != nullptr) {
        m_ThreadSlot->ThreadHandle = NULL;
        RtlZeroMemory(&m_ThreadSlot->Context, sizeof(struct UnifiedThreadContext));
    }
}

ThreadGuard::~ThreadGuard() {
    if (m_IsStarted) {
        Stop();
    }
}

NTSTATUS ThreadGuard::Start() {
    if (m_ThreadSlot == nullptr || m_Routine == nullptr) {
        return STATUS_INVALID_PARAMETER;
    }
    if (m_IsStarted) {
        return STATUS_SUCCESS;
    }

    m_ThreadSlot->Context.KeepRunning = 1;

#ifdef LINK_KERNEL
    // Compilazione nativa Ring 0 (WDK)
    NTSTATUS status = PsCreateSystemThread(
        &m_ThreadSlot->ThreadHandle,
        THREAD_ALL_ACCESS,
        NULL, NULL, NULL,
        m_Routine, // Accetta nativamente PKSTART_ROUTINE (VOID)
        &m_ThreadSlot->Context
    );
#else
    // Compilazione simulata Ring 3 (Win32)
    DWORD threadId = 0;
    HANDLE hThread = CreateThread(
        NULL, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(m_Routine), // Cast di compatibilità sicuro per Win32
        &m_ThreadSlot->Context,
        0, &threadId
    );

    NTSTATUS status = (hThread != NULL) ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
    if (NT_SUCCESS(status)) {
        m_ThreadSlot->ThreadHandle = hThread;
    }
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
    if (m_ThreadSlot == nullptr || !m_IsStarted) return;

    while (InterlockedOr(&m_ThreadSlot->Context.KeepRunning, 0) == 1) {
        YieldProcessor();
    }
}

VOID ThreadGuard::Stop() {
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

