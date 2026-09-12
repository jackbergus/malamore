cmake_minimum_required(VERSION 4.0)
project(malamore C CXX)

include_directories(include)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

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
set(SIGN_THUMBPRINT "7D89486963BA0DE667E29219936312532D9195E8")
# ==============================================================================

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/submodules/findwdk/cmake")
find_package(WDK REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(Python_EXECUTABLE ${Python3_EXECUTABLE})
set(WDK_BIN "${WDK_ROOT}/bin/${WDK_VERSION}")
message(STATUS "Python trovato in: ${Python_EXECUTABLE}")

# Aggiunta del driver (rimosso EXCLUDE_FROM_ALL per farlo compilare di default)
wdk_add_driver(${DRIVER_NAME}
        KMDF 1.15
        WINVER ${WDK_WINVER}
        simple_driver.cpp
)

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
        COMMAND signtool.exe sign /debug /ph /fd "sha256" /sha1 "${SIGN_THUMBPRINT}" /v "${DRIVER_NAME}.sys"
        COMMAND signtool.exe sign /debug /ph /fd "sha256" /sha1 "${SIGN_THUMBPRINT}" /v "${DRIVER_NAME}.cat"
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
