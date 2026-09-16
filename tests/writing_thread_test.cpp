//
// Created by Giacomo Bergami on 16/09/2026.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <malamore/utility/kernel_compatibility.h>
#include <cstdint>
#include <thread>

#include "malamore/threads/ThreadGuard.h"
#include "malamore/utility/StdStream.h"
#include "malamore/winapi_client/Win32NamedPipeGuard.h"


VOID bogus_task(struct UnifiedThreadContext* Context, struct FutureResultParcel* OutResult) {
    if (OutResult) {
        *((uint32_t*)OutResult->DataBuffer) = (uint32_t)123456;
        OutResult->DataLength = sizeof(uint32_t);
    }
}

TEST_CASE( "Testing Threads with future support-", "[simple bogus task]" ) {
    UnifiedThreadInterface if_;
    ThreadGuard thread1(&if_, bogus_task);
    thread1.Start();
    thread1.Join();
    REQUIRE(true); // That is, I am required to immediately terminate as soon as the thread stops.
    auto ptr = thread1.Get();
    // Returning the data that the thread has produced.
    REQUIRE(ptr != nullptr);
    REQUIRE(ptr->DataLength == sizeof(uint32_t));
    REQUIRE(*((uint32_t*)ptr->DataBuffer) == (uint32_t)123456);
}

// La funzione callback che verrà sparata in background dal Thread Pool non appena il client si connette
void OnPipeClientConnected(PVOID Context, NTSTATUS CompletionStatus) {
    // UNREFERENCED_PARAMETER(Context);
    if (NT_SUCCESS(CompletionStatus)) {
        printf("[CALLBACK NOTIFY ✅] Il client del framework si è connesso asincronamente alla risorsa!\n");
    }
    if (Context) {
        *((ULONG*)Context) = 1;
    }
}

TEST_CASE("Testing so the reading and writing utility via named pipes [named pipes simulation driver] ") {
    char output_buffer[1024];
    ULONG result = 0;

    // --- ACCANCIO LATO SERVER ---+
    // Il server si registra semplicemente dichiarando il suo tag nominale logico
    Win32NamedPipeGuard hardwarePipeServer(2);
    REQUIRE(NT_SUCCESS(hardwarePipeServer.Create(L"MalamoreDuplexSocket", StreamFlags::Read | StreamFlags::Write, sizeof(output_buffer))));
    REQUIRE(NT_SUCCESS(hardwarePipeServer.ListenAsync(OnPipeClientConnected, &result)));
    REQUIRE_FALSE(result); // At this stage, I have no connection, and therefore I should not be able to re-read it.

    // --- AGGANCIO LATO CLIENT COMPATIBILE ---
    MalamoreIOStream clientStream;
    // MalamoreIStream clientInputStream;

    // Estrae l'oggetto MalamorePath compilato internamente dal server
    // e lo passa direttamente alle Open dei canali, senza tirare ad indovinare la stringa!
    const MalamorePath& percorsoVerificato = hardwarePipeServer.GetPath();
    // I client aprono in automatico sulle coordinate esatte pre-configurate
    REQUIRE(NT_SUCCESS(clientStream.Open(percorsoVerificato, StreamFlags::Read | StreamFlags::Write)));

    // Se non ho ancora scritto niente all'interno della pipe, rimango comunque in attesa bloccante
    ULONG actuallyRead = 0;
    NTSTATUS status = clientStream.ReadWithTimeout(output_buffer, sizeof(output_buffer), 3000000, &actuallyRead);
    REQUIRE(status == STATUS_TIMEOUT);

    // Altrimenti, dovrei essere in grado di scrivere qualcosa sulla pipe, e poi leggerla qui di nuovo
    clientStream << "hello, world!" << sflush;
    // REQUIRE(result); // At the first write, I should be able to connect and read

    RtlZeroMemory(output_buffer, sizeof(output_buffer));
    REQUIRE(NT_SUCCESS(hardwarePipeServer.Read(output_buffer, sizeof(output_buffer), &actuallyRead)));
    REQUIRE(strcmp(output_buffer, "hello, world!") == 0);
    REQUIRE(strlen("hello, world!") == actuallyRead);

    REQUIRE(NT_SUCCESS(hardwarePipeServer.Write("This is some new text, forsooth!\n", strlen("This is some new text, forsooth!\n"), &actuallyRead)));
    REQUIRE(strlen("This is some new text, forsooth!\n") == actuallyRead);

    REQUIRE(clientStream.GetLine(output_buffer, sizeof(output_buffer)) == actuallyRead-1);
    REQUIRE(strcmp(output_buffer, "This is some new text, forsooth!") == 0);

    clientStream.Close();
    // clientInputStream.Close();
    hardwarePipeServer.Close();
}