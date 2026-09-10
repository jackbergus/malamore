cmake_minimum_required(VERSION 4.0)
project(malamore C CXX)
include_directories(include)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compila i componenti in Modalità Utente
# 1. Crea la libreria statica a oggetti (genera KernelBinderClientLib.lib)
add_library(KernelBinderClientLib STATIC
        src/Library/KernelBinderClient.cpp
        src/Library/KernelBufferChannel.cpp
        src/Library/PinnedMemory.cpp
)

# Compila il ServiceManager
add_executable(KernelServiceManager src/Server/ServiceManager.cpp)

# Compila il Worker Realtime
add_executable(KernelServer    src/Server/KernelServer.cpp
        src/Server/ServerContext.cpp
        src/Server/DllTaskManager.cpp
       src/Server/SafeRecovery.cpp
        src/Server/EdfScheduler.cpp)
target_link_libraries(KernelServer PRIVATE KernelBinderClientLib)

# Compila le 3 DLL dei Task Realtime
add_library(DllProduttore SHARED src/Tasks/Produttore.cpp)
target_link_libraries(DllProduttore PRIVATE KernelBinderClientLib)

add_library(DllConsumatore SHARED src/Tasks/Consumatore.cpp)
target_link_libraries(DllConsumatore PRIVATE KernelBinderClientLib)

add_library(DllTaskCondiviso SHARED src/Tasks/TaskCondiviso.cpp)
target_link_libraries(DllTaskCondiviso PRIVATE KernelBinderClientLib)

add_executable(KernelClient src/Client/Client.cpp)
target_link_libraries(KernelClient PRIVATE KernelBinderClientLib)

# Aggiunge il sotto-progetto per il driver
add_subdirectory(src/Driver)