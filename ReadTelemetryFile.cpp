//
// Created by Giacomo Bergami on 17/09/2026.
//

#include <filesystem>
#include <chrono>
#include <execution>
#include <iostream>
#include <fstream>

#include "include/malamore/structures/DataChannelParcel.h"
#include "malamore/utility/telemetry_formatter.h"

// Source - https://codereview.stackexchange.com/a/165162
// Posted by Edward, modified by community. See post 'Timeline' for change history
// Retrieved 2026-09-18, License - CC BY-SA 3.0
std::ostream& hex_dump(std::ostream& os, const void *buffer,
                       std::size_t bufsize, bool showPrintableChars = true)
{
    if (buffer == nullptr) {
        return os;
    }
    auto oldFormat = os.flags();
    auto oldFillChar = os.fill();
    constexpr std::size_t maxline{8};
    // create a place to store text version of string
    char renderString[maxline+1];
    char *rsptr{renderString};
    // convenience cast
    const unsigned char *buf{reinterpret_cast<const unsigned char *>(buffer)};

    for (std::size_t linecount=maxline; bufsize; --bufsize, ++buf) {
        os << std::setw(2) << std::setfill('0') << std::hex
           << static_cast<unsigned>(*buf) << ' ';
        *rsptr++ = std::isprint(*buf) ? *buf : '.';
        if (--linecount == 0) {
            *rsptr++ = '\0';  // terminate string
            if (showPrintableChars) {
                os << " | " << renderString;
            }
            os << '\n';
            rsptr = renderString;
            linecount = min(maxline, bufsize);
        }
    }
    // emit newline if we haven't already
    if (rsptr != renderString) {
        if (showPrintableChars) {
            for (*rsptr++ = '\0'; rsptr != &renderString[maxline+1]; ++rsptr) {
                os << "   ";
            }
            os << " | " << renderString;
        }
        os << '\n';
    }

    os.fill(oldFillChar);
    os.flags(oldFormat);
    return os;
}


int main() {
    char buffer[8064];
    memset(buffer, 0, sizeof(buffer));
    bool firstRecord = true;
    QpcSnapshot telemetry;
    LARGE_INTEGER largeInt;
    std::filesystem::path file{"Malamore_Telemetry_Sink.txt"};
    if (std::filesystem::is_regular_file(file)) {
        DataChannelParcel parcel;
        FILE * fptr;
        fptr = fopen("Malamore_Telemetry_Sink.txt", "rb");
        if(fptr == NULL) {
            printf("Unable to open the file\n");
            return EXIT_FAILURE;
        }
        else {
            // Reading until the end of the file using fread
            size_t bytesRead;
            if (firstRecord) {
                if (((bytesRead = fread(&parcel, sizeof(parcel), 1, fptr)) > 0)) {
                    if ((parcel.Timestamp != 0) || (parcel.SourceNodeId != ULONG_MAX) || (parcel.DataLength != sizeof(QpcSnapshot))) {
                        std::cerr << "Expecting that the first record is a telemetry record for converting the timestamp. But that is not the case" << std::endl;
                        return EXIT_FAILURE;
                    }
                    telemetry = *((QpcSnapshot*)parcel.PayloadData);
                }
                firstRecord = false;
            }
            while ((bytesRead = fread(&parcel, sizeof(parcel), 1, fptr)) > 0) {
                // Process the data in the buffer (example: print to stdout)
                LONGLONG val;
                if (parcel.Timestamp > LLONG_MAX) {
                    val = LLONG_MAX;
                } else {
                    val = parcel.Timestamp;
                }
                ReconstructFromSubsequentTickKernel(&telemetry, val, buffer, sizeof(buffer));
                std::cout << "[" << buffer << "] srcID = " << parcel.SourceNodeId;
                std::cout << " msg (hexdump): " << std::endl << "'''" << std::endl;
                hex_dump(std::cout, parcel.PayloadData, parcel.DataLength, true);
                std::cout << "'''" << std::endl << std::endl;
            }

            fclose(fptr);
        }
        return 0;
    } else {
        std::cerr << "File does not exist" << std::endl;
    }
}
