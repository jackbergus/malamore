#ifndef MALAMORE_THREADGUARD_H
#define MALAMORE_THREADGUARD_H

#include "thread_shared.h"
#include "malamore/utility/kernel_compatibility.h"

// Rilevamento automatico della famiglia di API di Windows (Win32 vs UWP/IoT Core)
#ifndef LINK_KERNEL
#include <winapifamily.h>
#if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_APP)
    #define LINK_UWP_IOT 1
#endif
#endif

#pragma pack(push, 4)
struct ThreadBridgePayload {
    struct UnifiedThreadInterface* Slot;
    PFN_FUTURE_THREAD_ROUTINE RealRoutine;
};

struct UnifiedThreadInterface {
    HANDLE ThreadHandle;
#ifdef LINK_UWP_IOT
    IAsyncAction^ UwpAction; // Handle logico del thread asincrono nel backend UWP/IoT Core
#endif
    struct UnifiedThreadContext Context;
    struct FutureResultParcel FutureResult;
    volatile LONG IsCompleted;              
};
#pragma pack(pop)

#ifdef __cplusplus
class ThreadGuard {
private:
    ThreadBridgePayload             fields; 
    BOOLEAN                        m_IsStarted;

    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;

    // I tre Thunk/Bridge nativi mappati a tempo di compilazione
#ifdef LINK_KERNEL
    static VOID SystemThreadBridge(_In_ PVOID Context);
#elif defined(LINK_UWP_IOT)
    // Bridge per Windows::System::Threading (UWP / IoT Core)
    void UwpThreadBridge(Windows::Foundation::IAsyncAction^ action);
#else
    static DWORD WINAPI UserThreadBridge(LPVOID Context);
#endif

public:
    ThreadGuard(struct UnifiedThreadInterface* threadSlot = NULL,
                PFN_FUTURE_THREAD_ROUTINE  routine = NULL);
    ~ThreadGuard();

    VOID Init(struct UnifiedThreadInterface* threadSlot = NULL,
              PFN_FUTURE_THREAD_ROUTINE  routine = NULL);

    NTSTATUS Start();
    VOID Join();
    VOID Stop();

    const FutureResultParcel* Get() const {
        auto& m_ThreadSlot = this->fields.Slot;
        if (m_ThreadSlot == nullptr) return nullptr;
        if (InterlockedOr(const_cast<volatile LONG*>(&m_ThreadSlot->IsCompleted), 0) == 0) {
            return nullptr;
        }
        return &m_ThreadSlot->FutureResult;
    }

    BOOLEAN IsStarted() const { return m_IsStarted; }
};
#endif

#endif //MALAMORE_THREADGUARD_H
