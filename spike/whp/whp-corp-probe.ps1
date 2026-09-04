<#
Will the hypervisor substrate work on this machine?

Spike (f) measured the Windows Hypervisor Platform on one unmanaged developer
box and found it usable at a median 5.0 us per exit. That says nothing about a
managed corporate machine, where the question is almost never the hardware and
almost always the policy: whether the optional feature is installed, whether
Device Guard is configured in a way that forbids it, whether the machine is
itself a virtual desktop with no nested virtualisation to give, and whether an
endpoint product objects to a process that creates a partition.

This probe answers that, read-only and unelevated. It changes nothing: no
registry write, no feature install, no service start, no file written outside
the transcript you ask for. It is safe to hand to an IT reviewer, and it is
meant to be -- the likely outcome of running it is a conversation with them.

What it does not do is measure exit latency. That needs a compiled probe, it
needs a vCPU to actually run guest code, and the constants involved are worth
more care than a script you are going to run on somebody else's laptop. The
partition test below proves the substrate functions; spike/whp/ measures what
it costs, and its README says how to run it once this one says yes.

Usage:
  whp-corp-probe.ps1 [options]

Options:
  -Output FILE      Transcript destination; - is stdout. [default: -]
  -Json             Emit the findings as JSON instead of a report.
  -NoLiveTest       Read the policy surface only; create no partition.
  -Quiet            Errors only.
  -Version          Print the version and exit.
  -Help             Print this message and exit.

Exit: 0 the substrate is usable here; 1 it is not; 2 usage error;
      3 the probe could not measure (policy blocked it, which is a finding).

The WHP constants below are read from winhvplatformdefs.h rather than recalled:
WHvCapabilityCodeHypervisorPresent = 0x0, WHvCapabilityCodeProcessorVendor =
0x1000, WHvPartitionPropertyCodeProcessorCount = 0x1fff, WHvMapGpaRangeFlag
Read|Write|Execute = 0x1|0x2|0x4.

Run it as:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\whp-corp-probe.ps1
#>

[CmdletBinding()]
param(
    [string] $Output = '-',
    [switch] $Json,
    [switch] $NoLiveTest,
    [switch] $Quiet,
    [switch] $Version,
    [switch] $Help
)

$script:Release = 'whp-corp-probe 1.0'

if ($Help)    { Get-Content $PSCommandPath | Select-Object -First 44 | ForEach-Object { $_ }; exit 0 }
if ($Version) { Write-Output $script:Release; exit 0 }

$ErrorActionPreference = 'Continue'
$f = [ordered]@{}          # findings, key=value, the comparable part
$notes = @()               # human-readable caveats

function Note($m) { if (-not $Quiet) { Write-Host "whp-corp-probe: $m" -ForegroundColor DarkGray } }
function Try-Get($block, $default = 'unknown') {
    try { $v = & $block; if ($null -eq $v -or $v -eq '') { return $default } else { return $v } }
    catch { return $default }
}

# ---------------------------------------------------------------- preflight

# Constrained Language Mode forbids the reflection this probe needs. That is
# not a failure of the probe: an environment that runs PowerShell in CLM is
# telling you something about how it will treat a hypervisor client, and the
# right response is to record it rather than to work around it.
$lang = $ExecutionContext.SessionState.LanguageMode
$f['language_mode'] = $lang
$f['powershell_version'] = $PSVersionTable.PSVersion.ToString()

$isAdmin = Try-Get {
    ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
} $false
$f['running_as_admin'] = if ($isAdmin) { 'yes' } else { 'no' }

# ---------------------------------------------------------------- the host

$os = Try-Get { Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop } $null
$cs = Try-Get { Get-CimInstance -ClassName Win32_ComputerSystem -ErrorAction Stop } $null
$cpu = Try-Get { Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop | Select-Object -First 1 } $null

$build = Try-Get { [int]([Environment]::OSVersion.Version.Build) } 0
$f['windows_caption'] = Try-Get { $os.Caption }
$f['windows_build']   = "$build"
$f['windows_ubr']     = Try-Get {
    (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -Name UBR -ErrorAction Stop).UBR
}
$f['cpu'] = Try-Get { $cpu.Name -replace '\s+', ' ' }

