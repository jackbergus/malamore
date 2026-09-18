//
// Created by Giacomo Bergami on 16/09/2026.
//

/**
 * This header file provides a compatibility layer for
 */

#ifndef MALAMORE_KERNEL_COMPATIBILITY_H
#define MALAMORE_KERNEL_COMPATIBILITY_H

//
// Created by Giacomo Bergami on 15/09/2026.
//

#pragma once

#ifdef LINK_KERNEL
    // =========================================================================
    // COMPILAZIONE DRIVER (RING 0): Inclusione degli header nativi del WDK
    // =========================================================================
    #include <ntddk.h>
    #include <ntstrsafe.h>


    // Per completezza a Ring 0, alcune macro sono già fornite dal WDK
    #ifndef UNREFERENCED_PARAMETER
        #define UNREFERENCED_PARAMETER(P) (void)(P)
    #endif

#else
    // =========================================================================
    // COMPILAZIONE SIMULATA CLIENT/TEST (RING 3): Mappatura su Win32/MSVC
    // =========================================================================
    #include <windows.h>
    #include <synchapi.h>
    #include <strsafe.h>
    #include <stdio.h>
    #include <intrin.h> // <--- FONDAMENTALE: Espone le funzioni intrinseche della CPU (_Interlocked*)


    // 1. Tipi di dato e macro di controllo fondamentali del WDK
    typedef LONG          NTSTATUS;
    typedef void*         PVOID;
    typedef char*         PCHAR;
    typedef unsigned char UCHAR;
    typedef unsigned char* PUCHAR;
    typedef unsigned long ULONG;
    typedef unsigned long* PULONG;
    typedef ULONG_PTR     KAFFINITY;
    typedef BOOLEAN       *PBOOLEAN;
    // typedef unsigned short WCHAR;
    // typedef WCHAR*        PWSTR;
    // typedef const WCHAR*  PCWSTR;
    // typedef char          BOOLEAN;

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif



// ALLINEAMENTO COMPLETO DEI CODICI SIMBOLICI NTSTATUS DI RIFERIMENTO
    #ifndef STATUS_SUCCESS
        #define STATUS_SUCCESS                  ((NTSTATUS)0x00000000L)
    #endif
    #ifndef STATUS_PENDING
        #define STATUS_PENDING                  ((NTSTATUS)0x00000103L)
    #endif
    #ifndef STATUS_TIMEOUT
        #define STATUS_TIMEOUT                  ((NTSTATUS)0x00000102L)
    #endif
    #ifndef STATUS_UNSUCCESSFUL
        #define STATUS_UNSUCCESSFUL             ((NTSTATUS)0xC0000001L)
    #endif
    #ifndef STATUS_INVALID_PARAMETER
        #define STATUS_INVALID_PARAMETER        ((NTSTATUS)0xC000000DL)
    #endif
    #ifndef STATUS_INSUFFICIENT_RESOURCES
        #define STATUS_INSUFFICIENT_RESOURCES   ((NTSTATUS)0xC000009AL)
    #endif
    #ifndef STATUS_INVALID_DEVICE_STATE
        #define STATUS_INVALID_DEVICE_STATE     ((NTSTATUS)0xC0000184L)
    #endif

    // NUOVI CODICI AGGIUNTI PER COERENZA DIAGNOSTICA CROSS-LAYER
    #ifndef STATUS_ACCESS_VIOLATION
        #define STATUS_ACCESS_VIOLATION         ((NTSTATUS)0xC0000005L)
    #endif
    #ifndef STATUS_ACCESS_DENIED
        #define STATUS_ACCESS_DENIED            ((NTSTATUS)0xC0000022L)
    #endif
    #ifndef STATUS_OBJECT_NAME_NOT_FOUND
        #define STATUS_OBJECT_NAME_NOT_FOUND    ((NTSTATUS)0xC0000034L)
    #endif
    #ifndef STATUS_OBJECT_PATH_NOT_FOUND
        #define STATUS_OBJECT_PATH_NOT_FOUND    ((NTSTATUS)0xC000003AL)
    #endif
    #ifndef STATUS_SHARING_VIOLATION
        #define STATUS_SHARING_VIOLATION        ((NTSTATUS)0xC0000043L)
    #endif
    #ifndef STATUS_INVALID_HANDLE
        #define STATUS_INVALID_HANDLE           ((NTSTATUS)0xC0000008L)
    #endif
    #ifndef STATUS_END_OF_FILE
        #define STATUS_END_OF_FILE              ((NTSTATUS)0xC0000011L)
    #endif
    #ifndef STATUS_CANCELLED
        #define STATUS_CANCELLED                ((NTSTATUS)0xC0000120L)
    #endif
    #ifndef STATUS_DEVICE_NOT_READY
        #define STATUS_DEVICE_NOT_READY         ((NTSTATUS)0xC000009DL)
    #endif
    #ifndef STATUS_BUFFER_OVERFLOW
        #define STATUS_BUFFER_OVERFLOW          ((NTSTATUS)0x40000006L)
    #endif
    #ifndef STATUS_BUFFER_TOO_SMALL
        #define STATUS_BUFFER_TOO_SMALL         ((NTSTATUS)0xC0000023L)
    #endif


#ifndef OBJ_CASE_INSENSITIVE
    #define OBJ_CASE_INSENSITIVE          0x00000040L
#endif

#ifndef OBJ_KERNEL_HANDLE
    #define OBJ_KERNEL_HANDLE             0x00000200L
#endif

#ifndef FILE_SHARE_READ
#define FILE_SHARE_READ               0x00000001
#endif

#ifndef FILE_OPEN_IF
#define FILE_OPEN_IF                  0x00000003
#endif

#ifndef FILE_NON_DIRECTORY_FILE
#define FILE_NON_DIRECTORY_FILE       0x00000040
#endif

#ifndef FILE_ATTRIBUTE_NORMAL
#define FILE_ATTRIBUTE_NORMAL         0x00000080
#endif

#ifndef ASYNC_READ_TAG
#define ASYNC_READ_TAG 0x4153594E
#endif

#ifndef FILE_APPEND_DATA
#define FILE_APPEND_DATA              0x00000004
#endif
#ifndef FILE_WRITE_DATA
#define FILE_WRITE_DATA               0x00000002
#endif

#ifndef FILE_SUPERSEDE
#define FILE_SUPERSEDE            0x00000000 // Valore reale nel WDK
#endif
#ifndef FILE_OPEN
#define FILE_OPEN                 0x00000001 // Valore reale nel WDK
#endif
#ifndef FILE_OPEN_IF
#define FILE_OPEN_IF              0x00000003 // Valore reale nel WDK
#endif

