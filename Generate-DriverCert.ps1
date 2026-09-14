[CmdletBinding()]
param (
    [Parameter(Mandatory = $true, HelpMessage = "Il DnsName del certificato self-signed.")]
    [string]$DnsName,

    [Parameter(Mandatory = $true, HelpMessage = "Il nome del file .pfx da generare.")]
    [string]$OutputFilename
)

# Ottiene la cartella corrente in cui lo script viene eseguito
$currentFolder = Get-Location

# Definisce i percorsi completi per i file di output
$pfxPath = Join-Path -Path $currentFolder -ChildPath $OutputFilename
$baseName = [System.IO.Path]::GetFileNameWithoutExtension($OutputFilename)
$txtPath = Join-Path -Path $currentFolder -ChildPath "thumbprint_$baseName.txt"

Write-Host "===================================================" -ForegroundColor Gray
Write-Host " Generazione Certificato Driver in corso (PowerShell)..." -ForegroundColor Yellow
Write-Host "===================================================" -ForegroundColor Gray
Write-Host "DnsName:         $DnsName"
Write-Host "Output PFX:      $pfxPath"
Write-Host "Output TXT Hash: $txtPath"
Write-Host "---------------------------------------------------" -ForegroundColor Gray

try {
    # 1. Crea il certificato nello Store Utente Personale
    $cert = New-SelfSignedCertificate -DnsName $DnsName -Type CodeSigning -CertStoreLocation Cert:\CurrentUser\My

    # 2. Richiede la password all'utente in modo sicuro (i caratteri saranno mascherati)
    $securePassword = Read-Host -Prompt "Inserisci la password per proteggere il file PFX" -AsSecureString

    # 3. Esporta il certificato in formato PFX
    Export-PfxCertificate -Cert "Cert:\CurrentUser\My\$($cert.Thumbprint)" -FilePath $pfxPath -Password $securePassword | Out-Null

    # 4. Salva il Thumbprint nel file di testo
    $cert.Thumbprint | Out-File -FilePath $txtPath -Encoding ascii

    Write-Host "---------------------------------------------------" -ForegroundColor Gray
    Write-Host "Certificato e file del Thumbprint creati con successo!" -ForegroundColor Green
    Write-Host "Thumbprint salvato in: thumbprint_$baseName.txt" -ForegroundColor Cyan
}
catch {
    Write-Error "Si è verificato un errore durante la generazione del certificato: $_"
    exit 1
}