# The project's floor: 1803 (17134) for function, Win11 for certification.
# Below 17134 the placeholder and LX interfaces the rest of the design needs
# are absent, so the hypervisor answer would not save it.
$f['meets_host_floor'] = if ($build -ge 17134) { 'yes' } else { 'no' }

# Home has no Hyper-V and therefore no WHP. Worth naming separately, because
# it is the one blocker IT cannot lift without a licence change.
$edition = Try-Get {
    (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -Name EditionID -ErrorAction Stop).EditionID
}
$f['windows_edition'] = $edition
$f['edition_supports_whp'] = if ($edition -match 'Core|Home') { 'no' } else { 'yes' }

# ------------------------------------------------- is this machine itself a VM

# The common corporate shape is a virtual desktop. WHP inside a guest needs the
# outer hypervisor to expose nested virtualisation, which most VDI fleets do
# not. This is the single most likely reason a corporate answer differs from a
# developer-box answer, so it is called out on its own.
$model = Try-Get { "$($cs.Manufacturer) $($cs.Model)" }
$f['machine_model'] = $model
$isVm = $model -match 'VMware|VirtualBox|Virtual Machine|KVM|QEMU|Xen|Parallels|Hyper-V|Amazon EC2|Google Compute'
$f['machine_is_virtual'] = if ($isVm) { 'yes' } else { 'no' }
if ($isVm) {
    $notes += 'This machine appears to be a virtual desktop. WHP inside a guest requires nested virtualisation from the outer hypervisor; most VDI fleets leave it off, and enabling it is a change to the host farm rather than to this machine.'
}

# HypervisorPresent says the Microsoft hypervisor is running underneath. WHP
# cannot work without it, and it is what the optional feature turns on.
$hvPresent = Try-Get { [bool]$cs.HypervisorPresent } $false
$f['hypervisor_running'] = if ($hvPresent) { 'yes' } else { 'no' }
$f['virtualisation_firmware_enabled'] = Try-Get {
    if ($cpu.VirtualizationFirmwareEnabled) { 'yes' } else { 'no' }
}

# ---------------------------------------------------------------- the policy

# VBS and HVCI run on the same hypervisor WHP needs. Modern builds let them
# coexist; the reason to read them is that a locked-down Device Guard policy is
# the usual sign of a fleet where the optional feature will not be granted, and
# HVCI is separately the thing that refuses the unsigned driver the design
# already ruled out.
$dg = Try-Get {
    Get-CimInstance -Namespace root\Microsoft\Windows\DeviceGuard `
                    -ClassName Win32_DeviceGuard -ErrorAction Stop
} $null
$f['vbs_status'] = Try-Get {
    switch ([int]$dg.VirtualizationBasedSecurityStatus) {
        0 { 'off' } 1 { 'enabled-not-running' } 2 { 'running' } default { 'unknown' }
    }
}
$f['hvci_running'] = Try-Get {
    if ($dg.SecurityServicesRunning -contains 2) { 'yes' } else { 'no' }
}
$f['credential_guard_running'] = Try-Get {
    if ($dg.SecurityServicesRunning -contains 1) { 'yes' } else { 'no' }
}
$f['wdac_policy_enforced'] = Try-Get {
    switch ([int]$dg.CodeIntegrityPolicyEnforcementStatus) {
        0 { 'off' } 1 { 'audit' } 2 { 'enforced' } default { 'unknown' }
    }
}

# An endpoint product that injects into every process is the risk spike (b)
# could not measure: this project's host process deliberately loads no
# kernel32, and an injected DLL that imports it changes that.
$av = Try-Get {
    (Get-CimInstance -Namespace root\SecurityCenter2 -ClassName AntiVirusProduct -ErrorAction Stop |
        Select-Object -ExpandProperty displayName) -join '; '
}
$f['security_products'] = $av
$f['third_party_edr'] = if ($av -and $av -notmatch '^\s*Windows Defender\s*$') { 'yes' } else { 'no' }
if ($f['third_party_edr'] -eq 'yes') {
    $notes += "A third-party endpoint product is present ($av). Spike (b) measured process shape against Windows Defender only, so the ntdll-only host process is unproven against this one. That is a separate test from this probe."
}

$f['applocker_enforced'] = Try-Get {
    $p = Get-AppLockerPolicy -Effective -ErrorAction Stop
    if ($p.RuleCollections | Where-Object { $_.EnforcementMode -eq 'Enabled' }) { 'yes' } else { 'no' }
} 'unknown'

# --------------------------------------------------------- the proxy signals

# The most informative question on a corporate machine is social, not
# technical: does anything else here already use the hypervisor? If WSL2 or
# Docker Desktop runs, the feature is on and the fleet tolerates it.
$f['wsl_present'] = Try-Get {
    if (Get-Command wsl.exe -ErrorAction Stop) { 'yes' } else { 'no' }
} 'no'
# wsl.exe writes UTF-16LE, so the naive pipeline reads it as NUL-separated
# bytes and every match fails -- reporting "no WSL2" on a machine that has it.
# WSL_UTF8 fixes it where supported; stripping the NULs covers where it is not.
$f['wsl2_distro_present'] = Try-Get {
    $prev = $env:WSL_UTF8
    $env:WSL_UTF8 = '1'
    try   { $out = (& wsl.exe -l -v 2>$null | Out-String) -replace "`0", '' }
    finally { $env:WSL_UTF8 = $prev }
    if ($out -match '(?m)^\s*\*?\s*\S+\s+\S+\s+2\s*$') { 'yes' }
    elseif ($out -match '\S') { 'no' } else { 'unknown' }
} 'unknown'
$f['docker_desktop_present'] = Try-Get {
    if (Test-Path "$env:ProgramFiles\Docker\Docker\Docker Desktop.exe") { 'yes' } else { 'no' }
} 'no'

