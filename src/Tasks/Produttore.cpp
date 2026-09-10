#include <malamore.h>
#include <Library/KernelBinderClient.h>
#include <Library/KernelBufferChannel.h>

extern "C" __declspec(dllexport) void ExecuteCustomTask(void* pParcel, void* pIn, void* pOut) {
    BinderParcel* parcel = reinterpret_cast<BinderParcel*>(pParcel);
    
    // Legge il valore numerico fornito dal client dal parcel
    UCHAR clientInput = parcel->PayloadData[0];
    
    // Calcolo a basso livello
    UCHAR processedVal = clientInput + 10; 

    if (pOut) {
        KernelBufferChannel outputChannel(pOut);
        // Scrittura nello stream stile I/O C inter-processo
        outputChannel.Write(&processedVal, 1);
    }
}