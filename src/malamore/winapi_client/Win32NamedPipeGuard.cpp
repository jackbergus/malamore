//
// Created by Giacomo Bergami on 16/09/2026.
//

#ifndef LINK_KERNEL
#include "malamore/winapi_client//Win32NamedPipeGuard.h"
#include <strsafe.h>

// #define STATUS_SUCCESS                ((LONG)0x00000000L)
// #define STATUS_UNSUCCESSFUL           ((LONG)0xC0000001L)
// #define STATUS_PENDING                ((LONG)0x00000103L)
// #define STATUS_INVALID_DEVICE_STATE   ((LONG)0xC0000184L)
// #define STATUS_INSUFFICIENT_RESOURCES ((LONG)0xC000009AL)

Win32NamedPipeGuard::Win32NamedPipeGuard(ULONG nmaxconn)
    : m_PipeHandle(INVALID_HANDLE_VALUE), m_IsConnected(FALSE), m_WaitHandle(NULL),
      m_UserCallback(nullptr), m_UserContext(nullptr), nmaxconn(nmaxconn)
{
    RtlZeroMemory(m_FullPipePath, sizeof(m_FullPipePath));
    RtlZeroMemory(&m_Overlapped, sizeof(OVERLAPPED));
}

Win32NamedPipeGuard::~Win32NamedPipeGuard() {
    Close();
}

NTSTATUS Win32NamedPipeGuard::Create(const WCHAR* pipeName, ULONG streamFlags, ULONG maxBufferSize) {
    if (m_PipeHandle != INVALID_HANDLE_VALUE) return STATUS_SUCCESS;
    if (pipeName == nullptr || streamFlags == 0) return STATUS_UNSUCCESSFUL;

    StringCchPrintfW(m_FullPipePath, MAX_PATH, L"\\\\.\\pipe\\%s", pipeName);
    m_CachedPath = m_FullPipePath;

    DWORD openMode = FILE_FLAG_OVERLAPPED; // <--- FONDAMENTALE: Abilita il supporto hardware all'asincronia
    if ((streamFlags & StreamFlags::Read) && (streamFlags & StreamFlags::Write)) openMode |= PIPE_ACCESS_DUPLEX;
    else if (streamFlags & StreamFlags::Write) openMode |= PIPE_ACCESS_OUTBOUND;
    else if (streamFlags & StreamFlags::Read) openMode |= PIPE_ACCESS_INBOUND;

    m_PipeHandle = CreateNamedPipeW(
        m_FullPipePath,
        openMode,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        nmaxconn, maxBufferSize, maxBufferSize, 0, NULL
    );

    if (m_PipeHandle == INVALID_HANDLE_VALUE) {
        m_CachedPath = L"";
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    // Crea l'evento Win32 da associare alla struttura di notifica asincrona
    m_Overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL); // Manual-reset, inizialmente non segnalato
    return STATUS_SUCCESS;
}

/**
 * AVVIO DELL'ASCOLTO ASINCRONO IN BACKGROUND (ZERO-BLOCKING)
 */
