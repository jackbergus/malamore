#include <malamore.h>
#include <Library/KernelBinderClient.h>
#include <iostream>

extern "C" __declspec(dllexport) void ExecuteCustomTask(void* pParcel, void* pIn, void* pOut) {
    BinderParcel* parcel = reinterpret_cast<BinderParcel*>(pParcel);
    // Task periodico: legge e incrementa un contatore interno hardware per diagnostica
    parcel->PayloadData[0] += 1;
}
