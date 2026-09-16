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

#ifndef STATUS_SUCCESS
    #define STATUS_SUCCESS               ((NTSTATUS)0x00000000L)
#endif

#ifndef STATUS_UNSUCCESSFUL
    #define STATUS_UNSUCCESSFUL          ((NTSTATUS)0xC0000001L)
#endif

#ifndef STATUS_INVALID_PARAMETER
    #define STATUS_INVALID_PARAMETER     ((NTSTATUS)0xC000000DL)
#endif

#ifndef STATUS_INSUFFICIENT_RESOURCES
    #define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xC000009AL)
#endif

#ifndef STATUS_PENDING
    #define STATUS_PENDING                ((NTSTATUS)0x00000103L)
#endif

#ifndef STATUS_TIMEOUT
    #define STATUS_TIMEOUT                ((NTSTATUS)0x00000102L)
#endif

#ifndef OBJ_CASE_INSENSITIVE
    #define OBJ_CASE_INSENSITIVE          0x00000040L
#endif

#ifndef OBJ_KERNEL_HANDLE
    #define OBJ_KERNEL_HANDLE             0x00000200L
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

        if (FileHandle == NULL || ObjectAttributes == NULL || ObjectAttributes->ObjectName == NULL || IoStatusBlock == NULL) {
            return STATUS_INVALID_PARAMETER;
        }

        // Converte la stringa di Ring 0 in un percorso Win32 comprensibile
        PCWSTR win32Path = ParseKernelPathToWin32(ObjectAttributes->ObjectName->Buffer);

        // Mappatura grossolana del CreateDisposition del Kernel su Win32
        DWORD creationDisposition = OPEN_ALWAYS; // Equivalente di sicurezza generico
        if (CreateDisposition == 0x00000001) creationDisposition = OPEN_EXISTING; // FILE_OPEN
        else if (CreateDisposition == 0x00000002) creationDisposition = CREATE_ALWAYS; // FILE_CREATE / SUPERSEDE

        HANDLE hFile = CreateFileW(
            win32Path,
            DesiredAccess,
            ShareAccess,
            NULL,
            creationDisposition,
            FileAttributes,
            NULL
        );

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
        PLARGE_INTEGER ByteOffset,
        PULONG Key
    ) {
        UNREFERENCED_PARAMETER(Event);
        UNREFERENCED_PARAMETER(ApcRoutine);
        UNREFERENCED_PARAMETER(ApcContext);
        UNREFERENCED_PARAMETER(ByteOffset);
        UNREFERENCED_PARAMETER(Key);

        if (FileHandle == NULL || FileHandle == INVALID_HANDLE_VALUE || IoStatusBlock == NULL || Buffer == NULL) {
            return STATUS_INVALID_PARAMETER;
        }

        DWORD bytesRead = 0;
        // Simulazione della lettura asincrona non bloccante.
        // Nota: se desideri emulare STATUS_PENDING nei test, puoi forzarlo condizionalmente qui.
        BOOL success = ReadFile(FileHandle, Buffer, Length, &bytesRead, NULL);

        IoStatusBlock->Information = bytesRead;
        if (success) {
            IoStatusBlock->Status = STATUS_SUCCESS;
            return STATUS_SUCCESS;
        } else {
            IoStatusBlock->Status = STATUS_UNSUCCESSFUL;
            return STATUS_UNSUCCESSFUL;
        }
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

#endif


#endif //MALAMORE_KERNEL_COMPATIBILITY_H