# ------------------------------------------------------------- the live test

# Everything above is inference. This is the measurement: call WHP and see.
$f['live_test_run'] = 'no'
$f['whp_dll_present'] = if (Test-Path "$env:SystemRoot\System32\WinHvPlatform.dll") { 'yes' } else { 'no' }
$f['whp_hypervisor_present'] = 'not-measured'
$f['whp_partition_created'] = 'not-measured'
$f['whp_vcpu_created'] = 'not-measured'
$f['whp_memory_mapped'] = 'not-measured'
$f['whp_last_hresult'] = 'n/a'

function New-WhpBinding {
    # DefinePInvokeMethod rather than Add-Type: it needs no C# compiler and
    # writes no temporary DLL, which matters precisely in the environments this
    # probe exists to characterise. Both are refused under Constrained
    # Language Mode, and that refusal is reported rather than worked around.
    $an  = New-Object System.Reflection.AssemblyName('WhpCorpProbe')
    $asm = [AppDomain]::CurrentDomain.DefineDynamicAssembly(
               $an, [System.Reflection.Emit.AssemblyBuilderAccess]::Run)
    $mod = $asm.DefineDynamicModule('WhpCorpProbeModule')
    $tb  = $mod.DefineType('WhpCorpProbe', 'Public, Class')

    $dll   = 'WinHvPlatform.dll'
    $attrs = [System.Reflection.MethodAttributes] 'Public, Static, PinvokeImpl'
    $conv  = [System.Runtime.InteropServices.CallingConvention]::StdCall
    $chars = [System.Runtime.InteropServices.CharSet]::Ansi

    $defs = @(
        @{ n = 'WHvGetCapability';        a = @([UInt32], [IntPtr], [UInt32], [IntPtr]) },
        @{ n = 'WHvCreatePartition';      a = @([IntPtr]) },
        @{ n = 'WHvSetPartitionProperty'; a = @([IntPtr], [UInt32], [IntPtr], [UInt32]) },
        @{ n = 'WHvSetupPartition';       a = @([IntPtr]) },
        @{ n = 'WHvDeletePartition';      a = @([IntPtr]) },
        @{ n = 'WHvCreateVirtualProcessor';  a = @([IntPtr], [UInt32], [UInt32]) },
        @{ n = 'WHvDeleteVirtualProcessor';  a = @([IntPtr], [UInt32]) },
        @{ n = 'WHvMapGpaRange';          a = @([IntPtr], [IntPtr], [UInt64], [UInt64], [UInt32]) }
    )
    foreach ($d in $defs) {
        $m = $tb.DefinePInvokeMethod($d.n, $dll, $attrs,
                 [System.Reflection.CallingConventions]::Standard,
                 [Int32], $d.a, $conv, $chars)
        $m.SetImplementationFlags(
            $m.GetMethodImplementationFlags() -bor
            [System.Reflection.MethodImplAttributes]::PreserveSig)
    }

    # WHvMapGpaRange requires a page-aligned source, which AllocHGlobal does
    # not promise -- it returns E_INVALIDARG (0x80070057) on a heap pointer
    # that happens to land mid-page, which reads exactly like a refusal by
    # policy and is not one. VirtualAlloc is the allocator the API expects.
    $va = $tb.DefinePInvokeMethod('VirtualAlloc', 'kernel32.dll', $attrs,
              [System.Reflection.CallingConventions]::Standard,
              [IntPtr], @([IntPtr], [UIntPtr], [UInt32], [UInt32]), $conv, $chars)
    $va.SetImplementationFlags(
        $va.GetMethodImplementationFlags() -bor
        [System.Reflection.MethodImplAttributes]::PreserveSig)

    $vf = $tb.DefinePInvokeMethod('VirtualFree', 'kernel32.dll', $attrs,
              [System.Reflection.CallingConventions]::Standard,
              [Int32], @([IntPtr], [UIntPtr], [UInt32]), $conv, $chars)
    $vf.SetImplementationFlags(
        $vf.GetMethodImplementationFlags() -bor
        [System.Reflection.MethodImplAttributes]::PreserveSig)

    $tb.CreateType()
}

