//
// Created by Giacomo Bergami on 17/09/2026.
//

#include <filesystem>
#include <chrono>
#include <execution>
#include <iostream>
#include <fstream>

#include "include/malamore/structures/DataChannelParcel.h"

int main() {
    char buffer[8064];
    std::filesystem::path file{"Malamore_Telemetry_Sink.txt"};
    if (std::filesystem::is_regular_file(file)) {
        DataChannelParcel parcel;
        std::ifstream stream{file, std::ios::binary};
        while (stream.read(reinterpret_cast<char *>(&parcel), sizeof(parcel))) {
            memset(buffer, 0, sizeof(buffer));
            strncpy(buffer, (char *)(parcel.PayloadData), min(sizeof(buffer), parcel.DataLength));
            std::cout << buffer << std::endl;
        }
    } else {
        std::cerr << "File does not exist" << std::endl;
    }
}
