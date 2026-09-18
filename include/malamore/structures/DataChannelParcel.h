//
// Created by Giacomo Bergami on 17/09/2026.
//

#ifndef MALAMORE_DATACHANNELPARCEL_H
#define MALAMORE_DATACHANNELPARCEL_H

#ifndef  DataChannelParcelArraySize
#define     DataChannelParcelArraySize      (1024)
#include "malamore/utility/kernel_compatibility.h"
#endif


    struct DataChannelParcel {
        ULONGLONG Timestamp;
        ULONG SourceNodeId;
        ULONG DataLength;
        UCHAR PayloadData[DataChannelParcelArraySize];
    };

#endif //MALAMORE_DATACHANNELPARCEL_H