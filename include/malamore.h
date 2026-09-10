/*
* malamore
Copyright (C) 2026 Giacomo Bergami

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once



#ifdef LINK_KERNEL
#include <ntddk.h>
#else
#include <windows.h>
#endif

#define MY_DEVICE_TYPE 0x8000
#define IOCTL_REGISTER_SERVICE_MANAGER CTL_CODE(MY_DEVICE_TYPE, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_BINDER_COMMAND           CTL_CODE(MY_DEVICE_TYPE, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)

enum BinderCmdType {
    CMD_SPAWN_NODE = 1,
    CMD_CONNECT_NODES = 2,
    CMD_SIGNAL_NODE = 3,
    CMD_DESTROY_NODE = 4,
    CMD_REGISTER_PERIODIC_TASK = 5
};

enum BinderResponseStatus {
    STATUS_BINDER_OK = 0,
    STATUS_BINDER_CORE_EXHAUSTED = -1,
    STATUS_BINDER_ERROR = -2
};

#pragma pack(push, 1)
struct TaskRealtimeMeta {
    char TaskName[64];
    ULONG PeriodUs;            // Periodicità in Microsecondi (T)
    ULONG EstimatedWcetUs;     // Worst-Case Execution Time in Microsecondi (C)
    ULONG64 NextReleaseTicks;
    ULONG64 AbsoluteDeadlineTicks;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct BinderParcel {
    ULONG CommandType;
    char TargetNodeName[64];
    char DllPath[260];
    char SourceNodeName[64];
    UCHAR PayloadData[1024];// Buffer per lo scambio dati a lunghezza variabile
    ULONG DataLength; // Lunghezza effettiva/dinamica dei byte trasferiti
    LONG ResponseStatus;
    TaskRealtimeMeta TaskMeta;
};
#pragma pack(pop)

#define CHANNEL_BUFFER_SIZE 4096
#pragma pack(push, 1)
struct InterProcessChannelData {
    volatile LONG ReadIndex;
    volatile LONG WriteIndex;
    volatile LONG MessageCount;
    UCHAR Buffer[CHANNEL_BUFFER_SIZE];
};
#pragma pack(pop)
