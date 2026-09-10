#include <malamore.h>
#include <Library/KernelBinderClient.h>
#include <Library/KernelBufferChannel.h>
#include <cstring>

extern "C" __declspec(dllexport) void ExecuteCustomTask(void* pParcel, void* pIn, void* pOut) {
    BinderParcel* parcel = reinterpret_cast<BinderParcel*>(pParcel);

    if (pIn) {
        KernelBufferChannel inputChannel(pIn);
        UCHAR dataFromProducer = 0;

        while (inputChannel.Read(&dataFromProducer, 1) == 0) {
            _mm_pause();
        }

        // Generiamo una risposta a lunghezza variabile (es. una stringa descrittiva)
        char messaggioRisposta[256];
        sprintf_s(messaggioRisposta, "Risultato Elaborato Hardware: %d", dataFromProducer * 2);

        ULONG stringLength = static_cast<ULONG>(std::strlen(messaggioRisposta) + 1);

        // Copiamo l'intero flusso di byte nel pacchetto ed aggiorniamo la lunghezza reale
        std::memcpy(parcel->PayloadData, messaggioRisposta, stringLength);
        parcel->DataLength = stringLength;
    }
}
