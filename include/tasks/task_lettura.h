#pragma once

#ifdef LINK_KERNEL
#include <ntddk.h>
#else
#include <windows.h>
#endif

#include <malamore/threads/thread_shared.h>
// #include "malamore/ipc_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Entry point del task di lettura realtime eseguito dal tandem sul core isolato
     */
    VOID TaskLetturaExecute(
        _In_ ULONG CoreId,
        _In_ struct MessageMap* InMap,
        _Inout_ struct MessageMap* OutMap,
        _In_opt_ PVOID CustomContext
    );

#ifdef __cplusplus
}
#endif