NTSTATUS Win32NamedPipeGuard::ListenAsync(PFN_PIPE_LISTEN_CALLBACK callback, PVOID context) {
    if (m_PipeHandle == INVALID_HANDLE_VALUE || m_Overlapped.hEvent == NULL) return STATUS_INVALID_DEVICE_STATE;
    if (m_IsConnected) return STATUS_SUCCESS;
    if (callback == nullptr) return STATUS_UNSUCCESSFUL;

    m_UserCallback = callback;
    m_UserContext = context;

    // Reset preventivo dell'evento asincrono
    ResetEvent(m_Overlapped.hEvent);

    // Chiamata asincrona non bloccante: ritorna istantaneamente
    BOOL success = ConnectNamedPipe(m_PipeHandle, &m_Overlapped);

    if (success) {
        // Caso rarissimo: il client era già connesso nell'esatto istante di clock della chiamata
        m_IsConnected = TRUE;
        m_UserCallback(m_UserContext, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    DWORD lastError = GetLastError();

    if (lastError == ERROR_PIPE_CONNECTED) {
        // Il client si è connesso prima che la funzione andasse in sosta
        m_IsConnected = TRUE;
        m_UserCallback(m_UserContext, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    if (lastError == ERROR_IO_PENDING) {
        // L'operazione è presa in carico dal kernel ed è in attesa in background.
        // Registriamo l'evento nel Thread Pool di Windows per svegliare la callback asincronamente.
        BOOL regSuccess = RegisterWaitForSingleObject(
            &m_WaitHandle,
            m_Overlapped.hEvent,              // Handle dell'evento da monitorare
            PipeWaitCallbackBridge,           // La funzione thunk da invocare allo scatto
            this,                             // Passiamo il puntatore all'istanza della classe
            INFINITE,                         // Nessun timeout di sfoltimento
            WT_EXECUTEINWAITTHREAD | WT_EXECUTEONLYONCE // Esegue una sola volta all'aggancio del client
        );

        if (!regSuccess) {
            m_WaitHandle = NULL;
            return STATUS_UNSUCCESSFUL;
        }

        return STATUS_PENDING; // Ritorna immediatamente 0x00000103, liberando il thread!
    }

    return STATUS_UNSUCCESSFUL;
}

/**
 * BRIDGE STATICO DEL THREAD POOL DI WINDOWS
 */
void CALLBACK Win32NamedPipeGuard::PipeWaitCallbackBridge(PVOID lpParameter, BOOLEAN TimerOrWaitSignaled) {
    UNREFERENCED_PARAMETER(TimerOrWaitSignaled);
    Win32NamedPipeGuard* pThis = reinterpret_cast<Win32NamedPipeGuard*>(lpParameter);

    if (pThis && pThis->m_PipeHandle != INVALID_HANDLE_VALUE) {
        DWORD bytesTransferred = 0;

        // Verifica finale dell'esito dell'operazione overlapped asincrona
        BOOL success = GetOverlappedResult(pThis->m_PipeHandle, &pThis->m_Overlapped, &bytesTransferred, FALSE);

        if (success || GetLastError() == ERROR_PIPE_CONNECTED) {
            pThis->m_IsConnected = TRUE;
            if (pThis->m_UserCallback) pThis->m_UserCallback(pThis->m_UserContext, STATUS_SUCCESS);
        } else {
            pThis->m_IsConnected = FALSE;
            if (pThis->m_UserCallback) pThis->m_UserCallback(pThis->m_UserContext, STATUS_UNSUCCESSFUL);
        }
    }
}

NTSTATUS Win32NamedPipeGuard::Write(const VOID* buffer, ULONG bytesToWrite, ULONG* bytesWritten) {
    if (m_PipeHandle == INVALID_HANDLE_VALUE || !m_IsConnected || buffer == nullptr || bytesWritten == nullptr) return STATUS_INVALID_DEVICE_STATE;
    DWORD dwWritten = 0;
    // Usiamo l'operazione sincrona passando l'istanza per semplicità sul canale stabilito
    BOOL success = WriteFile(m_PipeHandle, buffer, bytesToWrite, &dwWritten, &m_Overlapped);
    if (!success && GetLastError() == ERROR_IO_PENDING) {
        success = GetOverlappedResult(m_PipeHandle, &m_Overlapped, &dwWritten, TRUE); // Forza l'attesa inline della scrittura
    }
    *bytesWritten = static_cast<ULONG>(dwWritten);
    return success ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

NTSTATUS Win32NamedPipeGuard::Read(VOID* buffer, ULONG bytesToRead, ULONG* bytesRead) {
    if (m_PipeHandle == INVALID_HANDLE_VALUE || !m_IsConnected || buffer == nullptr || bytesRead == nullptr) return STATUS_INVALID_DEVICE_STATE;
    DWORD dwRead = 0;
    BOOL success = ReadFile(m_PipeHandle, buffer, bytesToRead, &dwRead, &m_Overlapped);
    if (!success && GetLastError() == ERROR_IO_PENDING) {
        success = GetOverlappedResult(m_PipeHandle, &m_Overlapped, &dwRead, TRUE);
    }
    *bytesRead = static_cast<ULONG>(dwRead);
    return success ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

VOID Win32NamedPipeGuard::Close() {
    // Disinstalla la registrazione dal pool per evitare chiamate orfane a memoria liberata
    if (m_WaitHandle != NULL) {
        UnregisterWait(m_WaitHandle);
        m_WaitHandle = NULL;
    }
    if (m_PipeHandle != INVALID_HANDLE_VALUE) {
        if (m_IsConnected) {
            DisconnectNamedPipe(m_PipeHandle);
            m_IsConnected = FALSE;
        }
        CloseHandle(m_PipeHandle);
        m_PipeHandle = INVALID_HANDLE_VALUE;
        m_CachedPath = L"";
    }
    if (m_Overlapped.hEvent != NULL) {
        CloseHandle(m_Overlapped.hEvent);
        m_Overlapped.hEvent = NULL;
    }
}
#endif
