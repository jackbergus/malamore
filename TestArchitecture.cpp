//
// Created by Giacomo Bergami on 18/09/2026.
//
#include <cmath>
#include <malamore/utility/kernel_compatibility.h>
#include <cstdint>
#include <iostream>
#include <ostream>
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
    status = CorePool_RegisterHwReader(devicePath, 200, 3333, TELEMETRY_WRITER_ID);
    (NT_SUCCESS(status));
    // This should start the hardware service starting to read some messages from the socket
    status = CorePool_StartPoolExecution();
    (NT_SUCCESS(status));

    // Waiting for five seconds before starting simuilating the hardware sending signals
    std::this_thread::sleep_for(std::chrono::seconds(3));
    LARGE_INTEGER currentTime;

    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(60);
    long long iteration_count = 0;

    // Loop until the specified time has passed
    while (std::chrono::steady_clock::now() < end_time) {
        std::this_thread::sleep_for(std::chrono::microseconds(3333));
        currentTime = KeQueryPerformanceCounter(nullptr);
        snprintf(output_buffer, sizeof(output_buffer), "Msg #%llu @%llu\n", iteration_count, currentTime.QuadPart);
        hardwarePipeServer.Write(output_buffer, strlen(output_buffer), &result);
        iteration_count++;
    }

    std::cout << "Iterations: " << iteration_count << std::endl;
    CorePool_ShutdownAndClear();

}