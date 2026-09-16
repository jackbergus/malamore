//
// Created by Giacomo Bergami on 16/09/2026.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <malamore/utility/kernel_compatibility.h>
#include <cstdint>

#include "malamore/structures/InterProcessChannelData.h"

TEST_CASE("InterProcessChannelData") {
    InterProcessChannelData buffer;
    RealTimeIPC_Init(&buffer);
    REQUIRE(RealTimeIPC_IsEmpty(&buffer));
    static const UCHAR* element = reinterpret_cast<const UCHAR *>("hello, world");
    static size_t strl = strlen(reinterpret_cast<const char *>(element));
    REQUIRE(RealTimeIPC_Push(&buffer, 0, 1, element, strl));
    REQUIRE_FALSE(RealTimeIPC_IsEmpty(&buffer));
    auto obj = RealTimeIPC_Top(&buffer);
    REQUIRE_FALSE(obj == nullptr);
    REQUIRE(obj->DataLength == strl);
    REQUIRE(obj->SourceNodeId == 1);
    REQUIRE(obj->Timestamp == 0);
    REQUIRE(strcmp((const char*)obj->PayloadData, (const char*)element) == 0);
    REQUIRE(RealTimeIPC_Pop(&buffer));
    REQUIRE(RealTimeIPC_IsEmpty(&buffer));
}