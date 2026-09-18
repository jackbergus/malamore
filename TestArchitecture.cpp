//
// Created by Giacomo Bergami on 18/09/2026.
//
#include <malamore/utility/kernel_compatibility.h>
#include <cstdint>
#include <thread>

#include "malamore/threads/ThreadGuard.h"
#include "malamore/threads/thread_pool.h"
#include "malamore/winapi_client/Win32NamedPipeGuard.h"

// La funzione callback che verrà sparata in background dal Thread Pool non appena il client si connette
void OnPipeClientConnected2(PVOID Context, NTSTATUS CompletionStatus) {
    // UNREFERENCED_PARAMETER(Context);
    if (NT_SUCCESS(CompletionStatus)) {
        printf("[CALLBACK NOTIFY ✅] Il client del framework si è connesso asincronamente alla risorsa!\n");
    }
    if (Context) {
        *((ULONG*)Context) = 1;
    }
}

void main() {
    char output_buffer[1024];
    ULONG result = 0;
    Win32NamedPipeGuard hardwarePipeServer(2);
    (NT_SUCCESS(hardwarePipeServer.Create(L"HWDriverSimulator", StreamFlags::Read | StreamFlags::Write, sizeof(output_buffer))));
    (NT_SUCCESS(hardwarePipeServer.ListenAsync(OnPipeClientConnected2, &result)));


    auto devicePath = hardwarePipeServer.GetPath().wstring();
    auto status = CorePool_InitializeRegistry(75); // The main task is actually activated here
    (NT_SUCCESS(status));
    status = CorePool_RegisterHwReader(devicePath,3000, 3333, TELEMETRY_WRITER_ID);
    (NT_SUCCESS(status));
    // This should start the hardware service starting to read some messages from the socket
    status = CorePool_StartPoolExecution();
    (NT_SUCCESS(status));

    // Waiting for five seconds before starting simuilating the hardware sending signals
    std::this_thread::sleep_for(std::chrono::seconds(3));

    for (uint64_t t = 0; t < 100; t++) {
        // Simulating the driver sending messages constantly at 3 millisecond rate
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        snprintf(output_buffer, sizeof(output_buffer), "Msg #%llu\n", t);
        hardwarePipeServer.Write(output_buffer, strlen(output_buffer), &result);
        // Expecting that I am actually writing something
        (strlen(output_buffer) == result);
    }

}