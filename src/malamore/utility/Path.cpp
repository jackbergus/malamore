//
// Created by Giacomo Bergami on 16/09/2026.
//

//
// Created by Giacomo Bergami on 16/09/2026.
//
//
// Created by Giacomo Bergami on 16/09/2026.
//

#include <malamore/utility/Path.h>

void MalamorePath::AnalyzePathContext() {
    m_IsHardwareDevice = FALSE;

    // Rilevamento in Ring 3: Inizia con il link simbolico di periferica utente \\.
    if (wcsncmp(m_Buffer, LR"(\\.\)", 4) == 0) {
        m_IsHardwareDevice = TRUE;
        return;
    }
    // Rilevamento in Ring 0: Inizia con la directory dei device hardware oggetti di Windows
    if (wcsncmp(m_Buffer, LR"(\DosDevices\)", 12) == 0 || wcsncmp(m_Buffer, LR"(\??\)", 4) == 0) {
        m_IsHardwareDevice = TRUE;
        return;
    }

    // Se non ha i prefissi, controlliamo se è un nome nudo di un driver senza lettere di volume (es: "FSCC0")
    size_t len = 0;
    RtlStringCchLengthW(m_Buffer, MAX_PATH, &len);
    if (len > 0 && wcschr(m_Buffer, L':') == nullptr && wcschr(m_Buffer, L'\\') == nullptr && wcschr(m_Buffer, L'/') == nullptr) {
        m_IsHardwareDevice = TRUE; // È un driver di periferica implicito
    }
}

void MalamorePath::normalize() {
    for (size_t i = 0; i < MAX_PATH && m_Buffer[i] != L'\0'; ++i) {
        if (m_Buffer[i] == L'/') {
            m_Buffer[i] = preferred_separator;
        }
    }
    AnalyzePathContext(); // Ricalcola il contesto dopo la normalizzazione
}

MalamorePath::MalamorePath() : m_IsHardwareDevice(FALSE) {
    RtlZeroMemory(m_Buffer, sizeof(m_Buffer));
}

MalamorePath::MalamorePath(const WCHAR* p) : m_IsHardwareDevice(FALSE) {
    RtlZeroMemory(m_Buffer, sizeof(m_Buffer));
    if (p != nullptr) {
        RtlStringCchCopyW(m_Buffer, MAX_PATH, p);
        normalize();
    }
}

MalamorePath::MalamorePath(const MalamorePath& other) {
    RtlCopyMemory(m_Buffer, other.m_Buffer, sizeof(m_Buffer));
    m_IsHardwareDevice = other.m_IsHardwareDevice;
}

MalamorePath& MalamorePath::operator=(const MalamorePath& other) {
    if (this != &other) {
        RtlCopyMemory(m_Buffer, other.m_Buffer, sizeof(m_Buffer));
        m_IsHardwareDevice = other.m_IsHardwareDevice;
    }
    return *this;
}

MalamorePath& MalamorePath::operator=(const WCHAR* p) {
    RtlZeroMemory(m_Buffer, sizeof(m_Buffer));
    m_IsHardwareDevice = FALSE;
    if (p != nullptr) {
        RtlStringCchCopyW(m_Buffer, MAX_PATH, p);
        normalize();
    }
    return *this;
}

BOOLEAN MalamorePath::empty() const {
    return (m_Buffer[0] == L'\0') ? TRUE : FALSE;
}

/**
 * APPEND INTEGRATO: DISCRIMINA TRA FILE SYSTEM E PERIFERICHE HARDWARE
 */
MalamorePath& MalamorePath::operator/=(const MalamorePath& other) {
    if (other.empty()) return *this;

    size_t len = 0;
    RtlStringCchLengthW(m_Buffer, MAX_PATH, &len);

    // COMPORTAMENTO A CONTESTO HARDWARE MUTATO:
    // Se siamo su una periferica pura (es. \\.\FSCC0), l'operatore / NON inserisce
    // il backslash, ma effettua un append lineare (es. per comporre comandi o canali stream "FSCC0" + "0")
    if (m_IsHardwareDevice) {
        RtlStringCchCatW(m_Buffer, MAX_PATH, other.m_Buffer);
    }
    else {
        // Comportamento ordinario su File System
        if (len > 0 && m_Buffer[len - 1] != preferred_separator && other.m_Buffer[0] != preferred_separator) {
            RtlStringCchCatW(m_Buffer, MAX_PATH, L"\\");
        }
        const WCHAR* appendPtr = other.m_Buffer;
        if (len > 0 && m_Buffer[len - 1] == preferred_separator && other.m_Buffer[0] == preferred_separator) {
            appendPtr++;
        }
        RtlStringCchCatW(m_Buffer, MAX_PATH, appendPtr);
    }

    normalize();
    return *this;
}

/**
 * ESTRAZIONE DEL FILENAME CON BLOCCO HARDWARE
 */
MalamorePath MalamorePath::filename() const {
    if (m_IsHardwareDevice) {
        // Per una periferica (es. \\.\FSCC0 o \DosDevices\FSCC0), il filename è l'intera periferica stessa!
        // Estrarre solo l'ultimo pezzo distruggerebbe il link hardware.
        return *this;
    }

    size_t len = 0;
    RtlStringCchLengthW(m_Buffer, MAX_PATH, &len);
    if (len == 0) return MalamorePath();

    LONG i = static_cast<LONG>(len) - 1;
    for (; i >= 0; --i) {
        if (m_Buffer[i] == preferred_separator) break;
    }

    if (i < 0) return MalamorePath(m_Buffer);
    return MalamorePath(&m_Buffer[i + 1]);
}

