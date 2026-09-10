cmake_minimum_required(VERSION 4.0)
project(malamore C CXX)
include_directories(include)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_executable(malamore main.cpp)

# Compila i componenti in Modalità Utente
# 1. Crea la libreria statica a oggetti (genera KernelBinderClientLib.lib)
add_library(KernelBinderClientLib STATIC
        include/Client/KernelBinderClient.h
        src/Client/KernelBinderClient.cpp
)
add_executable(KernelServer src/Server/server.cpp)
target_link_libraries(KernelServer PRIVATE KernelBinderClientLib)
add_executable(KernelClient src/Client/client.cpp)
target_link_libraries(KernelClient PRIVATE KernelBinderClientLib)

add_library(MyTask SHARED src/Tasks/MyTask.cpp)
target_link_libraries(MyTask PRIVATE KernelBinderClientLib)

# Aggiunge il sotto-progetto per il driver
add_subdirectory(src/Driver)