#ifndef PKSTART_ROUTINE
// Mappa la firma della routine di Ring 0 sul tipo utente di Win32
// PKSTART_ROUTINE a Ring 0 è un puntatore a funzione: VOID (*PKSTART_ROUTINE)(PVOID Context)
typedef DWORD (WINAPI *PKSTART_ROUTINE)(LPVOID lpThreadParameter);
#endif

    #ifndef NT_SUCCESS
        #define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
    #endif

    #ifndef UNREFERENCED_PARAMETER
        #define UNREFERENCED_PARAMETER(P) (void)(P)
    #endif

// RISOLUTIVO: Specifica la maschera nativa di Ring 0 per impedire l'apertura di directory
#ifndef FILE_NON_DIRECTORY_FILE
#define FILE_NON_DIRECTORY_FILE   0x00000040 // <--- AGGIUNTO PER LA COMPILAZIONE DI KERNELSTREAM
#endif

// Mappatura dell'oggetto Evento del Kernel sull'Handle Win32 ordinario
typedef struct _KEVENT {
    HANDLE Handle;
    OVERLAPPED Overlapped; // <--- AGGIUNTA: Il contesto asincrono vive e muore con l'evento!
} KEVENT, *PKEVENT;


typedef enum _EVENT_TYPE {
    NotificationEvent,
    SynchronizationEvent
} EVENT_TYPE;


    // Strutture Kernel simulate a Ring 3
    struct UNICODE_STRING {
        unsigned short Length;
        unsigned short MaximumLength;
        PWSTR Buffer;
    };
    typedef struct UNICODE_STRING *PUNICODE_STRING;

    struct OBJECT_ATTRIBUTES {
        ULONG Length;
        HANDLE RootDirectory;
        PUNICODE_STRING ObjectName;
        ULONG Attributes;
        PVOID SecurityDescriptor;
        PVOID SecurityQualityOfService;
    };
    typedef struct OBJECT_ATTRIBUTES *POBJECT_ATTRIBUTES;

    #pragma pack(push, 1)
    struct IO_STATUS_BLOCK {
        NTSTATUS Status;
        ULONG_PTR Information;
    };
    typedef struct IO_STATUS_BLOCK *PIO_STATUS_BLOCK;
    #pragma pack(pop)

    // 2. Wrapper Macro per la manipolazione ed atomicità della memoria
    // #define RtlZeroMemory(Destination, Length)          memset((Destination), 0, (Length))
    // #define RtlCopyMemory(Destination, Source, Length)  memcpy((Destination), (Source), (Length))

    // #define InterlockedOr(Target, Value)            InterlockedOr((volatile LONG*)(Target), (LONG)(Value))
    // #define InterlockedExchange(Target, Value)      InterlockedExchange((volatile LONG*)(Target), (LONG)(Value))
    // #define InterlockedIncrement(Target)            InterlockedIncrement((volatile LONG*)(Target))
    // #define InterlockedDecrement(Target)            InterlockedDecrement((volatile LONG*)(Target))

    // #define YieldProcessor()                        _mm_pause()

// Routine di utilità per le stringhe kernel finti
inline void RtlInitUnicodeString(PUNICODE_STRING DestinationString, PCWSTR SourceString) {
    if (DestinationString && SourceString) {
        DestinationString->Buffer = const_cast<PWSTR>(SourceString);
        size_t len = wcslen(SourceString);
        DestinationString->Length = static_cast<unsigned short>(len * sizeof(WCHAR));
        DestinationString->MaximumLength = static_cast<unsigned short>((len + 1) * sizeof(WCHAR));
    }
}

inline void InitializeObjectAttributes(POBJECT_ATTRIBUTES p, PUNICODE_STRING n, ULONG a, HANDLE r, PVOID s) {
    p->Length = sizeof(OBJECT_ATTRIBUTES);
    p->RootDirectory = r;
    p->ObjectName = n;
    p->Attributes = a;
    p->SecurityDescriptor = s;
    p->SecurityQualityOfService = NULL;
}

    // 3. Stringhe e Output di Debug protetti
    #define RtlStringCchPrintfA                     StringCchPrintfA
    #define RtlStringCchLengthA                     StringCchLengthA
    #define KdPrint(FormatAndArgs)                  printf FormatAndArgs
    #define RtlStringCchCopyA                       StringCchCopyA
    #define RtlStringCchCopyW                       StringCchCopyW // <--- RISOLUTIVO PER KERNELSTREAM!
    #define RtlStringCchLengthW                     StringCchLengthW // <--- PERFETTAMENTE RISOLTO ADESSO!
    #define RtlStringCchCatW                        StringCchCatW // <--- ABILITATO E RISOLTO ADESSO!
    #define RtlStringCchPrintfW                     StringCchPrintfW // <--- RISOLUTIVO: Abilitato ora per MalamorePath!


    // 4. Simulazione delle routine del ciclo di vita dei Thread e File di Ring 0
    #define PsTerminateSystemThread(Status)         ExitThread((DWORD)(Status))

