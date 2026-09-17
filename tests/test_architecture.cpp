//
// Created by giaco on 16/09/2026.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <malamore/utility/kernel_compatibility.h>
#include <cstdint>
#include <thread>

#include "malamore/threads/ThreadGuard.h"
#include "malamore/threads/thread_pool.h"
#include "malamore/utility/StdStream.h"
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

TEST_CASE("BASIC_ARCHITECTURE") {
    char output_buffer[1024];
    ULONG result = 0;


    Win32NamedPipeGuard hardwarePipeServer(2);
    REQUIRE(NT_SUCCESS(hardwarePipeServer.Create(L"MalamoreDuplexSocket", StreamFlags::Read | StreamFlags::Write, sizeof(output_buffer))));
    REQUIRE(NT_SUCCESS(hardwarePipeServer.ListenAsync(OnPipeClientConnected2, &result)));
    auto devicePath = hardwarePipeServer.GetPath().wstring();

    auto status = CorePool_InitializeRegistry(75);
    REQUIRE(NT_SUCCESS(status));
    status = CorePool_RegisterHwReader(devicePath,3000, 3333, TELEMETRY_WRITER_ID);
    REQUIRE(NT_SUCCESS(status));
    status = CorePool_StartPoolExecution();
    REQUIRE(NT_SUCCESS(status));
    std::this_thread::sleep_for(std::chrono::minutes(10));
}