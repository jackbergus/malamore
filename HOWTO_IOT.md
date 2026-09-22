# Impostare l'ambiente Soft Real Time in Windows

Questo manuale specifica come questo si possa ottenere:

https://learn.microsoft.com/en-us/windows/iot/iot-enterprise/soft-real-time/soft-real-time-device#use-mdm-bridge-wmi-provider-to-configure-the-windowsiot-csp

## Use MDM Bridge WMI Provider to Configure the WindowsIoT CSP

This CSP configures the system for real-time performance. You need to provide the number of CPU cores to allocate to real-time tasks, with the rest being allocated for running system or standard user tasks. A numerical value must be provided in the SetRTCores node. This is the number of CPU Cores dedicated to real-time workloads. Valid numeric values must be at least 1 and less than the number of physical cores in the CPU.

Environments that use Windows Management Instrumentation (WMI) can use the MDM Bridge WMI Provider to accomplish this. Here's an example to set the RealTime configuration with three real-time cores:

 1. Download the psexec tool.
 2. Run an administrative command prompt.
 3. In the command prompt, launch PowerShell under system account ```psexec.exe -s -i powershell.exe```
 4. Execute the following script:

```cmd
$nameSpaceName="root\cimv2\mdm\dmmap"
$className="MDM_WindowsIoT_SoftRealTimeProperties01"
$obj = Get-CimInstance -Namespace $namespaceName -ClassName $className
Add-Type -AssemblyName System.Web
Set-CimInstance -CimInstance $obj
$obj.SetRTCores = 16
Set-CimInstance -CimInstance $obj
```

> [!TIP]
> You can use the same script for whatever number of real-time cores you need to have, just replacing the 3 in the second-to-last line with the appropriate number. This will reserve cores starting with the highest core and going downwards. So reserving 3 cores on a 4 core CPU will reserve cores 3, 2, and 1 and leave core 0 for system and non-real-time tasks. Please note that there's no guarantee that the real-time cores are going to remain highest, since the real-time core indices will remain fixed if increasing the CPU core count afterwards.