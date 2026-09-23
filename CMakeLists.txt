cmake_minimum_required(VERSION 4.0)
project(malamore C CXX)

# =========================================================================
# OPZIONI DI CONFIGURAZIONE NELL'INTERFACCIA DI CLION
# =========================================================================

#LINK_KERNEL (Ring 0): Significa che il codice viene compilato come un Driver di periferica nativo (.sys).
#Viene caricato nello spazio di memoria protetto del Kernel di Windows, non ha accesso a nessuna DLL utente
#e viene compilato usando il Windows Driver Kit (WDK).
option(LINK_KERNEL "Compila in modalita Driver Ring 0 (WDK)" OFF)

#LINK_UWP_IOT (Ring 3 App Container): Significa che il codice viene compilato come un'Applicazione utente per
#la piattaforma universale Windows (UWP) per girare su Windows IoT Core. Questa modalità esegue dentro una sandbox
#protetta (App Container) a livello utente, dialoga con il runtime WinRT e viene compilata usando il Windows
#SDK standard con le estensioni C++/CX (/ZW).
option(LINK_UWP_IOT "Compila in modalita App UWP / Windows IoT Core" ON)

# Controllo di coerenza hardware
if(LINK_KERNEL AND LINK_UWP_IOT)
    ## Non posso trovarmi sia in modalità Kernel, sia in modalità IoT.
    message(FATAL_ERROR "[MALAMORE] Impossibile abilitare contemporaneamente modalita Kernel e UWP!")
endif()


# =========================================================================
# CONFIGURAZIONE STRATO 1: DRIVER KERNEL (RING 0)
# =========================================================================
if(LINK_KERNEL)
    add_compile_definitions(LINK_KERNEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_VERBOSE_MAKEFILE ON)
    message(STATUS "[MALAMORE 🔥] Configurazione Build: DRIVER NATIVO (Ring 0)")
    # [Qui inserirai le macro del WDK precedentemente consolidate]

    # =========================================================================
    # CONFIGURAZIONE STRATO 2: APP UWP / WINDOWS IOT CORE (COMPILATORE C++/CX)
    # =========================================================================
elseif(LINK_UWP_IOT)
##    add_compile_definitions(WINAPI_FAMILY=WINAPI_FAMILY_APP)
    add_compile_definitions(LINK_UWP_IOT)
    set(CMAKE_CXX_STANDARD 20)
#    message(STATUS "[MALAMORE 📱] Configurazione Build: UWP / WINDOWS IOT CORE (C++/CX)")
#    set(CMAKE_CXX_STANDARD 17)
#
#    if(MSVC)
#        # SOLUZIONE 1 (CONSERVATIVA): Rimuoviamo /ZW:nostdlib.
#        # Questo permette a MSVC di caricare in autonomia platform.winmd ed i tipi base.
#        add_compile_options(
#                /ZW
#                /EHsc-
#                /EHa
#        )
#
#        # Manteniamo le inclusioni dei metadati dell'SDK per le interfacce superiori come IAsyncAction
#        string(APPEND CMAKE_CXX_FLAGS " /AI\"C:/Program Files (x86)/Windows Kits/10/UnionMetadata\"")
#        string(APPEND CMAKE_CXX_FLAGS " /AI\"C:/Program Files (x86)/Windows Kits/10/UnionMetadata/10.0.22621.0\"")
#
#        add_link_options(/APPCONTAINER)
#    else()
#        message(FATAL_ERROR "[MALAMORE] Il supporto C++/CX (UWP) richiede tassativamente il compilatore MSVC!")
#    endif()
#
#    # =========================================================================
#    # CONFIGURAZIONE STRATO 3: USER-MODE DESKTOP TRADIZIONALE (RING 3)
#    # =========================================================================
else()
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_VERBOSE_MAKEFILE ON)
    message(STATUS "[MALAMORE 💻] Configurazione Build: USER-MODE DESKTOP (Win32)")
    # Compilazione standard per i test sequenziali di telemetria
endif()

include_directories(include)
add_subdirectory(submodules/Catch2)
add_subdirectory(submodules/ftxui)
include_directories(submodules/json/include)
include_directories(submodules/ftxui/include)
include_directories(submodules/tqdmcpp2)

# ==============================================================================
# CONFIGURAZIONI DEL DRIVER (Modifica qui i tuoi parametri)
# ==============================================================================
set(DRIVER_NAME "KernelBinder")
set(MANUFACTURER "MalamoreDev")
set(VER_MAJOR "1")
set(VER_MINOR "0")
set(VER_PATCH "0")
set(VER_TWEAK "0")