// =========================================================================
    // SIMULAZIONE DELLE ROUTINE DI I/O DI RING 0 A LIVELLO UTENTE (ZW*)
    // =========================================================================

    // Helper interno per ripulire i percorsi nativi di Ring 0 (\??\C:\... o \DosDevices\) in percorsi Win32 validi
    inline PCWSTR ParseKernelPathToWin32(PCWSTR kernelPath) {
        if (kernelPath == NULL) return L"";
        // Salta il prefisso \??\ (4 caratteri) se presente
        if (wcsncmp(kernelPath, L"\\??\\", 4) == 0) {
            return kernelPath + 4;
        }
        // Salta il prefisso \DosDevices\ (12 caratteri) se presente
        if (wcsncmp(kernelPath, L"\\DosDevices\\", 12) == 0) {
            return kernelPath + 12;
        }
        return kernelPath;
    }

    inline NTSTATUS ZwCreateFile(
        PHANDLE FileHandle,
        ACCESS_MASK DesiredAccess,
        POBJECT_ATTRIBUTES ObjectAttributes,
        PIO_STATUS_BLOCK IoStatusBlock,
        PLARGE_INTEGER AllocationSize,
        ULONG FileAttributes,
        ULONG ShareAccess,
        ULONG CreateDisposition,
        ULONG CreateOptions,
        PVOID EaBuffer,
        ULONG EaLength
    ) {
        UNREFERENCED_PARAMETER(AllocationSize);
        UNREFERENCED_PARAMETER(CreateOptions);
        UNREFERENCED_PARAMETER(EaBuffer);
        UNREFERENCED_PARAMETER(EaLength);
        if (FileHandle == NULL || ObjectAttributes == NULL || ObjectAttributes->ObjectName == NULL || IoStatusBlock == NULL)
            return STATUS_INVALID_PARAMETER;

        PCWSTR win32Path = ParseKernelPathToWin32(ObjectAttributes->ObjectName->Buffer);

        // CORREZIONE HARDWARE: Mappatura millimetrica e fedele dei codici di Ring 0 su Win32
        DWORD creationDisposition = OPEN_EXISTING; // Default di sicurezza fallimentare

        if (CreateDisposition == 0) {
            // 0x00000000 = FILE_SUPERSEDE -> Sovrascrive interamente azzerando i settori
            creationDisposition = CREATE_ALWAYS;
        }
        else if (CreateDisposition == 1) {
            // 0x00000001 = FILE_OPEN -> Apre esistente, fallisce se manca
            creationDisposition = OPEN_EXISTING;
        }
        else if (CreateDisposition == 3) {
            // 0x00000003 = FILE_OPEN_IF -> Apre se c'è, crea se manca (senza distruggere i dati)
            creationDisposition = OPEN_ALWAYS;
        }

    // Manteniamo intatto il parametro originale specificato dal chiamante
    DWORD shareMode = ShareAccess;

    // APPLICAZIONE CONDIZIONALE PROTETTA:
    // Eseguiamo il fallback di condivisione SOLO se l'oggetto è palesemente un file regolare su disco
    // (identificabile dall'assenza dei prefissi hardware e dalla presenza di un'estensione come .txt o .bin)
    // se e solo se non stiamo negoziando una Named Pipe o un Device Link (\??\ o \DosDevices\)
    if (shareMode == 0 &&
        wcsncmp(win32Path, L"\\\\.\\pipe\\", 9) != 0 &&
        wcsncmp(ObjectAttributes->ObjectName->Buffer, L"\\??\\", 4) != 0 &&
        wcsncmp(ObjectAttributes->ObjectName->Buffer, L"\\DosDevices\\", 12) != 0)
    {
        // Solo per i file di log/telemetria standard forziamo la condivisione per evitare che i blocchi di scope
        // successivi rimangano blindati fuori dal file system a causa del delayed metadata flushing.
        shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
    }
    ShareAccess |= shareMode;

        HANDLE hFile = CreateFileW(win32Path, DesiredAccess, ShareAccess, NULL, creationDisposition, FileAttributes | FILE_FLAG_OVERLAPPED, NULL);
        if (hFile == INVALID_HANDLE_VALUE) {
            IoStatusBlock->Status = STATUS_UNSUCCESSFUL;
            return STATUS_UNSUCCESSFUL;
        }

        *FileHandle = hFile;
        IoStatusBlock->Status = STATUS_SUCCESS;
        return STATUS_SUCCESS;
    }

    inline NTSTATUS ZwReadFile(
        HANDLE FileHandle,
        HANDLE Event,
        PVOID ApcRoutine,
        PVOID ApcContext,
        PIO_STATUS_BLOCK IoStatusBlock,
        PVOID Buffer,
        ULONG Length,
        PLARGE_INTEGER ByteOffset,// Usato come selettore di contesto: -1 = Asincrono Puro, NULL = Sincrono/Bloccante
        PULONG Key
    ) {
#ifdef PREVDEF
        UNREFERENCED_PARAMETER(Event);
        UNREFERENCED_PARAMETER(ApcRoutine);
        UNREFERENCED_PARAMETER(ApcContext);
        UNREFERENCED_PARAMETER(ByteOffset);
        UNREFERENCED_PARAMETER(Key);

        if (FileHandle == NULL || FileHandle == INVALID_HANDLE_VALUE || IoStatusBlock == NULL || Buffer == NULL) {
            return STATUS_INVALID_PARAMETER;
        }

        // Estrae la struttura KEVENT reale passata dal modulo
        PKEVENT pKernelEvent = reinterpret_cast<PKEVENT>(Event);
        // Allocazione localizzata nello stack della maschera asincrona nativa di Win32
        OVERLAPPED overlappedContext;
        RtlZeroMemory(&overlappedContext, sizeof(OVERLAPPED));

        // Se lo stream ha configurato un evento di notifica, lo leghiamo al contesto Overlapped
        if (pKernelEvent != nullptr && pKernelEvent->Handle != NULL) {
            overlappedContext.hEvent = pKernelEvent->Handle;
        }

        // 1. DETERMINAZIONE UNIFICATA DELLA NATURA DEL DESCRITTORE Win32
        DWORD fileType = GetFileType(FileHandle); //
        LARGE_INTEGER currentFilePointer = { 0 };
        // Se si tratta di un file regolare sul disco (FILE_TYPE_DISK), Windows NT Overlapped
        // esige che l'offset sia estratto e dichiarato manualmente ad ogni transazione!
        if (fileType == FILE_TYPE_DISK) { //
            // Estrae la posizione logica corrente del cursore memorizzata nell'handle
            currentFilePointer.QuadPart = 0;
            currentFilePointer.LowPart = SetFilePointer(FileHandle, 0, &currentFilePointer.HighPart, FILE_CURRENT); //

            // Iniettiamo l'offset esatto richiesto dall'I/O Manager
            overlappedContext.Offset = currentFilePointer.LowPart;
            overlappedContext.OffsetHigh = currentFilePointer.HighPart;
        }

        DWORD bytesRead = 0;
        // Simulazione della lettura asincrona non bloccante.
        // Nota: se desideri emulare STATUS_PENDING nei test, puoi forzarlo condizionalmente qui.
        BOOL success = ReadFile(FileHandle, Buffer, Length, &bytesRead, &overlappedContext);

        IoStatusBlock->Information = bytesRead;
        if (success) {
            IoStatusBlock->Information = bytesRead;
            IoStatusBlock->Status = STATUS_SUCCESS;

            // SE IL FILE È SU DISCO: Spostiamo in avanti il cursore Win32 in modalità atomica,
            // emulando fedelmente l'avanzamento automatico nativo di Linux e del WDK!
            if (fileType == FILE_TYPE_DISK && bytesRead > 0) { //
                LARGE_INTEGER moveOffset; moveOffset.QuadPart = bytesRead;
                SetFilePointer(FileHandle, moveOffset.LowPart, &moveOffset.HighPart, FILE_CURRENT); //
            }
            return STATUS_SUCCESS;
        }

        DWORD lastError = GetLastError();

        // IL VERO COMPORTAMENTO DI RING 0 ABILITATO IN USER-MODE:
        // Se Windows NT prende in carico l'operazione in background, l'API ritorna ERROR_IO_PENDING.
        // Lo traduciamo istantaneamente in STATUS_PENDING sbloccando ZwReadFile!
        if (lastError == ERROR_IO_PENDING) {
            IoStatusBlock->Status = STATUS_PENDING;

            // Nelle operazioni asincrone differite pendenti, il riallineamento dell'offset
            // verrà finalizzato dalla successiva GetOverlappedResult o KeWaitForSingleObject.
            // Per i test sequenziali ordinari a 1 carattere, il Fast I/O passa sempre dal ramo sopra.
            return STATUS_PENDING; // <--- RISOLUTIVO: ZwReadFile ritorna subito e non si pianta più!
        }

        // Gestione degli altri errori fisici reali (es. file system rimosso o pipe rotta)
        IoStatusBlock->Information = 0;
        IoStatusBlock->Status = STATUS_UNSUCCESSFUL;
        return STATUS_UNSUCCESSFUL;
#else
UNREFERENCED_PARAMETER(ApcRoutine); UNREFERENCED_PARAMETER(ApcContext); UNREFERENCED_PARAMETER(ByteOffset);
        if (FileHandle == NULL || FileHandle == INVALID_HANDLE_VALUE || IoStatusBlock == NULL || Buffer == NULL) {
            return STATUS_INVALID_PARAMETER;
        }

        PKEVENT pKernelEvent = reinterpret_cast<PKEVENT>(Event);
        if (pKernelEvent == nullptr || pKernelEvent->Handle == NULL) {
            return STATUS_INVALID_PARAMETER;
        }
        // OVERLAPPED overlappedContext;
        // RtlZeroMemory(&overlappedContext, sizeof(OVERLAPPED));

        // if (pKernelEvent != nullptr && pKernelEvent->Handle != NULL) {
        //     overlappedContext.hEvent = pKernelEvent->Handle;
        // }
    // CORREZIONE DEFINITIVA: Usiamo la struttura OVERLAPPED persistente integrata nell'evento!
    // In questo modo l'I/O Manager registra l'operazione su una memoria stabile che NON viene distrutta al return!
    LPOVERLAPPED pMyOverlapped = &pKernelEvent->Overlapped;


        // 1. GESTIONE MANUALE DELL'OFFSET PER I FILE SU DISCO
        DWORD fileType = GetFileType(FileHandle);
        LARGE_INTEGER currentFilePointer = { 0 };

        if (fileType == FILE_TYPE_DISK) {
            /*currentFilePointer.QuadPart = 0;
            currentFilePointer.LowPart = SetFilePointer(FileHandle, 0, &currentFilePointer.HighPart, FILE_CURRENT);
            overlappedContext.Offset = currentFilePointer.LowPart;
            overlappedContext.OffsetHigh = currentFilePointer.HighPart;*/
            LARGE_INTEGER currentFilePointer = { 0 };
            currentFilePointer.QuadPart = 0;
            currentFilePointer.LowPart = SetFilePointer(FileHandle, 0, &currentFilePointer.HighPart, FILE_CURRENT);
            pMyOverlapped->Offset = currentFilePointer.LowPart;
            pMyOverlapped->OffsetHigh = currentFilePointer.HighPart;
        }

        DWORD bytesRead = 0;
        BOOL success = ReadFile(FileHandle, Buffer, Length, &bytesRead, pMyOverlapped);

        if (success) {
            IoStatusBlock->Information = bytesRead;
            IoStatusBlock->Status = STATUS_SUCCESS;

            if (fileType == FILE_TYPE_DISK && bytesRead > 0) {
                LARGE_INTEGER moveOffset; moveOffset.QuadPart = bytesRead;
                SetFilePointer(FileHandle, moveOffset.LowPart, &moveOffset.HighPart, FILE_CURRENT);
            }
            return STATUS_SUCCESS;
        }

        DWORD lastError = GetLastError();

        if (lastError == ERROR_IO_PENDING) {
            // VERIFICA DEL CONTESTO SEMANTICO TRAMITE IL TAG KEY PROTETTO
            if (Key != nullptr && *Key == ASYNC_READ_TAG) {
                // CASO A: LETTURA ASINCRONA PURA (ReadWithTimeout)
                // Ritorna immediatamente STATUS_PENDING, delegando l'attesa oraria esterna
                IoStatusBlock->Status = STATUS_PENDING;
                IoStatusBlock->Information = 0;
                return STATUS_PENDING;
            } else {
                // CASO B: LETTURA SINCRONA COMPENSATA (Sgetn_Blocking / Sgetc)
                // Forza l'attesa immediata e scarica i byte, risolvendo il bug della 't' fissa
                DWORD bytesTransferred = 0;
                if (GetOverlappedResult(FileHandle, pMyOverlapped, &bytesTransferred, TRUE)) {
                    IoStatusBlock->Information = bytesTransferred;
                    IoStatusBlock->Status = STATUS_SUCCESS;

                    if (fileType == FILE_TYPE_DISK && bytesTransferred > 0) {
                        LARGE_INTEGER moveOffset; moveOffset.QuadPart = bytesTransferred;
                        SetFilePointer(FileHandle, moveOffset.LowPart, &moveOffset.HighPart, FILE_CURRENT);
                    }
                    return STATUS_SUCCESS;
                }
            }
        }

        IoStatusBlock->Information = 0;
        IoStatusBlock->Status = STATUS_UNSUCCESSFUL;
        return STATUS_UNSUCCESSFUL;
#endif
    }

    inline NTSTATUS ZwWriteFile(
        HANDLE FileHandle,
        HANDLE Event,
        PVOID ApcRoutine,
        PVOID ApcContext,
        PIO_STATUS_BLOCK IoStatusBlock,
        PVOID Buffer,
        ULONG Length,
        PVOID ByteOffset,
        PULONG Key
    ) {
#ifdef PREVDEF
        UNREFERENCED_PARAMETER(Event);
        UNREFERENCED_PARAMETER(ApcRoutine);
        UNREFERENCED_PARAMETER(ApcContext);
        UNREFERENCED_PARAMETER(ByteOffset);
        UNREFERENCED_PARAMETER(Key);

        if (FileHandle == NULL || FileHandle == INVALID_HANDLE_VALUE || IoStatusBlock == NULL) {
            return STATUS_INVALID_PARAMETER;
        }

        DWORD bytesWritten = 0;
        BOOL success = WriteFile(FileHandle, Buffer, Length, &bytesWritten, NULL);

        IoStatusBlock->Information = bytesWritten;
        if (success) {
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        } else {
            IoStatusBlock->Status = STATUS_UNSUCCESSFUL;
            return STATUS_UNSUCCESSFUL;
        }
#else
    UNREFERENCED_PARAMETER(ApcRoutine);
    UNREFERENCED_PARAMETER(ApcContext);
    UNREFERENCED_PARAMETER(ByteOffset);
    UNREFERENCED_PARAMETER(Key);

    if (FileHandle == NULL || FileHandle == INVALID_HANDLE_VALUE || IoStatusBlock == NULL || Buffer == NULL) {
        return STATUS_INVALID_PARAMETER; //
    }

    // Estrae l'oggetto KEVENT finto dal parametro astratto
    PKEVENT pKernelEvent = reinterpret_cast<PKEVENT>(Event);
    if (pKernelEvent == nullptr || pKernelEvent->Handle == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    LPOVERLAPPED pMyOverlapped = &pKernelEvent->Overlapped;

    // CORREZIONE HARDWARE CRITICA: Windows esige che hEvent sia VALIDO ad ogni costo.
    // Forziamo il riallineamento dell'Handle Win32 autentico subito prima del lancio di WriteFile,
    // annullando qualsiasi corruzione o azzeramento residuo avvenuto nei passi precedenti!
    pMyOverlapped->hEvent = pKernelEvent->Handle;
    DWORD fileType = GetFileType(FileHandle);
    if (fileType == FILE_TYPE_DISK) {
        // Se l'offset logico non è ancora stato impostato da FlushOutput, lo prendiamo dal file pointer corrente
        if (pMyOverlapped->Offset == 0 && pMyOverlapped->OffsetHigh == 0) {
            LARGE_INTEGER currentFilePointer = { 0 };
            currentFilePointer.LowPart = SetFilePointer(FileHandle, 0, &currentFilePointer.HighPart, FILE_CURRENT);
            pMyOverlapped->Offset = currentFilePointer.LowPart;
            pMyOverlapped->OffsetHigh = currentFilePointer.HighPart;
        }
        pMyOverlapped->Internal = 0;
        pMyOverlapped->InternalHigh = 0;
    }
    // Allocazione di sbarramento per il contesto asincrono di Win32
    // OVERLAPPED overlappedContext;
    // RtlZeroMemory(&overlappedContext, sizeof(OVERLAPPED));

    // // Se lo stream ha configurato un evento di notifica per la Put Area, lo leghiamo all'OVERLAPPED
    // if (pKernelEvent != nullptr && pKernelEvent->Handle != NULL) {
    //     overlappedContext.hEvent = pKernelEvent->Handle; //
    // }
    // DWORD fileType = GetFileType(FileHandle);
    DWORD bytesWritten = 0;

    // ORA FUNZIONA: Passando overlappedContext, WriteFile accetta l'handle FILE_FLAG_OVERLAPPED
    // e non restituisce più FALSE a tradimento!
    BOOL success = WriteFile(FileHandle, Buffer, Length, &bytesWritten, pMyOverlapped); //

    if (success) {
        // Caso Fast I/O sincrono fulmineo (I byte sono già nella cache di pagina)
        IoStatusBlock->Information = bytesWritten; //
        IoStatusBlock->Status = STATUS_SUCCESS; //
        // AVANZAMENTO ATOMICO MANUALE SOLO PER FILE REGOLARI SU DISCO A RING 3
        if (fileType == FILE_TYPE_DISK && bytesWritten > 0) {
            ULONG64 currentOffset = (static_cast<ULONG64>(pMyOverlapped->OffsetHigh) << 32) | pMyOverlapped->Offset;
            currentOffset += bytesWritten;

            pMyOverlapped->Offset = static_cast<DWORD>(currentOffset & 0xFFFFFFFF);
            pMyOverlapped->OffsetHigh = static_cast<DWORD>((currentOffset >> 32) & 0xFFFFFFFF);

            // Riallineiamo anche il cursore logico di Win32 per consistenza
            LARGE_INTEGER moveOffset;
            moveOffset.QuadPart = bytesWritten;
            SetFilePointer(FileHandle, moveOffset.LowPart, &moveOffset.HighPart, FILE_CURRENT);
        }
        return STATUS_SUCCESS;
    }

    DWORD lastError = GetLastError();

    // Se l'operazione di scrittura viene presa in carico in background dall'I/O Manager
    if (lastError == ERROR_IO_PENDING) {
        // Forza il completamento inline immediato in scrittura (Fast I/O o Blocco sincrono per i file)
        DWORD bytesTransferred = 0;
        if (GetOverlappedResult(FileHandle, pMyOverlapped, &bytesTransferred, TRUE)) {
            IoStatusBlock->Status = STATUS_PENDING;
            IoStatusBlock->Information = bytesTransferred; //0;

            // =========================================================================
            // CORREZIONE ASINCRONA MANCANTE CRITICA: AVANZAMENTO OFFSET POST-PENDING
            // =========================================================================
            if (fileType == FILE_TYPE_DISK && bytesTransferred > 0) {
                ULONG64 currentOffset = (static_cast<ULONG64>(pMyOverlapped->OffsetHigh) << 32) | pMyOverlapped->Offset;
                currentOffset += bytesTransferred; // Incrementiamo la sbarra reale cumulativa!

                pMyOverlapped->Offset = static_cast<DWORD>(currentOffset & 0xFFFFFFFF);
                pMyOverlapped->OffsetHigh = static_cast<DWORD>((currentOffset >> 32) & 0xFFFFFFFF);

                LARGE_INTEGER moveOffset; moveOffset.QuadPart = bytesTransferred;
                SetFilePointer(FileHandle, moveOffset.LowPart, &moveOffset.HighPart, FILE_CURRENT);
            }

            // Restituiamo STATUS_SUCCESS perché GetOverlappedResult(..., TRUE) ha già chiuso la transazione!
            return STATUS_SUCCESS;
            // RISOLUTIVO: Ritorniamo STATUS_PENDING immediatamente!
            // Eliminando la GetOverlappedResult(..., TRUE) che veniva eseguita qui dentro,
            // impediamo a Windows NT di troncare orfano il file ad ogni scrittura successiva!
            // return STATUS_PENDING;
            // IoStatusBlock->Information = bytesTransferred;
            // IoStatusBlock->Status = STATUS_SUCCESS;
            //
            // if (fileType == FILE_TYPE_DISK && bytesTransferred > 0) {
            //     ULONG64 currentOffset = (static_cast<ULONG64>(pMyOverlapped->OffsetHigh) << 32) | pMyOverlapped->Offset;
            //     currentOffset += bytesTransferred;
            //
            //     pMyOverlapped->Offset = static_cast<DWORD>(currentOffset & 0xFFFFFFFF);
            //     pMyOverlapped->OffsetHigh = static_cast<DWORD>((currentOffset >> 32) & 0xFFFFFFFF);
            //
            //     LARGE_INTEGER moveOffset; moveOffset.QuadPart = bytesTransferred;
            //     SetFilePointer(FileHandle, moveOffset.LowPart, &moveOffset.HighPart, FILE_CURRENT);
            // }
            // return STATUS_SUCCESS;
        }
        // IoStatusBlock->Information = 0; //
        // IoStatusBlock->Status = STATUS_PENDING; //
        // return STATUS_PENDING; // Mappatura nativa e corretta di Ring 0!
    }

    IoStatusBlock->Information = 0; //
    IoStatusBlock->Status = STATUS_UNSUCCESSFUL; //
    return STATUS_UNSUCCESSFUL; //
#endif
    }

    inline void KeDelayExecutionThread(char WaitMode, BOOLEAN Alertable, LARGE_INTEGER* Interval) {
        UNREFERENCED_PARAMETER(WaitMode);
        UNREFERENCED_PARAMETER(Alertable);
        if (Interval == NULL) return;

        // A Ring 0 l'intervallo è espresso in valori negativi da 100 nanosec.
        // Convertiamo in millisecondi positivi per la Sleep Win32: ms = Ticks / -10000
        LONGLONG ms = Interval->QuadPart / -10000;
        if (ms <= 0) ms = 1;
        Sleep((DWORD)ms);
    }


inline NTSTATUS ZwClose(HANDLE Handle) {
    if (Handle == NULL || Handle == INVALID_HANDLE_VALUE) {
        return STATUS_INVALID_PARAMETER;
    }
    return CloseHandle(Handle) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

#ifndef KernelMode
    #define KernelMode 0
#endif

#ifndef Executive
    #define Executive 0
#endif

// Mappatura trasparente di KeMemoryBarrier sulla barriera hardware nativa di MSVC Ring 3
// Forza la CPU a completare tutti i transiti di cache (Read/Write) prima di avanzare.
#ifndef KeMemoryBarrier
#define KeMemoryBarrier()                   MemoryBarrier()
#endif

inline
NTSTATUS ConvertAnsiToWideStatic(const char* ansiSrc, WCHAR* wideDest, size_t destCch) {
    if (ansiSrc == nullptr || wideDest == nullptr || destCch == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    // Il flag %S (maiuscolo) esegue la traslazione automatica da char* a WCHAR*
    // all'interno del buffer statico dello stack, a costo zero a runtime.
    return RtlStringCchPrintfW(wideDest, destCch, L"%S", ansiSrc);
}



inline void KeInitializeEvent(PKEVENT Event, EVENT_TYPE Type, BOOLEAN State) {
    UNREFERENCED_PARAMETER(Type);
    if (Event) {
        // Crea un evento Win32 manual-reset o auto-reset anonimo
        /*Event->Handle = CreateEventW(NULL, (Type == NotificationEvent) ? TRUE : FALSE, State, NULL);
*/

        Event->Handle = CreateEventW(NULL, (Type == NotificationEvent) ? TRUE : FALSE, State, NULL);
        RtlZeroMemory(&Event->Overlapped, sizeof(OVERLAPPED));
        Event->Overlapped.hEvent = Event->Handle; // Sincronizzazione automatica nativa
    }
}

// Risolutivo per KeResetEvent richiesto: Abbassa la sbarra di sincronizzazione Win32
inline void KeResetEvent(PKEVENT Event) {
    if (Event && Event->Handle) {
        ResetEvent(Event->Handle);
        // Preserviamo l'offset ma azzeriamo lo stato transizionale interno dell'I/O Manager
        // Event->Overlapped.Internal = 0;
        // Event->Overlapped.InternalHigh = 0;
    }
}

// Risolutivo per KeQueryPerformanceCounter: Interroga i tick orari della CPU ad alta precisione
inline LARGE_INTEGER KeQueryPerformanceCounter(PLARGE_INTEGER PerformanceFrequency) {
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    if (PerformanceFrequency != NULL) {
        QueryPerformanceFrequency(PerformanceFrequency);
    }
    return count;
}


// Risolutivo per KeSetSystemGroupAffinityThread: Sega l'I/O ed isola il thread sul core logico
inline void KeSetSystemGroupAffinityThread(PGROUP_AFFINITY Affinity, PGROUP_AFFINITY PreviousAffinity) {
    if (Affinity != nullptr) {
        HANDLE hCurrentThread = GetCurrentThread();
        DWORD_PTR oldMask = SetThreadAffinityMask(hCurrentThread, static_cast<DWORD_PTR>(Affinity->Mask));
        if (PreviousAffinity != nullptr) {
            PreviousAffinity->Mask = static_cast<KAFFINITY>(oldMask);
            PreviousAffinity->Group = 0;
        }
    }
}


// Risolutivo per KeRevertToUserGroupAffinityThread: Ripristina il gruppo originario
inline void KeRevertToUserGroupAffinityThread(PGROUP_AFFINITY PreviousAffinity) {
    if (PreviousAffinity != nullptr) {
        SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(PreviousAffinity->Mask));
    }
}

/**
 * Mappatura speculare di KeQueryActiveProcessorCountEx per lo spazio utente (Ring 3)
 */
inline ULONG KeQueryActiveProcessorCountEx(USHORT GroupNumber) {
    // Se il modulo richiede ALL_PROCESSOR_GROUPS (0xFFFF), GetActiveProcessorCount
    // accetta lo stesso identico valore per restituire il totale globale dei core logici del sistema.
    return GetActiveProcessorCount(GroupNumber);
}

inline NTSTATUS KeWaitForSingleObject(PVOID Object, char WaitReason, char WaitMode, BOOLEAN Alertable, LARGE_INTEGER* Timeout) {
    UNREFERENCED_PARAMETER(WaitReason);
    UNREFERENCED_PARAMETER(WaitMode);
    UNREFERENCED_PARAMETER(Alertable);

    PKEVENT pEvent = reinterpret_cast<PKEVENT>(Object);
    if (!pEvent || !pEvent->Handle) return STATUS_INVALID_PARAMETER;

    DWORD dwTimeoutMs = INFINITE;
    if (Timeout != NULL) {
        // A Ring 0 il timeout è relativo, negativo ed espresso in unità da 100ns
        // Convertiamo in millisecondi positivi per Win32: ms = Ticks / -10000
        LONGLONG ticks = Timeout->QuadPart;
        if (ticks < 0) {
            dwTimeoutMs = static_cast<DWORD>(ticks / -10000);
        }
    }

    DWORD waitResult = WaitForSingleObject(pEvent->Handle, dwTimeoutMs);
    if (waitResult == WAIT_TIMEOUT) return STATUS_TIMEOUT;
    if (waitResult == WAIT_OBJECT_0) return STATUS_SUCCESS;
    return STATUS_UNSUCCESSFUL;
}

/**
     * Mappatura unificata di ZwFlushBuffersFile per lo spazio utente (Ring 3)
     * Costringe il sistema operativo a scaricare i blocchi di cache sul silicio del disco
     */
inline NTSTATUS ZwFlushBuffersFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock) {
    if (FileHandle == NULL || FileHandle == INVALID_HANDLE_VALUE || IoStatusBlock == NULL) {
        return STATUS_INVALID_PARAMETER; //
    }

    // Forza Windows NT ad evacuare la cache logica sui piatti fisici dell'SSD
    BOOL success = FlushFileBuffers(FileHandle); //

    if (success) {
        IoStatusBlock->Status = STATUS_SUCCESS;
        IoStatusBlock->Information = 0;
        return STATUS_SUCCESS; //
    } else {
        IoStatusBlock->Status = STATUS_UNSUCCESSFUL;
        return STATUS_UNSUCCESSFUL; //
    }
}

    // =========================================================================
    // ESTENSIONI LAYER UTENTE: MAPPATURA STRUTTURE E ROUTINE TEMPORALI (RING 3)
    // =========================================================================
    // --- PORTING DI CSHORT IN USER MODE ---
    typedef short CSHORT;
    typedef short *PCSHORT;
    // 1. Definizione della struttura TIME_FIELDS speculare al WDK di Ring 0
    typedef struct _TIME_FIELDS {
        CSHORT Year;          // Intervallo: [1601...]
        CSHORT Month;         // Intervallo: [1...12]
        CSHORT Day;           // Intervallo: [1...31]
        CSHORT Hour;          // Intervallo: [0...23]
        CSHORT Minute;        // Intervallo: [0...59]
        CSHORT Second;        // Intervallo: [0...59]
        CSHORT Milliseconds;  // Intervallo: [0...999]
        CSHORT Weekday;       // Intervallo: [0...6] (0 = Domenica)
    } TIME_FIELDS, *PTIME_FIELDS;

    /**
     * Mappatura unificata di RtlTimeToTimeFields per lo spazio utente (Ring 3).
     * Scompone il tempo a 64 bit del kernel (100ns da epoca 1601) in record orari discreti.
     */
    inline VOID RtlTimeToTimeFields(
        PLARGE_INTEGER Time,
        PTIME_FIELDS TimeFields
    ) {
        if (Time == NULL || TimeFields == NULL) return;

        FILETIME fileTime;
        // Estrarre i registri hardware a 64 bit della coordinata oraria
        fileTime.dwLowDateTime = Time->LowPart;
        fileTime.dwHighDateTime = static_cast<DWORD>(Time->HighPart);

        SYSTEMTIME systemTime;
        // La system call Win32 converte l'epoca 1601 NT nativa in record in RAM
        if (FileTimeToSystemTime(&fileTime, &systemTime)) {
            TimeFields->Year         = static_cast<CSHORT>(systemTime.wYear);
            TimeFields->Month        = static_cast<CSHORT>(systemTime.wMonth);
            TimeFields->Day          = static_cast<CSHORT>(systemTime.wDay);
            TimeFields->Hour         = static_cast<CSHORT>(systemTime.wHour);
            TimeFields->Minute       = static_cast<CSHORT>(systemTime.wMinute);
            TimeFields->Second       = static_cast<CSHORT>(systemTime.wSecond);
            TimeFields->Milliseconds = static_cast<CSHORT>(systemTime.wMilliseconds);
            TimeFields->Weekday      = static_cast<CSHORT>(systemTime.wDayOfWeek);
        } else {
            // Fallback di azzeramento di sicurezza se la coordinata è corrotta
            RtlZeroMemory(TimeFields, sizeof(TIME_FIELDS));
        }
    }
    // NTSTRSAFE_PSTR è un puntatore a una stringa ANSI (char*)
    typedef PSTR  NTSTRSAFE_PSTR;
    typedef PCSTR NTSTRSAFE_PCSTR; // Versione const (se ti dovesse servire)
    /**
     * Mappatura unificata di RtlStringCchPrintfA (Ansi) per lo spazio utente (Ring 3).
     * Sfrutta l'orchestrazione sicura ad allocazione statica di strsafe.h
     */
    // inline NTSTATUS RtlStringCchPrintfA(
    //     NTSTRSAFE_PSTR pszDest,
    //     size_t cchDest,
    //     NTSTRSAFE_PCSTR pszFormat,
    //     ...
    // ) {
    //     if (pszDest == NULL || cchDest == 0 || pszFormat == NULL) {
    //         return STATUS_INVALID_PARAMETER;
    //     }
    //
    //     va_list args;
    //     va_start(args, pszFormat);
    //
    //     // Invocazione della variante Win32 sicura basata su argomenti variabili (va_list)
    //     HRESULT hr = StringCchVPrintfA(pszDest, cchDest, pszFormat, args);
    //
    //     va_end(args);
    //
    //     // Mappatura geometrica del risultato per conformità NTSTATUS
    //     if (SUCCEEDED(hr)) {
    //         return STATUS_SUCCESS;
    //     } else if (hr == STRSAFE_E_INSUFFICIENT_BUFFER) {
    //         return STATUS_BUFFER_OVERFLOW;
    //     }
    //
    //     return STATUS_UNSUCCESSFUL;
    // }


// Abilitiamo il comando di annullamento dell'I/O asincrono per lo spazio utente
#define ZwCancelIoFile(FileHandle, IoStatusBlock) \
(CancelIoEx((FileHandle), NULL) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL)

#include <string>

// 1. Emulazione di KeQuerySystemTimePrecise per Ring 3
// Restituisce il tempo UTC corrente in intervalli da 100 nanosecondi dal 1° gennaio 1601.
// inline VOID KeQuerySystemTimePrecise(PLARGE_INTEGER CurrentTime) {
//         if (!CurrentTime) return;
//         FILETIME ft;
//         GetSystemTimePreciseAsFileTime(&ft);
//
//         CurrentTime->LowPart = ft.dwLowDateTime;
//         CurrentTime->HighPart = ft.dwHighDateTime;
//     }
// Istruiamo il linker a cercare il simbolo nelle librerie OneCore / MinCore di Windows.
// Questo risolve l'errore LNK2019 / simbolo non risolto senza fare GetProcAddress.
#pragma comment(lib, "OneCore.lib")
// 2. Emulazione di KeQueryInterruptTime per Ring 3
// Restituisce il tempo trascorso dal boot (inclusi i periodi di sleep/sospensione se stabili)
// espresso in unità da 100 nanosecondi. Mappato sulla Win32 QueryInterruptTime.
// Emulazione nativa di KeQueryInterruptTime in Ring 3 senza indirizzi fissi
inline ULONG64 KeQueryInterruptTime() {
    ULONGLONG interruptTimeNs100 = 0;

    // QueryInterruptTimePrecise fornisce la massima accuratezza a livello hardware.
    // È supportata nativamente a partire da Windows 10 in kernel32.dll.
    QueryInterruptTimePrecise(&interruptTimeNs100);

    return interruptTimeNs100;
}

// Emulazione di KeQuerySystemTimePrecise per Ring 3 tramite API standard
inline VOID KeQuerySystemTimePrecise(PLARGE_INTEGER CurrentTime) {
    if (!CurrentTime) return;

    FILETIME ft;
    // API ufficiale ad alta precisione disponibile da Windows 8 in poi
    GetSystemTimePreciseAsFileTime(&ft);

    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    CurrentTime->QuadPart = uli.QuadPart;
}

// --- EMULAZIONE DI ExSystemTimeToLocalTime PER RING 3 ---
inline VOID ExSystemTimeToLocalTime(
    _In_  PLARGE_INTEGER SystemTime,
    _Out_ PLARGE_INTEGER LocalTime
) {
    if (!SystemTime || !LocalTime) return;

    // Mappiamo i LARGE_INTEGER sulle strutture FILETIME di Win32
    FILETIME ftSystem;
    ftSystem.dwLowDateTime = SystemTime->LowPart;
    ftSystem.dwHighDateTime = SystemTime->HighPart;

    FILETIME ftLocal;
    // Effettua la conversione matematica sottraendo il fuso orario corrente
    if (FileTimeToLocalFileTime(&ftSystem, &ftLocal)) {
        LocalTime->LowPart = ftLocal.dwLowDateTime;
        LocalTime->HighPart = ftLocal.dwHighDateTime;
    } else {
        // Fallback difensivo in caso di errore Win32: copia il tempo inalterato
        LocalTime->QuadPart = SystemTime->QuadPart;
    }
}

#ifdef __cplusplus
extern "C" {
#endif


#ifndef ALL_PROCESSOR_GROUPS
#define ALL_PROCESSOR_GROUPS 0xFFFF
#endif




#ifdef __cplusplus
}
#endif

#endif

static __inline LONG KernelAtomicRead(volatile LONG* Target) {
    return InterlockedOr(const_cast<LONG*>(Target), 0);
}

/**
 * Converte un codice NTSTATUS nella sua stringa letterale corrispondente.
 * Ritorna un puntatore statico a caratteri costante (const char*) esente da heap allocation.
 */
inline const char* NtStatusToString(NTSTATUS status) {
    switch (status) {
        // --- CODICI DI SUCCESSO E STATO INTERMEDIO ---
        case STATUS_SUCCESS:                return "STATUS_SUCCESS (0x00000000)";
        case STATUS_PENDING:                return "STATUS_PENDING (0x00000103)";
        case STATUS_TIMEOUT:                return "STATUS_TIMEOUT (0x00000102)";

        // --- CODICI DI ERRORE GENERICI ---
        case STATUS_UNSUCCESSFUL:           return "STATUS_UNSUCCESSFUL (0xC0000001)";
        case STATUS_INVALID_PARAMETER:      return "STATUS_INVALID_PARAMETER (0xC000000D)";
        case STATUS_INSUFFICIENT_RESOURCES: return "STATUS_INSUFFICIENT_RESOURCES (0xC000009A)";
        case STATUS_INVALID_DEVICE_STATE:   return "STATUS_INVALID_DEVICE_STATE (0xC0000184)";

        // --- ERRORI DI MEMORIA, ACCESSO E OGGETTI NATIVI NT ---
        case STATUS_ACCESS_VIOLATION:       return "STATUS_ACCESS_VIOLATION (0xC0000005)";
        case STATUS_ACCESS_DENIED:          return "STATUS_ACCESS_DENIED (0xC0000022)";
        case STATUS_OBJECT_NAME_NOT_FOUND:  return "STATUS_OBJECT_NAME_NOT_FOUND (0xC0000034)";
        case STATUS_OBJECT_PATH_NOT_FOUND:  return "STATUS_OBJECT_PATH_NOT_FOUND (0xC000003A)";
        case STATUS_SHARING_VIOLATION:      return "STATUS_SHARING_VIOLATION (0xC0000043)";
        case STATUS_INVALID_HANDLE:         return "STATUS_INVALID_HANDLE (0xC0000008)";
        case STATUS_END_OF_FILE:            return "STATUS_END_OF_FILE (0xC0000011)";
        case STATUS_CANCELLED:              return "STATUS_CANCELLED (0xC0000120)";
        case STATUS_DEVICE_NOT_READY:       return "STATUS_DEVICE_NOT_READY (0xC000009D)";
        case STATUS_BUFFER_OVERFLOW:        return "STATUS_BUFFER_OVERFLOW (0x40000006)";
        case STATUS_BUFFER_TOO_SMALL:       return "STATUS_BUFFER_TOO_SMALL (0xC0000023)";

        // --- FALLBACK PER CODICI VARIABILI NON CENSITI ---
        default:                            return "STATUS_UNKNOWN_NT_ERROR";
    }
}


#endif //MALAMORE_KERNEL_COMPATIBILITY_H
