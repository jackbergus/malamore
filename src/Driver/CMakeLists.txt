cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4 /WX")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../submodules/findwdk/cmake")
set(WDK_TARGET_VERSION "Win11" CACHE STRING "Target Windows Version")
find_package(WDK REQUIRED)

# Definiamo la versione corretta di Windows (Windows 10/11 moderna)
# per abilitare ExAllocatePool2 senza warning di build
set(WDK_NTDDI_VERSION "0x0A00000C" CACHE STRING "NTDDI_VERSION") # Win11 22H2+


# Specifica che questo target genera un Driver di tipo Kernel (WDM/KMDF)
wdk_add_driver(KernelBinder
        Driver.cpp
        ../../include/malamore.h
)


# NOTA: Rimosso _KERNEL_MODE da qui perché lo inserisce già il compilatore /kernel
target_compile_definitions(KernelBinder PRIVATE
        LINK_KERNEL
        NTDDI_VERSION=0x0A00000C   # Comunica all'SDK che supportiamo ExAllocatePool2
        _WIN32_WINNT=0x0A00        # Imposta la macro corretta per Windows 10/11 (invece di 0x0601/Win7)
)

# Se i warning del WDK sono ancora troppo severi, puoi disattivare
# selettivamente il blocco solo per i codici di deprecazione (C4996)
target_compile_options(KernelBinder PRIVATE "/wd4996")