$hr = 0
if ($NoLiveTest) {
    $notes += 'The live test was skipped by request; every WHP line above is inference from the policy surface.'
}
elseif ($lang -ne 'FullLanguage') {
    $notes += "PowerShell is in $lang, which forbids the reflection the live test needs. The policy surface above still stands."
}
elseif ($f['whp_dll_present'] -ne 'yes') {
    $notes += 'WinHvPlatform.dll is absent, so the Windows Hypervisor Platform feature has never been installed on this machine.'
}
else {
    try {
        Note 'binding WinHvPlatform.dll'
        $whp = New-WhpBinding
        $f['live_test_run'] = 'yes'

        $buf = [System.Runtime.InteropServices.Marshal]::AllocHGlobal(64)
        $wrt = [System.Runtime.InteropServices.Marshal]::AllocHGlobal(4)
        try {
            Note 'asking whether the hypervisor is present'
            $hr = $whp::WHvGetCapability(0x0, $buf, 4, $wrt)   # HypervisorPresent
            $f['whp_last_hresult'] = ('0x{0:x8}' -f $hr)
            if ($hr -ne 0) {
                $f['whp_hypervisor_present'] = 'call-failed'
            } else {
                $present = [System.Runtime.InteropServices.Marshal]::ReadInt32($buf)
                $f['whp_hypervisor_present'] = if ($present -ne 0) { 'yes' } else { 'no' }
            }

            if ($f['whp_hypervisor_present'] -eq 'yes') {
                Note 'creating a partition'
                $ph = [System.Runtime.InteropServices.Marshal]::AllocHGlobal(8)
                try {
                    $hr = $whp::WHvCreatePartition($ph)
                    $f['whp_last_hresult'] = ('0x{0:x8}' -f $hr)
                    if ($hr -ne 0) {
                        $f['whp_partition_created'] = 'no'
                    } else {
                        $f['whp_partition_created'] = 'yes'
                        $part = [System.Runtime.InteropServices.Marshal]::ReadIntPtr($ph)
                        try {
                            # one vCPU, then set the partition up
                            [System.Runtime.InteropServices.Marshal]::WriteInt32($buf, 1)
                            $hr = $whp::WHvSetPartitionProperty($part, 0x1fff, $buf, 4)
                            if ($hr -eq 0) { $hr = $whp::WHvSetupPartition($part) }
                            $f['whp_last_hresult'] = ('0x{0:x8}' -f $hr)

                            if ($hr -eq 0) {
                                Note 'creating a virtual processor'
                                $hr = $whp::WHvCreateVirtualProcessor($part, 0, 0)
                                $f['whp_vcpu_created'] = if ($hr -eq 0) { 'yes' } else { 'no' }
                                $f['whp_last_hresult'] = ('0x{0:x8}' -f $hr)

                                if ($hr -eq 0) {
                                    Note 'mapping guest memory'
                                    $gpaSize = 0x10000
                                    # MEM_COMMIT|MEM_RESERVE = 0x3000, PAGE_READWRITE = 0x4
                                    $src = $whp::VirtualAlloc(
                                               [IntPtr]::Zero, [UIntPtr]::op_Explicit($gpaSize),
                                               0x3000, 0x4)
                                    if ($src -eq [IntPtr]::Zero) {
                                        $f['whp_memory_mapped'] = 'no-host-alloc'
                                    } else {
                                        try {
                                            # Read|Write|Execute
                                            $hr = $whp::WHvMapGpaRange($part, $src, 0, $gpaSize, 0x7)
                                            $f['whp_memory_mapped'] = if ($hr -eq 0) { 'yes' } else { 'no' }
                                            $f['whp_last_hresult'] = ('0x{0:x8}' -f $hr)
                                        } finally {
                                            # MEM_RELEASE = 0x8000
                                            [void]$whp::VirtualFree($src, [UIntPtr]::Zero, 0x8000)
                                        }
                                    }
                                    [void]$whp::WHvDeleteVirtualProcessor($part, 0)
                                }
                            } else {
                                $f['whp_vcpu_created'] = 'not-reached'
                            }
                        } finally {
                            [void]$whp::WHvDeletePartition($part)
                        }
                    }
                } finally {
                    [System.Runtime.InteropServices.Marshal]::FreeHGlobal($ph)
                }
            }
        } finally {
            [System.Runtime.InteropServices.Marshal]::FreeHGlobal($buf)
            [System.Runtime.InteropServices.Marshal]::FreeHGlobal($wrt)
        }
    }
    catch [System.DllNotFoundException] {
        $notes += 'WinHvPlatform.dll would not load. The Windows Hypervisor Platform feature is not installed.'
        $f['whp_hypervisor_present'] = 'dll-not-found'
    }
    catch {
        $notes += "The live test could not run: $($_.Exception.Message)"
        $f['whp_hypervisor_present'] = 'probe-error'
    }
}

