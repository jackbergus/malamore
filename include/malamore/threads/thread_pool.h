//
// Created by Giacomo Bergami on 15/09/2026.
//

#ifndef MALAMORE_THREAD_POOL_H
#define MALAMORE_THREAD_POOL_H

#define MAX_POOL_CORES          (MAX_TOTAL_SLOTS)

#include <malamore/threads/ThreadGuard.h>
#include "malamore/structures/InterProcessChannelData.h"
#include "malamore/utility/telemetry_formatter.h"
#include "malamore/utility/streambuf/MalamoreIStream.h"

struct MalamoreThreadSlot {
    ULONG SlotId;
    enum MalamoreThreadType Type;
    KAFFINITY BaseAffinityMask;
    UnifiedThreadInterface threadArgument;
    ThreadGuard            actualThread;
};

// Lo Slot Omogeneo dell'array unico del pool statico
struct MalamoreTaskSlot {
    MalamoreThreadSlot* ContextAB[2];
    InterProcessChannelData IpcChannel;
    // // HANDLE hThreadHandleA;
    // // HANDLE hThreadHandleB;
    //
    // struct UnifiedThreadContext* ContextA; // Gestisce HW Reader o Task A del tandem
    // struct UnifiedThreadContext* ContextB; // Gestisce esclusivamente il Task B del tandem
    //
    // ThreadGuard* hThreadHandleA;
    // ThreadGuard* hThreadHandleB;
};

#define MALAMORE_MAX_DEVICES 16
#define MALAMORE_PATH_MAX    260

// Struttura interna della cella logica ad allocazione fitta
typedef struct _MALAMORE_DEVICE_ENTRY {
    MalamoreIStream Stream;
    WCHAR           DevicePath[MALAMORE_PATH_MAX];
    LONG            InUse; // Sbarra logica di occupazione: 0 = Libero, 1 = Occupato
} MALAMORE_DEVICE_ENTRY;


// =========================================================================
// CORE POOL REGISTRY CENTRALIZZATA (ARRAY UNICO OMOGENEO STATICO)
// =========================================================================
#pragma pack(push, 4)
struct CorePoolRegistry {
    struct MalamoreThreadSlot ThreadPool[MAX_TOTAL_SLOTS];
    struct MalamoreTaskSlot   Tasks[MAX_TOTAL_SLOTS];
    MALAMORE_DEVICE_ENTRY     Devices[MALAMORE_MAX_DEVICES];
    // InterProcessChannelData IpcChannels[MAX_GENERIC_TASKS];

    ULONG CurrentHwThreadsCount;
    ULONG CurrentGenericTasksCount;
    ULONG TotalAllocatedSlots;

    ULONG64 GlobalSynchronizedStartTicks;
    ULONG64 GlobalPerfCounterFrequency;
    volatile LONG IsRegistryActive;

    // Telemetry Management
    HANDLE                  hFileHandle;
    HANDLE                  hDiskThread;
    volatile LONG           TerminateDiskThread;
    InterProcessChannelData TelemetryChannel;
    QpcSnapshot             snapshot;
    // MalamoreThreadSlot      TelemetryThread;
};
#pragma pack(pop)

#ifdef __cplusplus
extern "C" {
#endif

#ifdef INCLUDE_UNTESTED_FEATURES

    NTSTATUS CorePool_RegisterHwReader(ULONG64 processId, const WCHAR* devicePath, ULONG readTimeoutUs, ULONG samplingPeriodUs, KAFFINITY coreAffinityMask, ULONG targetTaskId);
    NTSTATUS CorePool_RegisterGenericTask(ULONG64 processId, const char* taskName, ULONG executionPeriodUs, ULONG estimatedWcetUs, KAFFINITY coreAffinityMask, ULONG targetNodeId, BOOLEAN runRead, BOOLEAN runWrite, BOOLEAN saveDisk, PFN_EXECUTE_ROUTINE_IO executionFunction);
    NTSTATUS CorePool_StartPoolExecution();

#endif

    NTSTATUS CorePool_InitializeRegistry(ULONG PercentageToUse);
    NTSTATUS CorePool_RegisterHwReader(const WCHAR* devicePath, ULONG readTimeoutUs, ULONG samplingPeriodUs, ULONG targetTaskId);
    NTSTATUS CorePool_StartPoolExecution();
    VOID CorePool_ShutdownAndClear();

    /*
     * The others are just utility methods that are used
     *
     */
    BOOL isTelemetryQueueEmpty();
    VOID dumpBinaryTelemetryToFile();
    ULONG64 usecToTicks(const ULONG64 usec);
    NTSTATUS CorePool_RouteMessageToDest(ULONG targetNodeId, const struct DataChannelParcel* packet);
    NTSTATUS CorePool_RouteMessageSrcToDest(ULONGLONG Timestamp, ULONG SourceNodeId, ULONG targetNodeId,  const UCHAR* PayloadData ,ULONG DataLength);

    /*These methods are for managing the thread pool
     */
    /**
 * Inizializza i registri della memoria statica del pool.
 * Deve essere invocata dal manager all'avvio del sistema prima di innescare i task.
 */
    void MalamoreDevicePool_Initialize(void);

    /**
     * Aggiunge un Device Path al pool statico.
     * Se esiste già, restituisce l'indice. Altrimenti occupa il primo slot libero ed esegue la Open.
     * Ritorna l'indice assegnato [0...N-1] oppure -1 in caso di fallimento o pool esaurito.
     */
    int MalamoreDevicePool_AddDevicePath(const WCHAR* devicePath);

    /**
     * Pulisce e chiude la risorsa cercando per stringa letterale del percorso.
     * Se esiste, chiama la Close e libera lo slot per successive allocazioni.
     */
    void MalamoreDevicePool_CleanDeviceByPath(const WCHAR* devicePath);

    /**
     * Pulisce e chiude la risorsa cercando per indice geometrico del pool.
     * Se l'indice è valido ed è aperto, lo chiude e svuota la cella.
     */
    void MalamoreDevicePool_CleanDeviceByIndex(int index);

    /**
     * Estrattore per accedere allo stream di riferimento di una determinata posizione.
     * Restituisce il puntatore all'istanza MalamoreIStream o NULL se lo slot è libero.
     */
    MalamoreIStream* MalamoreDevicePool_GetStream(int index);


#ifdef __cplusplus
}
#endif



#endif //MALAMORE_THREAD_POOL_H