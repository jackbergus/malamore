import argparse
from datetime import datetime

def format_date_mdy(date_obj=None):
    """
    Restituisce la data nel formato corretto Month/Day/Year (MM/DD/YYYY).
    """
    try:
        if date_obj is None:
            date_obj = datetime.now()
        elif not isinstance(date_obj, datetime):
            raise TypeError("date_obj deve essere un oggetto datetime o None.")

        # Corretto il bug: l'INF di Windows richiede tassativamente MM/DD/YYYY
        return date_obj.strftime("%m/%d/%Y")
    except Exception as e:
        return f"Error: {e}"

def generate_inf_file(driver_name,
                      manufacturer,
                      version_major,
                      version_minor,
                      version_patch,
                      version_tweak,
                      guid="4d36e97d-e325-11ce-bfc1-08002be10318"):

    date = format_date_mdy(datetime.now())

    if guid is None or guid == "":
        import uuid
        guid = f"{{{str(uuid.uuid5(uuid.NAMESPACE_DNS, manufacturer + driver_name)).upper()}}}"

    inf_file = f"""
;
; {driver_name}.inf
;

[Version]
Signature   = "$WINDOWS NT$"
Class       = System
ClassGuid   = {guid}
Provider    = %ManufacturerName%
CatalogFile = {driver_name}.cat
DriverVer   = {date},{version_major}.{version_minor}.{version_patch}.{version_tweak}
PnpLockdown = 1

[DestinationDirs]
DefaultDestDir = 13

[SourceDisksNames]
1 = %DiskName%,,,""

[SourceDisksFiles]
{driver_name}.sys = 1,,

;*****************************************
; Install Section
;*****************************************

[Manufacturer]
%ManufacturerName% = Standard,NT$ARCH$.10.0...16299

[Standard.NT$ARCH$.10.0...16299]
%{driver_name}.DeviceDesc% = {driver_name}_Device, Root\\{driver_name}

[KMDFDriver2_Device.NT]
CopyFiles = File_Copy

[File_Copy]
{driver_name}.sys

;-------------- Service installation
[{driver_name}_Device.NT.Services]
AddService = {driver_name},%SPSVCINST_ASSOCSERVICE%, {driver_name}_Service_Inst

; -------------- {driver_name} driver install sections
[{driver_name}_Service_Inst]
DisplayName    = %{driver_name}.SVCDESC%
ServiceType    = 1               ; SERVICE_KERNEL_DRIVER
StartType      = 3               ; SERVICE_DEMAND_START
ErrorControl   = 1               ; SERVICE_ERROR_NORMAL
ServiceBinary  = %13%\\{driver_name}.sys

[{driver_name}_Device.NT.Wdf]
KmdfService = {driver_name}, {driver_name}_wdfsect

[{driver_name}_wdfsect]
KmdfLibraryVersion = 1.15

[Strings]
SPSVCINST_ASSOCSERVICE = 0x00000002
ManufacturerName = "{manufacturer}"
DiskName = "{driver_name} Installation Disk"
{driver_name}.DeviceDesc = "{driver_name} Device"
{driver_name}.SVCDESC = "{driver_name} Service"
"""
    # Il file viene salvato nella directory di lavoro corrente (la cartella di build di CMake)
    with open(f"{driver_name}.inf", 'w', encoding='utf-8') as f:
        f.write(inf_file.strip())

if __name__ == "__main__":
    # Configurazione del parser degli argomenti da riga di comando
    parser = argparse.ArgumentParser(description="Generatore di file INF per driver Windows di malamore.")

    parser.add_argument("--driver_name", required=True, help="Nome del driver (.sys)")
    parser.add_argument("--manufacturer", required=True, help="Nome del produttore")
    parser.add_argument("--major", required=True, help="Versione Major")
    parser.add_argument("--minor", required=True, help="Versione Minor")
    parser.add_argument("--patch", required=True, help="Versione Patch")
    parser.add_argument("--tweak", required=True, help="Versione Tweak")
    parser.add_argument("--guid", default="{4d36e97d-e325-11ce-bfc1-08002be10318}", help="ClassGuid del Driver")

    args = parser.parse_args()

    # Invocazione della funzione con i parametri mappati
    generate_inf_file(
        driver_name=args.driver_name,
        manufacturer=args.manufacturer,
        version_major=args.major,
        version_minor=args.minor,
        version_patch=args.patch,
        version_tweak=args.tweak,
        guid=args.guid
    )
# ```
#
# ### Cosa è cambiato nello specifico?
# 1. **Risoluzione del Bug della Data:** Modificato `strftime("%d/%m/%Y")` in `strftime("%m/%d/%Y")`. Lo strumento `inf2cat` di Microsoft scarterebbe il file se rilevasse un mese superiore a 12 (scambiandolo per il giorno).
# 2. **Uso di `argparse`:** Il blocco `if __name__ == "__main__":` intercetta le stringhe passate da CMake (es: `--driver_name "${DRIVER_NAME}"`) e le mappa ordinatamente dentro le variabili della funzione.
# 3. **Correzione delle Stringhe Generiche:** Sostituiti i vecchi placeholder statici hardcoded `KMDFDriver2.DeviceDesc` nelle stringhe finali con la variabile dinamica `{driver_name}.DeviceDesc` per evitare conflitti di validazione nell'INF.
#
# Vuoi che verifichiamo anche come configurare la **creazione automatica del certificato PFX di test** se non è presente nel sistema, in modo da automatizzare completamente l'ambiente di sviluppo locale?
