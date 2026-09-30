//
// Created by Giacomo Bergami on 15/09/2026.
//

#ifndef MALAMORE_HARDWARE_READER_H
#define MALAMORE_HARDWARE_READER_H

// #include <windows.h>
#include <malamore/structures/MessageMap.h>

// #pragma once
// #include "malamore/ipc_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

    VOID HardwareReaderThreadRoutine(struct UnifiedThreadContext* Context, struct FutureResultParcel* OutResult);
    VOID HardwareWriterThreadRoutine(struct UnifiedThreadContext* Context, struct FutureResultParcel* OutResult);

#ifdef __cplusplus
}
#endif


#endif //MALAMORE_HARDWARE_READER_H