# Hash SHA1 del certificato di firma (Test/Self-Signed o Commerciale)
set(SIGN_THUMBPRINT "B4DDC9FC4975D4029D3EA0C8AF1387D69CEAF5F3")
# ==============================================================================
# CONFIGURAZIONI DEL DRIVER
# ==============================================================================
# Lascia VUOTO se vuoi che CMake generi e registri un nuovo certificato di test
#set(SIGN_THUMBPRINT "")

# Se l'utente non ha fornito un Thumbprint, lo generiamo dinamicamente via PowerShell
if(NOT SIGN_THUMBPRINT)
    message(STATUS "Nessun SIGN_THUMBPRINT fornito. Tentativo di generazione certificato di test...")

    # Esegue lo script PowerShell in fase di configurazione per creare/trovare il certificato
    execute_process(
            COMMAND powershell.exe -ExecutionPolicy Bypass -Command
            "$certName = '${MANUFACTURER}'; \
             $cert = Get-ChildItem Cert:\\LocalMachine\\My | Where-Object { $_.Subject -like \"*CN=$certName*\" } | Select-Object -First 1; \
             if (-not $cert) { \
                 $cert = New-SelfSignedCertificate -Subject \"CN=$certName\" -CertStoreLocation \"Cert:\\LocalMachine\\My\" -Type CodeSigningCert -KeyUsage DigitalSignature -KeyExportPolicy Exportable -KeyLength 2048 -Provider \"Microsoft Enhanced RSA and AES Cryptographic Provider\"; \
                 $rootStore = New-Object System.Security.Cryptography.X509Certificates.X509Store(\"Root\", \"LocalMachine\"); \
                 $rootStore.Open(\"ReadWrite\"); \
                 $rootStore.Add($cert); \
                 $rootStore.Close(); \
                 $pubStore = New-Object System.Security.Cryptography.X509Certificates.X509Store(\"TrustedPublisher\", \"LocalMachine\"); \
                 $pubStore.Open(\"ReadWrite\"); \
                 $pubStore.Add($cert); \
                 $pubStore.Close(); \
                 Write-Host \"GENERATED:$($cert.Thumbprint)\"; \
             } else { \
                 Write-Host \"FOUND:$($cert.Thumbprint)\"; \
             }"
            OUTPUT_VARIABLE POWERSHELL_OUTPUT
            ERROR_VARIABLE POWERSHELL_ERROR
            RESULT_VARIABLE POWERSHELL_RESULT
    )

    if(POWERSHELL_RESULT EQUAL 0 AND POWERSHELL_OUTPUT MATCHES "(GENERATED|FOUND):([A-FA-f0-9]+)")
        set(SIGN_THUMBPRINT "${CMAKE_MATCH_2}")
        message(STATUS "Certificato di test pronto! SHA1 Thumbprint: ${SIGN_THUMBPRINT}")
    else()
        message(FATAL_ERROR "Impossibile generare il certificato di test. Assicurati di eseguire CMake/Visual Studio come AMMINISTRATORE. Errore: ${POWERSHELL_ERROR}")
    endif()
endif()

# ==============================================================================

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/submodules/findwdk/cmake")
set(WDK_TARGET_VERSION "Win11" CACHE STRING "Target Windows Version")
find_package(WDK REQUIRED)
# Oppure "0x0A00" a seconda di come gestisci la variabile nel tuo script
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(Python_EXECUTABLE ${Python3_EXECUTABLE})
set(WDK_BIN "${WDK_ROOT}/bin/${WDK_VERSION}")
set(WDK_NTDDI_VERSION "0x0A00000C" CACHE STRING "NTDDI_VERSION")
message(STATUS "Python trovato in: ${Python_EXECUTABLE}")

set(EXPORT_DRIVER_NAME "driver_lettura")

#add_executable(client_malamore client_malamore.cpp
#        ReadTelemetryFile.cpp
#        )

# Aggiunta del driver (rimosso EXCLUDE_FROM_ALL per farlo compilare di default)
wdk_add_driver(${DRIVER_NAME}
        KMDF 1.15
        WINVER ${WDK_WINVER}
        src/malamore/driver/malamore_driver.cpp
        src/malamore/utility/streambuf/MalamoreIOStream.cpp
        src/malamore/utility/telemetry_formatter.cpp
        src/malamore/utility/Path.cpp
        src/malamore/utility/streambuf/KernelStreamBuf.cpp
        src/malamore/utility/streambuf/MalamoreIStream.cpp
        src/malamore/utility/streambuf/MalamoreOStream.cpp

        src/malamore/structures/InterProcessChannelData.cpp
        src/malamore/structures/MessageMap.cpp
        src/malamore/structures/TelemetryQueue.cpp

        src/malamore/threads/ThreadGuard.cpp
        src/malamore/threads/thread_pool.cpp

        src/malamore/wrapper_tasks/hardware_reader.cpp
        src/malamore/wrapper_tasks/telemetry_writer.cpp

        src/malamore/winapi_client/Win32NamedPipeGuard.cpp

        src/yaucl/data/csv.cpp
)
target_compile_definitions(${DRIVER_NAME} PRIVATE LINK_KERNEL NTDDI_VERSION=0x0A00000C _WIN32_MINNT=0x0A00)

