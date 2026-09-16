//
// Created by Giacomo Bergami on 16/09/2026.
//

#pragma once
#ifndef LINK_KERNEL // Esclusivo per Ring 3 (User Mode)
#include <windows.h>
#include "malamore/utility/streambuf/StreamFlags.h"
#include "malamore/utility//Path.h"

// Firma della callback asincrona: riceve un puntatore al contesto utente e lo status finale
typedef void (*PFN_PIPE_LISTEN_CALLBACK)(PVOID CallbackContext, NTSTATUS CompletionStatus);

class Win32NamedPipeGuard {
private:
    HANDLE       m_PipeHandle;
    BOOLEAN      m_IsConnected;
    WCHAR        m_FullPipePath[MAX_PATH];
    MalamorePath m_CachedPath;
    ULONG nmaxconn;

    // Strutture native Win32 per l'I/O sovrapposto asincrono
    OVERLAPPED   m_Overlapped;
    HANDLE       m_WaitHandle; // Handle del Thread Pool di Windows

    // Memorie di sosta per la callback
    PFN_PIPE_LISTEN_CALLBACK m_UserCallback;
    PVOID                    m_UserContext;

    Win32NamedPipeGuard(const Win32NamedPipeGuard&) = delete;
    Win32NamedPipeGuard& operator=(const Win32NamedPipeGuard&) = delete;

    // Funzione ponte statica (Thunk) invocata dal Thread Pool di Windows
    static void CALLBACK PipeWaitCallbackBridge(PVOID lpParameter, BOOLEAN TimerOrWaitSignaled);

public:
    Win32NamedPipeGuard(ULONG nmaxconn);
    ~Win32NamedPipeGuard();

    // Nota: Aggiunto obbligatoriamente FILE_FLAG_OVERLAPPED nella Create interna per l'asincronia
    NTSTATUS Create(const WCHAR* pipeName, ULONG streamFlags, ULONG maxBufferSize);

    // NUOVO METODO: Avvia l'ascolto in background e ritorna immediatamente STATUS_PENDING
    NTSTATUS ListenAsync(PFN_PIPE_LISTEN_CALLBACK callback, PVOID context);

    NTSTATUS Write(const VOID* buffer, ULONG bytesToWrite, ULONG* bytesWritten);
    NTSTATUS Read(VOID* buffer, ULONG bytesToRead, ULONG* bytesRead);
    VOID Close();

    const MalamorePath& GetPath() const { return m_CachedPath; }
    HANDLE GetHandle() const { return m_PipeHandle; }
    BOOLEAN IsConnected() const { return m_IsConnected; }
};
#endif
