//
// Created by Giacomo Bergami on 16/09/2026.
//

#pragma once

#ifdef __cplusplus
namespace Malamore {
    namespace Math {

        // Verifica a tempo di compilazione se un numero è una potenza di due pura
        // Proprietà binaria: (N & (N - 1)) == 0, con N > 0
        constexpr bool IsPowerOfTwo(size_t n) {
            return (n > 0) && ((n & (n - 1)) == 0);
        }

        // Calcola l'indice circolare corretto a tempo di compilazione o a runtime
        // Se la dimensione è potenza di due, usa l'AND bitwise veloce. Altrimenti esegue il modulo.
        template <size_t QueueDepth>
        constexpr size_t WrapIndex(size_t index) {
            if (IsPowerOfTwo(QueueDepth)) {
                // Ottimizzazione hardware: equivale a index % QueueDepth, ma richiede 1 solo ciclo di clock
                return index & (QueueDepth - 1);
            } else {
                // Fallback di sicurezza se la dimensione non è potenza di due
                return index % QueueDepth;
            }
        }

    } // namespace Math
} // namespace Malamore
#endif