# Percorso dello script Python che genera il file .inf
set(GENERATE_INF_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/generate_inf.py")
# Nota: Assicurati di salvare il codice Python ricevuto nel file 'generate_inf.py'
# modificando l'ultima riga del codice Python per salvare il file in CMAKE_CURRENT_BINARY_DIR.

# ==============================================================================
# POST-BUILD: Generazione INF, Stampinf, ApiValidator, Inf2Cat e SignTool
# ==============================================================================
# ==============================================================================
# POST-BUILD PIPELINE: Comandi sequenziali con commenti dedicati
# ==============================================================================
# ==============================================================================
# PIPELINE DI CERTIFICAZIONE E FIRMA (Suddivisa in Fasi di Build distinte)
# ==============================================================================

# Definiamo i percorsi dei file generati per creare la catena di dipendenze
set(INF_OUTPUT_FILE  "${CMAKE_CURRENT_BINARY_DIR}/${DRIVER_NAME}.inf")
set(STAMP_MARKER     "${CMAKE_CURRENT_BINARY_DIR}/${DRIVER_NAME}.stampinf.ok")
set(VALID_MARKER     "${CMAKE_CURRENT_BINARY_DIR}/${DRIVER_NAME}.apivalidator.ok")
set(CAT_OUTPUT_FILE  "${CMAKE_CURRENT_BINARY_DIR}/${DRIVER_NAME}.cat")
set(SIGN_MARKER      "${CMAKE_CURRENT_BINARY_DIR}/${DRIVER_NAME}.signtool.ok")

# ------------------------------------------------------------------------------
# FASE 1: Generazione File .inf (Dipende dalla compilazione del driver .sys)
# ------------------------------------------------------------------------------
add_custom_command(
        OUTPUT "${INF_OUTPUT_FILE}"
        COMMAND ${Python_EXECUTABLE} "${GENERATE_INF_SCRIPT}"
        --driver_name "${DRIVER_NAME}"
        --manufacturer "${MANUFACTURER}"
        --major "${VER_MAJOR}"
        --minor "${VER_MINOR}"
        --patch "${VER_PATCH}"
        --tweak "${VER_TWEAK}"
        DEPENDS ${DRIVER_NAME} # Riesegue se il driver viene ricompilato
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "[FASE 1/5] Esecuzione Script Python: Generazione file INF"
)

# ------------------------------------------------------------------------------
# FASE 2: Stampinf (Dipende dal file .inf generato al punto 1)
# ------------------------------------------------------------------------------
add_custom_command(
        OUTPUT "${STAMP_MARKER}"
        COMMAND "${WDK_BIN}/x86/stampinf.exe" -d "*" -a "amd64" -v "*" -k "1.15" -n -x -f "${DRIVER_NAME}.inf"
        COMMAND ${CMAKE_COMMAND} -E touch "${STAMP_MARKER}" # Crea un file marker se ha successo
        DEPENDS "${INF_OUTPUT_FILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "[FASE 2/5] WDK stampinf: Aggiornamento metadati nell'INF"
)

# ------------------------------------------------------------------------------
# FASE 3: ApiValidator (Dipende dal completamento dello Stampinf)
# ------------------------------------------------------------------------------
add_custom_command(
        OUTPUT "${VALID_MARKER}"
        COMMAND "${WDK_BIN}/x64/ApiValidator.exe"
        -DriverPackagePath:"${DRIVER_NAME}.sys"
        -SupportedApiXmlFiles:"${WDK_ROOT}/build/${WDK_VERSION}/universalDDIs/x64/UniversalDDIs.xml"
        -ModuleWhiteListXmlFiles:"${WDK_ROOT}/build/${WDK_VERSION}/universalDDIs/x64/ModuleWhiteList.xml"
        -ApiExtractorExePath:"${WDK_ROOT}/bin/${WDK_VERSION}/x64"
        COMMAND ${CMAKE_COMMAND} -E touch "${VALID_MARKER}" # Crea un file marker se ha successo
        DEPENDS "${STAMP_MARKER}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "[FASE 3/5] WDK ApiValidator: Validazione delle chiamate API del Driver"
)

# ------------------------------------------------------------------------------
# FASE 4: Inf2Cat (Dipende dalla validazione API avvenuta con successo)
# ------------------------------------------------------------------------------
add_custom_command(
        OUTPUT "${CAT_OUTPUT_FILE}"
        COMMAND "${WDK_BIN}/x86/inf2cat.exe" /os:10_x64 /VERBOSE /driver:"${CMAKE_CURRENT_BINARY_DIR}/"
        DEPENDS "${VALID_MARKER}"
        #WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "[FASE 4/5] WDK inf2cat: Creazione del file di catalogo (.cat)"
)

# ------------------------------------------------------------------------------
# FASE 5: SignTool (Dipende dal file .cat generato al punto 4)
# ------------------------------------------------------------------------------
add_custom_command(
        OUTPUT "${SIGN_MARKER}"
        COMMAND "${WDK_ROOT}/bin/${WDK_VERSION}/x64/signtool.exe"  sign  /sha1 "${SIGN_THUMBPRINT}"  /fd sha256 /v "${DRIVER_NAME}.sys"
        COMMAND "${WDK_ROOT}/bin/${WDK_VERSION}/x64/signtool.exe" sign  /sha1 "${SIGN_THUMBPRINT}" /fd sha256 /v "${DRIVER_NAME}.cat"
        COMMAND ${CMAKE_COMMAND} -E touch "${SIGN_MARKER}"
        DEPENDS "${CAT_OUTPUT_FILE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "[FASE 5/5] WDK SignTool: Firma digitale di .sys e .cat"
)

# ------------------------------------------------------------------------------
# TARGET ATTIVATORE: Forza CMake ad eseguire l'intera catena durante la build
# ------------------------------------------------------------------------------
add_custom_target(CertificaDriver ALL
        DEPENDS "${SIGN_MARKER}"
        COMMENT "Pipeline di certificazione completata con successo!"
)



### This library provides the support for enabling objects also at the user level, not necessarily at the kernel level
### This should help with the debugging facility.
add_library(user_level_struct_library OBJECT
        src/malamore/utility/streambuf/MalamoreIOStream.cpp
        src/malamore/utility/telemetry_formatter.cpp
        src/malamore/utility/Path.cpp
        src/malamore/utility/streambuf/KernelStreamBuf.cpp
        src/malamore/utility/streambuf/MalamoreIStream.cpp
        src/malamore/utility/streambuf/MalamoreOStream.cpp

        src/malamore/structures/InterProcessChannelData.cpp
        src/malamore/structures/MessageMap.cpp
        src/malamore/structures/TelemetryQueue.cpp

        src/malamore/threads/ThreadGuard.cpp
        src/malamore/threads/thread_pool.cpp

        src/malamore/wrapper_tasks/hardware_reader.cpp
        src/malamore/wrapper_tasks/telemetry_writer.cpp

        src/malamore/winapi_client/Win32NamedPipeGuard.cpp

        src/yaucl/data/csv.cpp
)

add_executable(writing_thread_test tests/writing_thread_test.cpp)
target_link_libraries(writing_thread_test Catch2WithMain user_level_struct_library)

add_executable(structures_test tests/structures_test.cpp)
target_link_libraries(structures_test Catch2WithMain user_level_struct_library)

add_executable(test_read_driver TestReadDriver.cpp)
target_link_libraries(test_read_driver user_level_struct_library)

add_executable(test_architecture tests/test_architecture.cpp)
target_link_libraries(test_architecture Catch2WithMain user_level_struct_library)

add_executable(read_telemetry_file ReadTelemetryFile.cpp
        src/malamore/utility/telemetry_formatter.cpp)

add_executable(test_arch TestArchitecture.cpp)
target_link_libraries(test_arch  user_level_struct_library)



if(LINK_KERNEL)


    # =========================================================================
    # CONFIGURAZIONE STRATO 2: APP UWP / WINDOWS IOT CORE (COMPILATORE C++/CX)
    # =========================================================================
elseif(LINK_UWP_IOT)

    add_executable(TestIoTThread TestIoTThread.cpp src/malamore/threads/ThreadGuard.cpp)
else()
    # =========================================================================
    # CONFIGURAZIONE STRATO 3: USER-MODE DESKTOP TRADIZIONALE (RING 3)
    # Osserva che la modalità IoT non supporta l'accesso e la modifica dei registri. Devo essere per forza qui.
    # =========================================================================
    add_executable(ClientReserveCPUs ClientReserveCPUs.cpp src/malamore/winapi_client/reserve_cpus.cpp)
    target_link_libraries(ClientReserveCPUs
            PRIVATE ftxui::screen
            PRIVATE ftxui::dom
            PRIVATE ftxui::component  # Not needed for this example.
    )
endif()


add_executable(test_csv_read csv_reader_test.cpp )
target_link_libraries(test_csv_read user_level_struct_library)

