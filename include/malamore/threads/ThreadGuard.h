#ifndef MALAMORE_THREADGUARD_H
#define MALAMORE_THREADGUARD_H

#include "thread_shared.h"
#include "malamore/utility/kernel_compatibility.h"

#pragma pack(push, 4)
struct ThreadBridgePayload {
    struct UnifiedThreadInterface* Slot;
    PFN_FUTURE_THREAD_ROUTINE RealRoutine;

    ThreadBridgePayload(struct UnifiedThreadInterface* Slot, PFN_FUTURE_THREAD_ROUTINE RealRoutine) : Slot{Slot}, RealRoutine{RealRoutine} {}
};

struct UnifiedThreadInterface {
    HANDLE ThreadHandle;
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
