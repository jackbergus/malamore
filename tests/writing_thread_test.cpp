//
// Created by Giacomo Bergami on 16/09/2026.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <malamore/utility/kernel_compatibility.h>
#include <cstdint>
#include <thread>

#include "malamore/threads/ThreadGuard.h"

uint32_t factorial( uint32_t number ) {
    return number <= 1 ? number : factorial(number-1) * number;
}

VOID bogus_task(PVOID Context, struct FutureResultParcel* OutResult) {
    if (OutResult) {
        *((uint32_t*)OutResult->DataBuffer) = (uint32_t)123456;
        OutResult->DataLength = sizeof(uint32_t);
    }
}

TEST_CASE( "Factorials are computed", "[factorial]" ) {
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