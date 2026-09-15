#include "tasks/task_lettura.h"
#include <string.h> // Richiesto per manipolazioni stringhe elementari se necessarie

VOID TaskLetturaExecute(
    _In_ ULONG CoreId,
    _In_ struct MessageMap* InMap,
    _Inout_ struct MessageMap* OutMap,
    _In_opt_ PVOID ThreadContext
) {
    ULONG idMittente = 0;
    if (ThreadContext == NULL || InMap == NULL || OutMap == NULL) {
        return;
    }

    if (!MessageMap_IsEmpty(InMap)) {
        const struct MSG2* msg = MessageMap_Top(InMap);
        UCHAR byteElaborato = msg->Buffer[0] ^ 0x55;
        ULONG idDestinatario = 2;
        MessageMap_Push(OutMap, idDestinatario, (PCHAR)&byteElaborato, sizeof(byteElaborato));
    }
}
