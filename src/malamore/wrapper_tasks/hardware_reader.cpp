//
// Created by Giacomo Bergami on 15/09/2026.
//

#include "malamore/wrapper_tasks/hardware_reader.h"
#include <intrin.h>
#include <ntddk.h>
#include <ntdef.h>
#include <winnt.h>
#include <ntifs.h>  // Include for NT kernel API
#include <ntddk.h>  // Basic kernel DDK types and functions


// #include "malamore/thread_generic_polling.h"

VOID HardwareReaderThreadRoutine(_In_ PVOID Context) {
    HWTaskContext* config = static_cast<HWTaskContext *>(Context);
    if (!config) {
        KdPrint(("HardwareReaderContext] Invalid configuration (nullptr)\n"));
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
        return;
    }
    if (!clear_map(&config->OutputMap)) {
        KdPrint(("HardwareReaderContext] Cannot clear the output map\n"));
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
        return;
    }

    NTSTATUS status;
    HANDLE hDevice = NULL;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    UNICODE_STRING uniName;
    KEVENT ioEvent;

    LARGE_INTEGER currentTime, nextPeriodTime;
    GROUP_AFFINITY targetAffinity, previousAffinity;
    ULONG64 sequence = 0;


    // 1. PINNING DEL CORE FISICO
    RtlZeroMemory(&targetAffinity, sizeof(GROUP_AFFINITY));
    targetAffinity.Mask = config->AffinityMask;
    targetAffinity.Group = 0;
    KeSetSystemGroupAffinityThread(&targetAffinity, &previousAffinity);

    // Inizializzazione dell'evento hardware di sincronizzazione per l'I/O asincrono
    KeInitializeEvent(&ioEvent, NotificationEvent, FALSE);

    // =========================================================================
    // STEP 1: APERTURA DINAMICA (ACCETTA SIA DRIVERS CHE FILE NEL FILE SYSTEM)
    // =========================================================================
    // Carica la stringa passata come argomento dal chiamante nel contesto
    RtlInitUnicodeString(&uniName, config->DevicePath);
    InitializeObjectAttributes(&objAttr, &uniName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);

    status = ZwCreateFile(
        &hDevice,
        GENERIC_READ | SYNCHRONIZE,
        &objAttr,
        &ioStatus,
        NULL,                                       // AllocationSize
        FILE_ATTRIBUTE_NORMAL,                      // FileAttributes
        FILE_SHARE_READ,                            // Condivisione aperta per file system
        FILE_OPEN_IF,                               // Apre se esiste, crea se manca (Universal Fallback)
        FILE_NON_DIRECTORY_FILE,                    // Rimosso FILE_SYNCHRONOUS_IO per abilitare l'I/O ASINCRONO
        NULL, 0
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("[MALAMORE 🚨] Impossibile aprire la risorsa '%ws'. Errore: 0x%X\n", config->DevicePath, status));
        KeRevertToUserGroupAffinityThread(&previousAffinity);
        PsTerminateSystemThread(status);
        return;
    }

    // Allineamento temporale iniziale sulla barriera comune pre-calcolata
    nextPeriodTime.QuadPart = config->SynchronizedStartTicks;

    // =========================================================================
    // LOOP PRINCIPALE DI LETTURA PERIODICA ASINCRONA (PASSIVE_LEVEL - 3.33ms)
    // =========================================================================
    while (KernelAtomicRead(&config->KeepRunning) == 1) {

        // Attesa attiva di precisione a PASSIVE_LEVEL
        do {
            YieldProcessor();
            currentTime = KeQueryPerformanceCounter(NULL);
        } while ((currentTime.QuadPart < nextPeriodTime.QuadPart) && (KernelAtomicRead(&config->KeepRunning) == 1));

        if (KernelAtomicRead(&config->KeepRunning) == 0) break;

        UCHAR idata[HARDWARE_PAYLOAD_SIZE];
        RtlZeroMemory(idata, HARDWARE_PAYLOAD_SIZE);

        // Reset dello stato dell'evento hardware prima di lanciare la lettura asincrona
        KeResetEvent(&ioEvent);

        // =========================================================================
        // STEP 2: LETTURA ASINCRONA NON BLOCCANTE CON EVENTO DI SEGNALAZIONE
        // =========================================================================
        status = ZwReadFile(
            hDevice,
            NULL,                       // Nessun handle di evento qui (usiamo la sincronizzazione manuale dell'IRP)
            NULL, NULL,                 // ApcRoutine, ApcContext
            &ioStatus,
            idata,                      // Buffer lineare idata
            HARDWARE_PAYLOAD_SIZE,      // sizeof(idata)
            NULL,                       // Legge dalla posizione corrente del puntatore file stream
            NULL
        );

        // Se l'I/O non si è concluso istantaneamente inline, entra in modalità di attesa asincrona controllata
        if (status == STATUS_PENDING) {

            // CONFIGURAZIONE DEL TIMEOUT RIGIDO DEL KERNEL
            // KeWaitForSingleObject vuole un tempo relativo espresso in unità di 100 nanosecondi negativo
            LARGE_INTEGER timeout;
            if ((LONGLONG)(config->ReadTimeoutUs * 10) < config->PeriodTicks) {
                timeout.QuadPart = -(LONGLONG)(config->ReadTimeoutUs * 10); // Conversione Microsecondi -> 100ns
            }

            // ATTESA CON TIMEOUT: Blocca il core solo per il budget concesso dal Client
            NTSTATUS waitStatus = KeWaitForSingleObject(&ioEvent, Executive, KernelMode, FALSE, &timeout);

            if (waitStatus == STATUS_TIMEOUT) {
                // HARDWARE TIMEOUT COMPENSATO: Il dispositivo non ha risposto in tempo.
                // Passiamo direttamente al tentativo successivo senza mandare il tandem in stallo!
                KdPrint(("[MALAMORE ⚠️] Timeout di lettura risorsa esaurito. Salto al ciclo successivo.\n"));

                // Opzione avanzata: Invia un comando di annullamento I/O per l'IRP orfano (ZwCancelIoFile)
                ZwCancelIoFile(hDevice, &ioStatus);

                nextPeriodTime.QuadPart += config->PeriodTicks;
                continue;
            } else {
                // CORREZIONE CRUCIALE: L'evento si è svegliato! L'operazione è terminata.
                // Estraiamo il vero codice di esito finale iniettato dall'I/O Manager nel blocco di stato.
                status = ioStatus.Status; // <--- RISOLUTIVO: Può essere STATUS_SUCCESS o un errore hardware reale
            }
        }

        // Se la lettura si è conclusa con successo (inline o post-attesa entro il timeout)
        if (NT_SUCCESS(status) || (status == STATUS_SUCCESS)) {
            ULONG letti = (ULONG)ioStatus.Information; // Variabile &tmp

            if (letti > 0) {

            }
        }

        // Avanzamento rigidamente periodico calibrato a 3.33ms (3333 microsecondi)
        nextPeriodTime.QuadPart += config->PeriodTicks;
    }

    // =========================================================================
    // STEP 3: CHIUSURA DEL DISPOSITIVO DINAMICO ALLA TERMINAZIONE
    // =========================================================================
    if (hDevice) {
        ZwClose(hDevice);
    }

    KeRevertToUserGroupAffinityThread(&previousAffinity);
    PsTerminateSystemThread(STATUS_SUCCESS);
}