# ---------------------------------------------------------------- the verdict

# Ordered so the first thing that actually stops you is what gets named. A
# verdict is a word; every number above is context and none of it decides.
$finding = 'inconclusive'
$ask = @()

if ($f['edition_supports_whp'] -eq 'no') {
    $finding = 'edition-unsupported'
    $ask += 'This Windows edition has no Hyper-V and therefore no WHP. A licence change is the only route; there is no setting for it.'
}
elseif ($f['meets_host_floor'] -eq 'no') {
    $finding = 'host-below-floor'
    $ask += "Windows build $build is below the project's 1803 floor, and the rest of the design needs interfaces this build does not have."
}
elseif ($f['whp_memory_mapped'] -eq 'yes') {
    $finding = 'whp-usable'
}
elseif ($f['whp_partition_created'] -eq 'yes') {
    $finding = 'whp-partial'
    $ask += "A partition was created but the machine would not carry it through to a mapped vCPU (last HRESULT $($f['whp_last_hresult'])). Worth re-running with the compiled probe before drawing a conclusion."
}
elseif ($f['whp_hypervisor_present'] -eq 'no' -or $f['hypervisor_running'] -eq 'no') {
    if ($isVm) {
        $finding = 'nested-virt-unavailable'
        $ask += 'Ask whether the VDI host can expose nested virtualisation to this desktop. That is a change on the host farm, and in most fleets the answer is no.'
    } else {
        $finding = 'hypervisor-not-running'
        $ask += 'Ask IT to enable the "Windows Hypervisor Platform" optional feature (it needs a reboot). If Virtualization is off in firmware, that is a separate BIOS setting and a separate request.'
    }
}
elseif ($f['whp_hypervisor_present'] -in @('dll-not-found', 'call-failed')) {
    $finding = 'whp-feature-disabled'
    $ask += 'Ask IT to enable the "Windows Hypervisor Platform" optional feature: dism /online /enable-feature /featurename:HypervisorPlatform /all, then reboot.'
}
elseif ($NoLiveTest) {
    # Asking not to measure and being refused the measurement are different
    # results, and only one of them says anything about the fleet.
    $finding = 'not-measured-by-request'
    $ask += 'The live test was skipped, so nothing here is a measurement. Re-run without -NoLiveTest for a verdict.'
}
elseif ($f['live_test_run'] -eq 'no') {
    $finding = 'blocked-cannot-measure'
    $ask += 'The probe was prevented from measuring. In a fleet locked down this far, treat the hypervisor substrate as unavailable until somebody with rights demonstrates otherwise.'
}

