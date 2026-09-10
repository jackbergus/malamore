#include "Library/KernelBinderClient.h"
#include <malamore.h>
#include <stdexcept>
#include <iostream>



KernelBinderClient::KernelBinderClient() : m_hDevice(INVALID_HANDLE_VALUE) {
    m_hDevice = CreateFileW(L"\\\\.\\KernelBinderLink", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (m_hDevice == INVALID_HANDLE_VALUE) throw std::runtime_error("Driver non caricato.");
}
KernelBinderClient::~KernelBinderClient() { Cleanup(); }
void KernelBinderClient::Cleanup() { if (m_hDevice != INVALID_HANDLE_VALUE) CloseHandle(m_hDevice); }

bool KernelBinderClient::SpawnNode(const std::string& nodeName, const std::string& dllPath) {
    BinderParcel parcel = {0};
    parcel.CommandType = CMD_SPAWN_NODE;
    strcpy_s(parcel.TargetNodeName, 64, nodeName.c_str());
    strcpy_s(parcel.DllPath, MAX_PATH, dllPath.c_str());
    BOOL success = DeviceIoControl(m_hDevice, IOCTL_BINDER_COMMAND, &parcel, sizeof(BinderParcel), &parcel, sizeof(BinderParcel), NULL, NULL);
    return success && parcel.ResponseStatus == STATUS_BINDER_OK;
}

bool KernelBinderClient::RegisterPeriodicTask(const std::string& nodeName, const std::string& taskName, ULONG periodUs, ULONG wcetUs) {
    BinderParcel parcel = {0};
    parcel.CommandType = CMD_REGISTER_PERIODIC_TASK;
    strcpy_s(parcel.TargetNodeName, 64, nodeName.c_str());
    strcpy_s(parcel.TaskMeta.TaskName, 64, taskName.c_str());
    parcel.TaskMeta.PeriodUs = periodUs;
    parcel.TaskMeta.EstimatedWcetUs = wcetUs;
    BOOL success = DeviceIoControl(m_hDevice, IOCTL_BINDER_COMMAND, &parcel, sizeof(BinderParcel), &parcel, sizeof(BinderParcel), NULL, NULL);
    return success && parcel.ResponseStatus == STATUS_BINDER_OK;
}

bool KernelBinderClient::ConnectNodes(const std::string& sourceNode, const std::string& targetNode) {
    BinderParcel parcel = {0};
    parcel.CommandType = CMD_CONNECT_NODES;
    strcpy_s(parcel.SourceNodeName, 64, sourceNode.c_str());
    strcpy_s(parcel.TargetNodeName, 64, targetNode.c_str());
    return DeviceIoControl(m_hDevice, IOCTL_BINDER_COMMAND, &parcel, sizeof(BinderParcel), &parcel, sizeof(BinderParcel), NULL, NULL);
}

bool KernelBinderClient::SignalNode(const std::string& nodeName, const uint8_t* data, ULONG length, uint8_t* outResponse, ULONG* outLength) {
    BinderParcel parcel = {0};
    parcel.CommandType = CMD_SIGNAL_NODE;
    strcpy_s(parcel.TargetNodeName, 64, nodeName.c_str());

    // Iniezione dei dati in ingresso (se presenti)
    if (data && length > 0 && length <= 1024) {
        std::memcpy(parcel.PayloadData, data, length);
        parcel.DataLength = length;
    } else {
        parcel.DataLength = 0;
    }

    // Se si attende una risposta, indichiamo nel pacchetto la capacità massima del buffer fornito
    if (outResponse && outLength) {
        parcel.DataLength = *outLength;
    }

    BOOL success = DeviceIoControl(m_hDevice, IOCTL_BINDER_COMMAND, &parcel, sizeof(BinderParcel), &parcel, sizeof(BinderParcel), NULL, NULL);

    if (success) {
        // Estrazione della risposta a lunghezza variabile
        if (outResponse && outLength) {
            // Impedisce buffer overflow locali se il server risponde con più dati del previsto
            ULONG bytesToCopy = (parcel.DataLength < *outLength) ? parcel.DataLength : *outLength;
            std::memcpy(outResponse, parcel.PayloadData, bytesToCopy);
            *outLength = bytesToCopy; // Comunica al client quanti byte sono stati effettivamente scritti
        }
        return true;
    }
    return false;
}

bool KernelBinderClient::DestroyNode(const std::string& nodeName) {
    BinderParcel parcel = {0};
    parcel.CommandType = CMD_DESTROY_NODE;
    strcpy_s(parcel.TargetNodeName, 64, nodeName.c_str());
    return DeviceIoControl(m_hDevice, IOCTL_BINDER_COMMAND, &parcel, sizeof(BinderParcel), &parcel, sizeof(BinderParcel), NULL, NULL) != FALSE;
}