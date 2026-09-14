#include "tasks/task_lettura.h"
#include <string.h> // Richiesto per manipolazioni stringhe elementari se necessarie

VOID TaskLetturaExecute(
    _In_ ULONG CoreId, 
    _In_ struct MessageInputMap* InMap,
    _Inout_ struct MessageOutputMap* OutMap,
    _In_opt_ PVOID CustomContext
) {
    // Supponiamo che questo core stia monitorando i flussi in ingresso dal Nodo con ID 0
    ULONG idMittente = 0;
    
    // 1. ESTRAZIONE DALLA MAPPA AD INDICIZZAZIONE DIRETTA DI RING 0 (Zero Jitter, No Lock)
    struct MSG2* msgIngresso = &InMap->Nodes[idMittente];

    // Verifichiamo se la CorePollingRoutineGeneric ha realmente catturato un messaggio per noi
    if (msgIngresso->Dimension > 0 && msgIngresso->Buffer != NULL) {
        
        PUCHAR payloadGrezzo = (PUCHAR)msgIngresso->Buffer;
        
        // ----------------=========================================================
        // LOGICA DI ELABORAZIONE REALE A PRIORITÀ CRITICA (HIGH_LEVEL)
        // ----------------=========================================================
        // Es. Filtro sui dati, decodifica o manipolazione del pacchetto a Ring 0.
        // Simuliamo un'operazione atomica sul primo byte del payload.
        UCHAR byteElaborato = payloadGrezzo[0] ^ 0x55; 

        // 2. DISPACCIAMENTO VERSO IL NODO SUCCESSIVO DELLA PIPELINE (es. ID Destinatario 2)
        ULONG idDestinatario = 2;
        struct MSG2* msgUscita = &OutMap->Nodes[idDestinatario];

        // Per rispettare i vincoli del NonPagedPoolNx ed evitare allocazioni dinamiche runtime,
        // utilizziamo l'area di memoria persistente passata nel CustomContext come area di transito.
        if (CustomContext != NULL) {
            PUCHAR bufferDiScambio = (PUCHAR)CustomContext;
            
            // Prepariamo una stringa o una struct di telemetria a lunghezza variabile
            // (Ad esempio copiamo una firma di testo fissa unita al dato elaborato hardware)
            const char* prefissoLog = "[CORE_REALTIME_DATA]: ";
            ULONG lunghezzaPrefisso = 22; // Lunghezza letterale del prefisso
            
            // Copiamo il prefisso nel buffer del CustomContext
            RtlCopyMemory(bufferDiScambio, prefissoLog, lunghezzaPrefisso);
            
            // Iniettiamo il byte elaborato subito dopo il prefisso
            bufferDiScambio[lunghezzaPrefisso] = byteElaborato;
            bufferDiScambio[lunghezzaPrefisso + 1] = '\0'; // Terminazione stringa di sicurezza

            // 3. VALORIZZAZIONE DELL'IOVEC IN USCITA PER IL FLUSHING
            // Impostiamo il puntatore al buffer e la dimensione reale dei dati modificati.
            // Al termine della funzione, la CorePollingRoutineGeneric intercetterà questo slot 
            // a livello DISPATCH_LEVEL e lo spingerà autonomamente nel Ring Buffer condiviso a valle.
            msgUscita->Buffer = bufferDiScambio;
            msgUscita->Dimension = lunghezzaPrefisso + 2; // Lunghezza totale del messaggio variabile
        }
    } else {
        // Se non ci sono messaggi in ingresso in questa iterazione periodica del tandem,
        // lo slot del core logico si azzera e non genera operazioni di Flushing spurie.
        UNREFERENCED_PARAMETER(CoreId);
    }
}
