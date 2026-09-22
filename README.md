# MALAMORE: Windows Kernel Binder (Low-Latency IPC Architecture)

Questa documentazione descrive l'architettura **Windows Kernel Binder**, un pattern di comunicazione inter-processo (IPC) ad altissime prestazioni pensato per aggirare i limiti dello scheduler di Windows e garantire l'esecuzione deterministica di task dinamici da 10ms a basso jitter distribuibili su N nodi concorrenti con granularità di jitter inferiore ai 400$\mu$s.

---


# Required commands before starting the architecture

## Device Interrupt Management on low-priority cores

As a first step, you need to set up all the devices to handle their interrupts with low priority over the non-performant cores. In this way, we can ensure to reserve the most performant cores to the reading task. 
To do so, you can run the ```ClientReserveCPUs.exe``` program with superuser abilities. This will force the management of each device over the high-end and less-efficient CPUs, thus avoiding their interference with the
tasks scheduled at the lower level, thus avoiding any further disruption caused by their management. 

After running this binary with Administrator privileges, you need to reboot your machine to make the changes effective.

## After each reboot

After each reboot, just for the first time the system is on, you need to remember to run the following commands.

### Disable energy saving

When energy-saving modes (Balanced, Power Saver) are enabled, Windows performs dynamic power management by:

 * Reducing CPU frequency during low-load conditions using **Dynamic Voltage and Frequency Scaling (DVFS)**
 * Parking inactive cores to save energy
 * Steering threads between high-performance cores (P-cores) and energy-efficient cores (E-cores) on hybrid CPUs

Disabling autonomous power management or using High Performance/Catnip Low Latency plans eliminates these delays by:

 * Keeping all cores active
 * Running CPUs at maximum P-state (frequency)
 * Preventing core parking

This is achieved in the following way

```cmd
:: Switch to High Performance Plan
powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c

:: Disable Core Parking (Forces all cores to stay awake)
powercfg /SETACVALUEINDEX SCHEME_CURRENT SUB_PROCESSOR CPMINCORES 100
powercfg /SETDCVALUEINDEX SCHEME_CURRENT SUB_PROCESSOR CPMINCORES 100

:: Apply changes
powercfg /setactive SCHEME_CURRENT
```

In IoT mode, you can also disable CPU idle states, where a CPU with no instructions to run will enter a power-saving state. 
This is undesirable in real-time scenarios as idle CPUs have a delay in starting to execute new instructions:

```cmd
powercfg.exe /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR IdleDisable 1
```

### Disable Diagnostic Policy Service

Run these three commands in a cmd prompt to disable DPS:

```cmd
sc query dps
sc stop dps
sc config dps start=disabled
```


### Disable Audio

Run these three commands in a cmd prompt to disable Audiosrv:

```cmd
sc query Audiosrv
sc stop Audiosrv
sc config Audiosrv start=disabled
```

### Disable Superfetch

Run these three commands in a cmd prompt to disable  Superfetch:

```cmd
sc query SysMain
sc stop SysMain
sc config SysMain start=disabled
```

### Disable Windows Update

Run these three commands in a cmd prompt to disable Windows Update:

> [!CAUTION]
> This will open up your device to vulnerabilities as security patches will not go through. That said, it is necessary as the Windows Update agent does not respect CPU core isolation. We recommend having a plan to ensure device security and install updates during times when the device can be taken down for maintenance
> 

> [!TIP]
> A good example of managing updates during downtime can be found in the UWF documentation: [Service UWF-protected devices](https://learn.microsoft.com/en-us/windows-hardware/customize/enterprise/service-uwf-protected-devices) If you are using UWF and Soft Real-time then this process should take care of the OS update need for both features at the same time.
> 

```cmd
sc query wuauserv
sc stop wuauserv
sc config wuauserv start=disabled
```

### (Optional) Run this command to disable threaded DPCs

Disable threaded DPCs for debugging

```cmd
reg add "HKLM\System\CurrentControlSet\Control\Session Manager\kernel" /v ThreadDpcEnable /t REG_DWORD /f /d 0
```


### Ensure that all the processes currently running on the OS are not running on my same core

In particular, you can ensure the currently running processes on the machine are never going to interfere with the task that you 
want by requiring them to "live" on the highest cores, where no efficiency performances are there. This implicitly frees the 
lower cores, that can be then dedicated to the current main process.

```cmd
# 480 in decimal = 0x1E0 in hex = Binary 000111100000 (allowing current processes to run only on the cores set to 1 in binary)
$Mask = 480 

# Get all running processes, filtering out critical system idles that throw access errors
Get-Process | Where-Object { $_.Id -gt 4 -and $_.ProcessName -notmatch "Idle|System" } | ForEach-Object {
    try {
        $_.ProcessorAffinity = $Mask
    }
    catch {
        # Catch and skip protected kernel/security processes that won't allow modification
        Write-Verbose "Skipped protected process: $($_.ProcessName)"
    }
}
```