/**
 * ESTRAZIONE DEL PARENT PATH CON BLOCCO HARDWARE
 */
MalamorePath MalamorePath::parent_path() const {
    if (m_IsHardwareDevice) {
        // Una periferica pura non ha una "cartella superiore" nel file system ordinario,
        // restituiamo l'oggetto stesso per indicare che siamo sul nodo radice hardware.
        return *this;
    }

    size_t len = 0;
    RtlStringCchLengthW(m_Buffer, MAX_PATH, &len);
    if (len == 0) return MalamorePath();

    LONG i = static_cast<LONG>(len) - 1;
    for (; i >= 0; --i) {
        if (m_Buffer[i] == preferred_separator) break;
    }

    if (i <= 0) return MalamorePath(L"");

    WCHAR temp[MAX_PATH];
    RtlZeroMemory(temp, sizeof(temp));
    RtlCopyMemory(temp, m_Buffer, i * sizeof(WCHAR));
    return MalamorePath(temp);
}

// =========================================================================
// TRASFORMAZIONI HARDWARE RIGIDE OPERANTI IN ENTRAMBI I MONDI
// =========================================================================



/**
 * TRASFORMAZIONE IN PERCORSO KERNEL DI WINDOWS NT (RING 0)
 */
const WCHAR* MalamorePath::ToKernelPath(WCHAR* outBuffer, ULONG maxCch) const {
    if (this->empty() || outBuffer == nullptr) return L"";
    RtlZeroMemory(outBuffer, maxCch * sizeof(WCHAR));

    // REQUISITO: Se a Ring 3 abbiamo configurato "C:\Windows\...", lo convertiamo nel link simbolico nativo \SystemRoot
    if (_wcsnicmp(m_Buffer, L"C:\\Windows\\", 11) == 0) {
        RtlStringCchPrintfW(outBuffer, maxCch, L"\\SystemRoot\\%s", m_Buffer + 11);
        return outBuffer;
    }
    if (_wcsnicmp(m_Buffer, L"C:\\Windows", 10) == 0 && m_Buffer[10] == L'\0') {
        RtlStringCchCopyW(outBuffer, maxCch, L"\\SystemRoot");
        return outBuffer;
    }

    // Periferiche hardware Win32 (\\\\.\\FSCC0) -> \DosDevices\FSCC0
    if (wcsncmp(m_Buffer, L"\\\\.\\", 4) == 0) {
        RtlStringCchPrintfW(outBuffer, maxCch, L"\\DosDevices\\%s", m_Buffer + 4);
        return outBuffer;
    }

    // Lettere di volume tradizionali (D:\...) -> \\??\\D:\...
    if (m_Buffer[0] != L'\0' && m_Buffer[1] == L':') {
        RtlStringCchPrintfW(outBuffer, maxCch, L"\\??\\%s", m_Buffer);
        return outBuffer;
    }

    // Se inizia già con una barra (\SystemRoot\ o \DosDevices\), è un percorso nativo pronto
    if (m_Buffer[0] == L'\\') {
        RtlStringCchCopyW(outBuffer, maxCch, m_Buffer);
        return outBuffer;
    }

    // Stringa nuda (FSCC0) -> Periferica implicita
    RtlStringCchPrintfW(outBuffer, maxCch, L"\\DosDevices\\%s", m_Buffer);
    return outBuffer;
}

/**
 * PARSER DI ESPANSIONE PER LE CHIAMATE SIMULATE (RING 3)
 */
const WCHAR* MalamorePath::ToWin32Path(WCHAR* outBuffer, ULONG maxCch) const {
    if (this->empty() || outBuffer == nullptr) return L"";
    RtlZeroMemory(outBuffer, maxCch * sizeof(WCHAR));

    // REQUISITO: Se incontriamo la cartella virtuale del kernel \SystemRoot\, la espandiamo nella cartella fisica di Windows
    if (_wcsnicmp(m_Buffer, L"\\SystemRoot\\", 12) == 0) {
#ifdef LINK_KERNEL
        // Se siamo nel driver, ritorniamo il percorso originale perché \SystemRoot\ è perfettamente valido
        RtlStringCchCopyW(outBuffer, maxCch, m_Buffer);
#else
        // Se siamo nei test a Ring 3, espandiamo programmaticamente nel percorso reale C:\Windows
        RtlStringCchPrintfW(outBuffer, maxCch, L"C:\\Windows\\%s", m_Buffer + 12);
#endif
        return outBuffer;
    }
    if (_wcsnicmp(m_Buffer, L"\\SystemRoot", 11) == 0 && m_Buffer[11] == L'\0') {
#ifdef LINK_KERNEL
        RtlStringCchCopyW(outBuffer, maxCch, m_Buffer);
#else
        RtlStringCchCopyW(outBuffer, maxCch, L"C:\\Windows");
#endif
        return outBuffer;
    }

    // Smantella il selettore globale del kernel (\??\C:\...) -> C:\...
    if (wcsncmp(m_Buffer, L"\\??\\", 4) == 0) {
        RtlStringCchCopyW(outBuffer, maxCch, m_Buffer + 4);
        return outBuffer;
    }

    // Converte la directory dei device (\DosDevices\FSCC0) nel formato utente standard (\\\\.\\FSCC0)
    if (wcsncmp(m_Buffer, L"\\DosDevices\\", 12) == 0) {
        RtlStringCchPrintfW(outBuffer, maxCch, L"\\\\.\\%s", m_Buffer + 12);
        return outBuffer;
    }

    RtlStringCchCopyW(outBuffer, maxCch, m_Buffer);
    return outBuffer;
}
