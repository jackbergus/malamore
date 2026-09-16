//
// Created by Giacomo Bergami on 16/09/2026.
//

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <malamore/utility/kernel_compatibility.h>
#include <cstdint>

#include "malamore/structures/InterProcessChannelData.h"
#include "malamore/utility/Path.h"

TEST_CASE("InterProcessChannelData") {
    InterProcessChannelData buffer;
    RealTimeIPC_Init(&buffer);
    REQUIRE(RealTimeIPC_IsEmpty(&buffer));
    static const UCHAR* element = reinterpret_cast<const UCHAR *>("hello, world");
    static size_t strl = strlen(reinterpret_cast<const char *>(element));
    REQUIRE(RealTimeIPC_Push(&buffer, 0, 1, element, strl));
    REQUIRE_FALSE(RealTimeIPC_IsEmpty(&buffer));
    auto obj = RealTimeIPC_Top(&buffer);
    REQUIRE_FALSE(obj == nullptr);
    REQUIRE(obj->DataLength == strl);
    REQUIRE(obj->SourceNodeId == 1);
    REQUIRE(obj->Timestamp == 0);
    REQUIRE(strcmp((const char*)obj->PayloadData, (const char*)element) == 0);
    REQUIRE(RealTimeIPC_Pop(&buffer));
    REQUIRE(RealTimeIPC_IsEmpty(&buffer));
}

TEST_CASE("FileSystemPath [case]") {
    {
        WCHAR kernelBuffer[MAX_PATH];
        // =========================================================================
        // CASO 1: I PERCORSI PERIFERICA (\\\\.\\FSCC0) VENCONO PROTETTI DA CORRUZIONE
        // =========================================================================
        MalamorePath devicePath(L"\\\\.\\FSCC0");

        // L'operatore / capisce che siamo su un hardware e compie un append lineare sul canale!
        MalamorePath subChannel = devicePath / L"0"; // Diventa L"\\\\.\\FSCC00" (Nessuna barra introdotta!)

        MalamorePath fName = subChannel.filename(); // Ritorna l'intera stringa hardware protetta, senza mutilazioni.
        REQUIRE(strcmp((const char*)fName.wstring(), (const char*)L"\\\\.\\FSCC0") == 0);

        subChannel.ToKernelPath(kernelBuffer, MAX_PATH);
        // Risultato finale per ZwCreateFile: L"\\DosDevices\\FSCC00" -> PERFETTO ED OPERANTE!
        REQUIRE(strcmp((const char*)kernelBuffer, (const char*)L"\\DosDevices\\FSCC00") == 0);

        // =========================================================================
        // CASO 2: I FILE DEL FILESYSTEM ORDINARI CONTINUANO A COMPORTARSI DA MANUALE
        // =========================================================================
        MalamorePath fileSystemPath(L"C:\\Windows");
        MalamorePath finalLog = fileSystemPath / L"System32" / L"Log.txt";
        finalLog.ToWin32Path(kernelBuffer, MAX_PATH);
        REQUIRE(strcmp((const char*)kernelBuffer, (const char*)L"C:\\Windows\\System32\\Log.txt") == 0);
    }

    {
        WCHAR kernelBuffer[MAX_PATH];

        // 1. Costruzione fluida tramite operatore / (Esente da heap allocation!)
        MalamorePath baseFolder(L"C:\\Users\\gbergami");
        MalamorePath subFolder(L"Documents/MalamoreProject");
        MalamorePath file(L"Sink_Log.bin");

        // Combinazione a cascata tramite l'operatore / sovraccaricato
        MalamorePath fullPath = baseFolder / subFolder / file;

        // 2. Estrazione delle componenti semantiche
        MalamorePath nomeFile = fullPath.filename();     // Restituisce "Sink_Log.bin"
        REQUIRE(strcmp((const char*)nomeFile.wstring(), (const char*)L"Sink_Log.bin") == 0);
        MalamorePath cartella  = fullPath.parent_path();  // Restituisce "C:\Users\gbergami\Documents\MalamoreProject"
        REQUIRE(strcmp((const char*)cartella.wstring(), (const char*)L"C:\\Users\\gbergami\\Documents\\MalamoreProject") == 0);

        // 3. Estrazione dei buffer finali sanificati per le ZwCreateFile o Win32 CreateFile

        fullPath.ToKernelPath(kernelBuffer, MAX_PATH);
        // Risultato in kernelBuffer: L"\\??\\C:\\Users\\gbergami\\Documents\\MalamoreProject\\Sink_Log.bin"
        REQUIRE(strcmp((const char*)kernelBuffer, (const char*)L"\\??\\C:\\Users\\gbergami\\Documents\\MalamoreProject\\Sink_Log.bin") == 0);
    }

    {
        // Testing telemetry strings
        MalamorePath pathLog(L"\\SystemRoot\\Malamore_Telemetry_Sink.bin");
        WCHAR targetBuffer[MAX_PATH];

        // =========================================================================
        // SE ESEGUITO NEL DRIVER (RING 0):
        // =========================================================================
        pathLog.ToKernelPath(targetBuffer, MAX_PATH);
        // targetBuffer conterrà: L"\\SystemRoot\\Malamore_Telemetry_Sink.bin"
        // -> ZwCreateFile aprirà con successo il file nel cuore di Windows.
        REQUIRE(strcmp((const char*)targetBuffer, (const char*)L"\\SystemRoot\\Malamore_Telemetry_Sink.bin") == 0);

        // =========================================================================
        // SE ESEGUITO NEL COMPILATORE DI TEST (RING 3):
        // =========================================================================
        pathLog.ToWin32Path(targetBuffer, MAX_PATH);
        // targetBuffer conterrà: L"C:\\Windows\\Malamore_Telemetry_Sink.bin"
        // -> La CreateFileW simulata troverà la cartella fisica reale su C: ed eseguirà i log.
        REQUIRE(strcmp((const char*)targetBuffer, (const char*)L"C:\\Windows\\Malamore_Telemetry_Sink.bin") == 0);
    }
}