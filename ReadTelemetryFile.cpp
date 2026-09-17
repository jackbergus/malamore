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
        FILE * fptr;
        fptr = fopen("Malamore_Telemetry_Sink.txt", "rb");
        if(fptr == NULL) {
            printf("Unable to open the file\n");

            exit(1);
        }
        else {
            // Reading until the end of the file using fread
            size_t bytesRead;
            while ((bytesRead = fread(&parcel, sizeof(parcel), 1, fptr)) > 0) {
                // Process the data in the buffer (example: print to stdout)
                std::cout << parcel.PayloadData << std::endl;
            }

            fclose(fptr);
        }
        return 0;
    } else {
        std::cerr << "File does not exist" << std::endl;
    }
}