if ($finding -eq 'whp-usable' -and $isVm) {
    $notes += 'The substrate works here, but this is a virtual desktop, so it works because nested virtualisation happens to be on. Confirm that is fleet policy rather than this one image before designing around it.'
}
if ($finding -eq 'whp-usable' -and $f['wsl2_distro_present'] -ne 'yes') {
    $notes += 'WHP works here and WSL2 is not installed, which is the combination worth knowing about: the hypervisor is permitted but the Linux userland on top of it is not being used. That is the case where this project has something WSL2 does not.'
}
if ($f['wsl2_distro_present'] -eq 'yes') {
    $notes += 'A WSL2 distro is present. Wherever WSL2 is permitted the hypervisor is already on, and it is worth asking why the project should not simply be WSL2 for that machine -- the answer may be good, but it should be said out loud.'
}

$f['finding'] = $finding

# ---------------------------------------------------------------- the report

$lines = @()
if ($Json) {
    $lines = ($f | ConvertTo-Json -Depth 3)
} else {
    $lines += 'would the hypervisor substrate work on this machine'
    $lines += ''
    $lines += ('host        {0}' -f $env:COMPUTERNAME)
    $lines += ('windows     {0} build {1}' -f $f['windows_caption'], $f['windows_build'])
    $lines += ('edition     {0}' -f $f['windows_edition'])
    $lines += ('cpu         {0}' -f $f['cpu'])
    $lines += ('date        {0}' -f (Get-Date -Format 'yyyy-MM-ddTHH:mm:ssZ'))
    $lines += ('script      {0}' -f $script:Release)
    $lines += ''
    $lines += 'reading'
    $lines += ''
    $lines += ('  machine is a virtual desktop:      {0}' -f $f['machine_is_virtual'])
    $lines += ('  Microsoft hypervisor running:      {0}' -f $f['hypervisor_running'])
    $lines += ('  WHP reports hypervisor present:    {0}' -f $f['whp_hypervisor_present'])
    $lines += ('  partition created:                 {0}' -f $f['whp_partition_created'])
    $lines += ('  virtual processor created:         {0}' -f $f['whp_vcpu_created'])
    $lines += ('  guest memory mapped:               {0}' -f $f['whp_memory_mapped'])
    $lines += ('  virtualisation-based security:     {0}' -f $f['vbs_status'])
    $lines += ('  code-integrity policy:             {0}' -f $f['wdac_policy_enforced'])
    $lines += ('  third-party endpoint product:      {0}' -f $f['third_party_edr'])
    $lines += ('  WSL2 distro present:               {0}' -f $f['wsl2_distro_present'])
    $lines += ('  running as administrator:          {0}' -f $f['running_as_admin'])
    $lines += ''
    $lines += 'raw'
    $lines += ''
    foreach ($k in $f.Keys) { $lines += ('    {0}={1}' -f $k, $f[$k]) }
    if ($notes.Count) {
        $lines += ''
        $lines += 'what this means'
        $lines += ''
        foreach ($n in $notes) { $lines += ('  - ' + $n) }
    }
    if ($ask.Count) {
        $lines += ''
        $lines += 'what to ask for'
        $lines += ''
        foreach ($a in $ask) { $lines += ('  - ' + $a) }
    }
    $lines += ''
    $lines += 'verdict'
    $lines += ''
    $lines += ('    finding={0}' -f $finding)
}

if ($Output -eq '-') { $lines | ForEach-Object { Write-Output $_ } }
else { $lines | Out-File -FilePath $Output -Encoding utf8; Note "transcript written to $Output" }

switch ($finding) {
    'whp-usable'            { exit 0 }
    'blocked-cannot-measure' { exit 3 }
    'inconclusive'          { exit 3 }
    default                 { exit 1 }
}
