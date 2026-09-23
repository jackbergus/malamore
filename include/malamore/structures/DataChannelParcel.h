//
// Created by Giacomo Bergami on 17/09/2026.
//

#ifndef MALAMORE_DATACHANNELPARCEL_H
#define MALAMORE_DATACHANNELPARCEL_H

#ifndef  DataChannelParcelArraySize
#define     DataChannelParcelArraySize      (1024)
#endif
#include <malamore/utility/kernel_compatibility.h>

/*// Structure representing the metadata you would save per session
struct QpcSnapshot {
    ULONGLONG bootTimeMsSinceEpoch; // Saved boot time (Unix epoch milliseconds)
    LARGE_INTEGER qpcTicks;         // The QPC ticks collected
    LARGE_INTEGER qpcFrequency;     // QPC frequency at the time of collection
};*/


    struct DataChannelParcel {
        ULONGLONG Timestamp;
        ULONG SourceNodeId;
        ULONG DataLength;
        UCHAR PayloadData[DataChannelParcelArraySize];
    };

#endif //MALAMORE_DATACHANNELPARCEL_H