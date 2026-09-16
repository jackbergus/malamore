//
// Created by Giacomo Bergami on 16/09/2026.
//

#ifndef MALAMORE_RESERVE_CPUS_H
#define MALAMORE_RESERVE_CPUS_H

/**
 * This is an extension of the WindowsIoT CSP Soft Real-Time Performance configuration
 * (https://learn.microsoft.com/en-us/windows/iot/iot-enterprise/soft-real-time/soft-real-time-device#use-mdm-bridge-wmi-provider-to-configure-the-windowsiot-csp)
 * and assumes you have read the  documentation. 'SetRTCores' essentially prevents interrupts and tasks from being
 * scheduled on reserved cores so that you can isolate real-time applications from user and kernel-level disturbances
 * by configuring affinity policies.
 *
 * Important!!
 *
 * Unexpected behavior occurs when a process affinity is set to reserved and unreserved CPUs. Ensure to set the affinity
 * to either reserved or unreserved CPUs, not a combination of both.
 * In the program, reverting the changes is equivalent to unchecking all CPUs then saving changes.
 *
 * Full Version: https://github.com/valleyofdoom/ReservedCpuSets/blob/main/README.md
 *
 * @param mask The mask of the CPU IDs to be reserved, starting to count from the left to the right
 * @return A negative number corresponding to the type of issue being encountered, or 1 otherwise
 */
extern "C" int SetSystemCpuSet(unsigned long mask);

#endif //MALAMORE_RESERVE_CPUS_H