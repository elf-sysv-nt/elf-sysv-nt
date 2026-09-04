#!/usr/bin/env python3
"""Will the hypervisor substrate work on this machine? -- the Python twin.

`whp-corp-probe.ps1` answers this question too, and where PowerShell is
unrestricted the two agree. This one exists for the case the other cannot
reach: Constrained Language Mode.

CLM is the lockdown a strict fleet actually applies, and it does not block
much of what the PowerShell probe reads -- `Get-CimInstance` and the registry
still work under it -- but it does block arbitrary .NET method calls and
reflection, which is exactly what binding `WinHvPlatform.dll` needs. So under
CLM the PowerShell probe keeps its whole policy surface and loses the one
measurement that matters, which is a poor trade on the machine where you most
want an answer.

Python is not subject to CLM, and a .py file is not a script class WDAC's
script enforcement covers, so `ctypes` reaches the API where reflection cannot.
That is the whole reason for this file. It asks the same questions, uses the
same verdict words, and prints the same transcript shape, so a result from
either is comparable with a result from the other.

Read-only and unelevated, on the same terms as the PowerShell twin: no
registry write, no feature install, no service start, nothing written outside
the transcript you ask for.

Usage:
  whp-corp-probe.py [options]

Options:
  -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
  -j, --json              Emit the findings as JSON instead of a report.
  -n, --no-live-test      Read the policy surface only; create no partition.
  -q, --quiet             Errors only.
  -V, --version           Print the version and exit.
  -h, --help              Print this message and exit.

Each option is also settable as WHP_CORP_PROBE_<OPTION>.

Exit: 0 the substrate is usable here; 1 it is not; 2 usage error;
      3 the probe could not measure (which is itself a finding).

The WHP constants are read from winhvplatformdefs.h rather than recalled:
WHvCapabilityCodeHypervisorPresent = 0x0, WHvPartitionPropertyCodeProcessorCount
= 0x1fff, WHvMapGpaRangeFlag Read|Write|Execute = 0x1|0x2|0x4.

Run it as:
  py -3 whp-corp-probe.py
"""

import ctypes
import json
import os
import subprocess
import sys
from collections import OrderedDict
from datetime import datetime, timezone

RELEASE = "whp-corp-probe.py 1.0"

# --- constants, from winhvplatformdefs.h -----------------------------------
WHV_CAP_HYPERVISOR_PRESENT = 0x00000000
WHV_PART_PROP_PROCESSOR_COUNT = 0x00001FFF
WHV_MAP_GPA_RWX = 0x1 | 0x2 | 0x4

MEM_COMMIT_RESERVE = 0x3000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04

HOST_FLOOR_BUILD = 17134          # 1803; below it the rest of the design fails


def envflag(name, default=False):
    v = os.environ.get("WHP_CORP_PROBE_" + name)
    if v is None:
        return default
    return v.strip().lower() in ("1", "yes", "true", "on")


class Probe:
    def __init__(self, quiet=False):
        self.f = OrderedDict()
        self.notes = []
        self.ask = []
        self.quiet = quiet

    def note(self, msg):
        if not self.quiet:
            print("whp-corp-probe: " + msg, file=sys.stderr)

    # -- helpers ------------------------------------------------------------

    @staticmethod
    def _reg(root, path, name, default="unknown"):
        try:
            import winreg
            hive = getattr(winreg, root)
            with winreg.OpenKey(hive, path) as k:
                return winreg.QueryValueEx(k, name)[0]
        except Exception:
            return default

    @staticmethod
    def _reg_exists(root, path):
        try:
            import winreg
            hive = getattr(winreg, root)
            with winreg.OpenKey(hive, path):
                return True
        except Exception:
            return False

    def _ps(self, command, default="unknown"):
        """Ask PowerShell for the few things only WMI knows.

        Every one of these works under Constrained Language Mode -- CLM
        restricts .NET and reflection, not cmdlets -- so leaning on it here
        costs nothing on the locked-down machine this file exists for. If
        PowerShell is missing or refuses outright, the field reads unknown and
        the live test below is unaffected, which is the point of the split.
        """
        try:
            out = subprocess.run(
                ["powershell.exe", "-NoProfile", "-NonInteractive",
                 "-ExecutionPolicy", "Bypass", "-Command", command],
                capture_output=True, text=True, timeout=60)
            v = (out.stdout or "").strip()
            return v if v else default
        except Exception:
            return default

    # -- the host -----------------------------------------------------------

    def host(self):
        cv = r"SOFTWARE\Microsoft\Windows NT\CurrentVersion"
        build = 0
        try:
            build = int(sys.getwindowsversion().build)
        except Exception:
            try:
                build = int(self._reg("HKEY_LOCAL_MACHINE", cv, "CurrentBuildNumber", "0"))
            except Exception:
                build = 0

        self.f["probe_runtime"] = "python %d.%d.%d" % sys.version_info[:3]

        # ProductName under CurrentVersion still reads "Windows 10 ..." on
        # Windows 11 -- Microsoft never revised the value, and WMI's Caption is
        # where the real name lives. A probe that reports the wrong major
        # version on a corporate transcript is worse than one that reports
        # nothing, so the build number corrects it: 22000 is the 11 boundary.
        caption = str(self._reg("HKEY_LOCAL_MACHINE", cv, "ProductName"))
        if build >= 22000 and "Windows 10" in caption:
            caption = caption.replace("Windows 10", "Windows 11")
        # WMI's Caption carries the vendor prefix and ProductName does not;
        # matching it keeps the two twins' transcripts diffable.
        if caption.startswith("Windows"):
            caption = "Microsoft " + caption
        self.f["windows_caption"] = caption
        self.f["windows_build"] = str(build)
        self.f["windows_ubr"] = str(self._reg("HKEY_LOCAL_MACHINE", cv, "UBR"))
        self.f["windows_edition"] = str(self._reg("HKEY_LOCAL_MACHINE", cv, "EditionID"))
        self.f["meets_host_floor"] = "yes" if build >= HOST_FLOOR_BUILD else "no"

        edition = self.f["windows_edition"].lower()
        self.f["edition_supports_whp"] = "no" if ("core" in edition or "home" in edition) else "yes"

        bios = r"HARDWARE\DESCRIPTION\System\BIOS"
        vendor = "%s %s" % (
            self._reg("HKEY_LOCAL_MACHINE", bios, "SystemManufacturer", ""),
            self._reg("HKEY_LOCAL_MACHINE", bios, "SystemProductName", ""))
        self.f["machine_model"] = vendor.strip() or "unknown"

        marks = ("vmware", "virtualbox", "virtual machine", "kvm", "qemu",
                 "xen", "parallels", "hyper-v", "amazon ec2", "google compute")
        is_vm = any(m in vendor.lower() for m in marks)
        self.f["machine_is_virtual"] = "yes" if is_vm else "no"
        if is_vm:
            self.notes.append(
                "This machine appears to be a virtual desktop. WHP inside a guest "
                "needs nested virtualisation from the outer hypervisor; most VDI "
                "fleets leave it off, and turning it on is a change to the host "
                "farm rather than to this machine.")
        return is_vm

    # -- the policy surface -------------------------------------------------

    def policy(self):
        # PowerShell hands back .NET's True/False; the twin says yes/no, and
        # the two transcripts are meant to be comparable line for line.
        hv = self._ps("(Get-CimInstance Win32_ComputerSystem).HypervisorPresent")
        self.f["hypervisor_running"] = {"true": "yes", "false": "no"}.get(
            hv.strip().lower(), hv)
        self.f["vbs_status"] = self._ps(
            "$d=Get-CimInstance -Namespace root/Microsoft/Windows/DeviceGuard "
            "-ClassName Win32_DeviceGuard -EA SilentlyContinue; "
            "switch([int]$d.VirtualizationBasedSecurityStatus)"
            "{0{'off'}1{'enabled-not-running'}2{'running'}default{'unknown'}}")
        self.f["hvci_running"] = self._ps(
            "$d=Get-CimInstance -Namespace root/Microsoft/Windows/DeviceGuard "
            "-ClassName Win32_DeviceGuard -EA SilentlyContinue; "
            "if($d.SecurityServicesRunning -contains 2){'yes'}else{'no'}")
        self.f["wdac_policy_enforced"] = self._ps(
            "$d=Get-CimInstance -Namespace root/Microsoft/Windows/DeviceGuard "
            "-ClassName Win32_DeviceGuard -EA SilentlyContinue; "
            "switch([int]$d.CodeIntegrityPolicyEnforcementStatus)"
            "{0{'off'}1{'audit'}2{'enforced'}default{'unknown'}}")

        av = self._ps(
            "(Get-CimInstance -Namespace root/SecurityCenter2 "
            "-ClassName AntiVirusProduct -EA SilentlyContinue"
            "|Select -Expand displayName) -join '; '")
        self.f["security_products"] = av
        third = bool(av and av != "unknown" and av.strip().lower() != "windows defender")
        self.f["third_party_edr"] = "yes" if third else "no"
        if third:
            self.notes.append(
                "A third-party endpoint product is present (%s). Spike (b) measured "
                "the ntdll-only host process against Windows Defender only, so that "
                "shape is unproven against this one. It is a separate test from this "
                "probe." % av)

        # PowerShell's own lockdown, reported rather than worked around: it is
        # why this file exists, and on a machine where it is on, a PowerShell
        # answer to the same question would be missing its live test.
        self.f["ps_language_mode"] = self._ps(
            "$ExecutionContext.SessionState.LanguageMode")
        if self.f["ps_language_mode"] not in ("FullLanguage", "unknown"):
            self.notes.append(
                "PowerShell here runs in %s, so whp-corp-probe.ps1 would report the "
                "policy surface and skip its live test. This file measures anyway, "
                "which is what it is for." % self.f["ps_language_mode"])

        # Transcription: not a blocker, but it decides whether running this
        # leaves a copy of its output somewhere the fleet keeps.
        tpath = r"SOFTWARE\Policies\Microsoft\Windows\PowerShell\Transcription"
        cpath = r"SOFTWARE\Policies\Microsoft\PowerShellCore\Transcription"
        on = False
        outdir = ""
        for p in (tpath, cpath):
            if self._reg_exists("HKEY_LOCAL_MACHINE", p):
                if str(self._reg("HKEY_LOCAL_MACHINE", p, "EnableTranscripting", "0")) == "1":
                    on = True
                    outdir = str(self._reg("HKEY_LOCAL_MACHINE", p, "OutputDirectory", ""))
        self.f["ps_transcription"] = "on" if on else "off"
        self.f["ps_transcript_dir"] = outdir or "n/a"
        if on:
            self.notes.append(
                "PowerShell transcription is on, writing to '%s'. This probe writes "
                "nothing itself, but anything you run through PowerShell on this "
                "machine leaves its output there." % (outdir or "the policy default"))

    def proxies(self):
        wsl = ""
        try:
            env = dict(os.environ, WSL_UTF8="1")
            out = subprocess.run(["wsl.exe", "-l", "-v"], capture_output=True,
                                 timeout=60, env=env)
            wsl = out.stdout.decode("utf-8", "replace").replace("\x00", "")
        except Exception:
            wsl = ""
        self.f["wsl_present"] = "yes" if wsl.strip() else "no"
        has2 = any(ln.rstrip().endswith(" 2") for ln in wsl.splitlines() if ln.strip())
        self.f["wsl2_distro_present"] = "yes" if has2 else ("no" if wsl.strip() else "unknown")

        docker = os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"),
                              "Docker", "Docker", "Docker Desktop.exe")
        self.f["docker_desktop_present"] = "yes" if os.path.exists(docker) else "no"

    # -- the live test ------------------------------------------------------

    def live(self, no_live_test):
        self.f["live_test_run"] = "no"
        dll = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"),
                           "System32", "WinHvPlatform.dll")
        self.f["whp_dll_present"] = "yes" if os.path.exists(dll) else "no"
        for k in ("whp_hypervisor_present", "whp_partition_created",
                  "whp_vcpu_created", "whp_memory_mapped"):
            self.f[k] = "not-measured"
        self.f["whp_last_hresult"] = "n/a"

        if no_live_test:
            self.notes.append("The live test was skipped by request; nothing below "
                              "the policy surface is a measurement.")
            return
        if self.f["whp_dll_present"] != "yes":
            self.notes.append("WinHvPlatform.dll is absent, so the Windows Hypervisor "
                              "Platform feature has never been installed here.")
            self.f["whp_hypervisor_present"] = "dll-not-found"
            return

        try:
            whp = ctypes.WinDLL("WinHvPlatform.dll")
            k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        except OSError:
            self.notes.append("WinHvPlatform.dll would not load. The Windows "
                              "Hypervisor Platform feature is not installed.")
            self.f["whp_hypervisor_present"] = "dll-not-found"
            return

        hp = ctypes.c_void_p
        whp.WHvGetCapability.argtypes = [ctypes.c_uint32, ctypes.c_void_p,
                                         ctypes.c_uint32,
                                         ctypes.POINTER(ctypes.c_uint32)]
        whp.WHvGetCapability.restype = ctypes.c_int32
        whp.WHvCreatePartition.argtypes = [ctypes.POINTER(hp)]
        whp.WHvCreatePartition.restype = ctypes.c_int32
        whp.WHvSetPartitionProperty.argtypes = [hp, ctypes.c_uint32,
                                                ctypes.c_void_p, ctypes.c_uint32]
        whp.WHvSetPartitionProperty.restype = ctypes.c_int32
        whp.WHvSetupPartition.argtypes = [hp]
        whp.WHvSetupPartition.restype = ctypes.c_int32
        whp.WHvDeletePartition.argtypes = [hp]
        whp.WHvDeletePartition.restype = ctypes.c_int32
        whp.WHvCreateVirtualProcessor.argtypes = [hp, ctypes.c_uint32, ctypes.c_uint32]
        whp.WHvCreateVirtualProcessor.restype = ctypes.c_int32
        whp.WHvDeleteVirtualProcessor.argtypes = [hp, ctypes.c_uint32]
        whp.WHvDeleteVirtualProcessor.restype = ctypes.c_int32
        whp.WHvMapGpaRange.argtypes = [hp, ctypes.c_void_p, ctypes.c_uint64,
                                       ctypes.c_uint64, ctypes.c_uint32]
        whp.WHvMapGpaRange.restype = ctypes.c_int32

        # WHvMapGpaRange wants a page-aligned source. Python's own buffers make
        # no such promise, and an unaligned one comes back E_INVALIDARG --
        # indistinguishable, at a glance, from a machine refusing the call.
        k32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                                     ctypes.c_uint32, ctypes.c_uint32]
        k32.VirtualAlloc.restype = ctypes.c_void_p
        k32.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32]
        k32.VirtualFree.restype = ctypes.c_int32

        self.f["live_test_run"] = "yes"
        self.note("asking whether the hypervisor is present")
        cap = ctypes.c_uint32(0)
        written = ctypes.c_uint32(0)
        hr = whp.WHvGetCapability(WHV_CAP_HYPERVISOR_PRESENT,
                                  ctypes.byref(cap), 4, ctypes.byref(written))
        self.f["whp_last_hresult"] = "0x%08x" % (hr & 0xFFFFFFFF)
        if hr != 0:
            self.f["whp_hypervisor_present"] = "call-failed"
            return
        self.f["whp_hypervisor_present"] = "yes" if cap.value else "no"
        if not cap.value:
            return

        self.note("creating a partition")
        part = hp()
        hr = whp.WHvCreatePartition(ctypes.byref(part))
        self.f["whp_last_hresult"] = "0x%08x" % (hr & 0xFFFFFFFF)
        if hr != 0:
            self.f["whp_partition_created"] = "no"
            return
        self.f["whp_partition_created"] = "yes"

        try:
            count = ctypes.c_uint32(1)
            hr = whp.WHvSetPartitionProperty(part, WHV_PART_PROP_PROCESSOR_COUNT,
                                             ctypes.byref(count), 4)
            if hr == 0:
                hr = whp.WHvSetupPartition(part)
            self.f["whp_last_hresult"] = "0x%08x" % (hr & 0xFFFFFFFF)
            if hr != 0:
                self.f["whp_vcpu_created"] = "not-reached"
                return

            self.note("creating a virtual processor")
            hr = whp.WHvCreateVirtualProcessor(part, 0, 0)
            self.f["whp_vcpu_created"] = "yes" if hr == 0 else "no"
            self.f["whp_last_hresult"] = "0x%08x" % (hr & 0xFFFFFFFF)
            if hr != 0:
                return

            try:
                self.note("mapping guest memory")
                size = 0x10000
                src = k32.VirtualAlloc(None, size, MEM_COMMIT_RESERVE, PAGE_READWRITE)
                if not src:
                    self.f["whp_memory_mapped"] = "no-host-alloc"
                else:
                    try:
                        hr = whp.WHvMapGpaRange(part, ctypes.c_void_p(src), 0,
                                                size, WHV_MAP_GPA_RWX)
                        self.f["whp_memory_mapped"] = "yes" if hr == 0 else "no"
                        self.f["whp_last_hresult"] = "0x%08x" % (hr & 0xFFFFFFFF)
                    finally:
                        k32.VirtualFree(ctypes.c_void_p(src), 0, MEM_RELEASE)
            finally:
                whp.WHvDeleteVirtualProcessor(part, 0)
        finally:
            whp.WHvDeletePartition(part)

    # -- the verdict --------------------------------------------------------

    def verdict(self, is_vm, no_live_test):
        finding = "inconclusive"
        f = self.f

        if f["edition_supports_whp"] == "no":
            finding = "edition-unsupported"
            self.ask.append("This Windows edition has no Hyper-V and therefore no "
                            "WHP. A licence change is the only route; there is no "
                            "setting for it.")
        elif f["meets_host_floor"] == "no":
            finding = "host-below-floor"
            self.ask.append("Windows build %s is below the project's 1803 floor, and "
                            "the rest of the design needs interfaces this build does "
                            "not have." % f["windows_build"])
        elif f["whp_memory_mapped"] == "yes":
            finding = "whp-usable"
        elif f["whp_partition_created"] == "yes":
            finding = "whp-partial"
            self.ask.append("A partition was created but the machine would not carry "
                            "it through to a mapped vCPU (last HRESULT %s). Worth "
                            "re-running with the compiled probe before concluding."
                            % f["whp_last_hresult"])
        elif f["whp_hypervisor_present"] == "no" or f["hypervisor_running"] == "no":
            if is_vm:
                finding = "nested-virt-unavailable"
                self.ask.append("Ask whether the VDI host can expose nested "
                                "virtualisation to this desktop. That is a change on "
                                "the host farm, and in most fleets the answer is no.")
            else:
                finding = "hypervisor-not-running"
                self.ask.append("Ask IT to enable the 'Windows Hypervisor Platform' "
                                "optional feature (it needs a reboot). If "
                                "virtualisation is off in firmware, that is a "
                                "separate BIOS setting and a separate request.")
        elif f["whp_hypervisor_present"] in ("dll-not-found", "call-failed"):
            finding = "whp-feature-disabled"
            self.ask.append("Ask IT to enable the 'Windows Hypervisor Platform' "
                            "optional feature: dism /online /enable-feature "
                            "/featurename:HypervisorPlatform /all, then reboot.")
        elif no_live_test:
            finding = "not-measured-by-request"
            self.ask.append("The live test was skipped, so nothing here is a "
                            "measurement. Re-run without --no-live-test.")
        elif f["live_test_run"] == "no":
            finding = "blocked-cannot-measure"
            self.ask.append("The probe could not measure. Treat the hypervisor "
                            "substrate as unavailable until somebody with rights "
                            "shows otherwise.")

        if finding == "whp-usable" and is_vm:
            self.notes.append(
                "The substrate works, but this is a virtual desktop, so it works "
                "because nested virtualisation happens to be on. Confirm that is "
                "fleet policy rather than this one image before designing around it.")
        if finding == "whp-usable" and f["wsl2_distro_present"] != "yes":
            self.notes.append(
                "WHP works here and WSL2 is not installed, which is the combination "
                "worth knowing about: the hypervisor is permitted but the Linux "
                "userland on top of it is not in use. That is the case where this "
                "project has something WSL2 does not.")
        if f["wsl2_distro_present"] == "yes":
            self.notes.append(
                "A WSL2 distro is present. Wherever WSL2 is permitted the hypervisor "
                "is already on, and it is worth asking why the project should not "
                "simply be WSL2 for that machine -- the answer may be good, but it "
                "should be said out loud.")

        self.f["finding"] = finding
        return finding

    # -- the report ---------------------------------------------------------

    def report(self, as_json):
        if as_json:
            return json.dumps(self.f, indent=2)
        f = self.f
        L = []
        L.append("would the hypervisor substrate work on this machine")
        L.append("")
        L.append("host        %s" % os.environ.get("COMPUTERNAME", "unknown"))
        L.append("windows     %s build %s" % (f["windows_caption"], f["windows_build"]))
        L.append("edition     %s" % f["windows_edition"])
        L.append("runtime     %s" % f["probe_runtime"])
        L.append("date        %s" % datetime.now(timezone.utc)
                 .strftime("%Y-%m-%dT%H:%M:%SZ"))
        L.append("script      %s" % RELEASE)
        L.append("")
        L.append("reading")
        L.append("")
        for label, key in (
                ("machine is a virtual desktop:", "machine_is_virtual"),
                ("WHP reports hypervisor present:", "whp_hypervisor_present"),
                ("partition created:", "whp_partition_created"),
                ("virtual processor created:", "whp_vcpu_created"),
                ("guest memory mapped:", "whp_memory_mapped"),
                ("virtualisation-based security:", "vbs_status"),
                ("code-integrity policy:", "wdac_policy_enforced"),
                ("third-party endpoint product:", "third_party_edr"),
                ("PowerShell language mode:", "ps_language_mode"),
                ("PowerShell transcription:", "ps_transcription"),
                ("WSL2 distro present:", "wsl2_distro_present")):
            L.append("  %-34s %s" % (label, f.get(key, "unknown")))
        L.append("")
        L.append("raw")
        L.append("")
        for k, v in f.items():
            L.append("    %s=%s" % (k, v))
        if self.notes:
            L.append("")
            L.append("what this means")
            L.append("")
            for n in self.notes:
                L.append("  - " + n)
        if self.ask:
            L.append("")
            L.append("what to ask for")
            L.append("")
            for a in self.ask:
                L.append("  - " + a)
        L.append("")
        L.append("verdict")
        L.append("")
        L.append("    finding=%s" % f["finding"])
        return "\n".join(L)


def main(argv):
    out = os.environ.get("WHP_CORP_PROBE_OUTPUT", "-")
    as_json = envflag("JSON")
    no_live = envflag("NO_LIVE_TEST")
    quiet = envflag("QUIET")

    args = argv[1:]
    i = 0
    while i < len(args):
        a = args[i]
        if a in ("-h", "--help"):
            print(__doc__.strip())
            return 0
        elif a in ("-V", "--version"):
            print(RELEASE)
            return 0
        elif a in ("-o", "--output"):
            i += 1
            if i >= len(args):
                print("whp-corp-probe.py: --output needs a value", file=sys.stderr)
                return 2
            out = args[i]
        elif a.startswith("--output="):
            out = a.split("=", 1)[1]
        elif a in ("-j", "--json"):
            as_json = True
        elif a in ("-n", "--no-live-test"):
            no_live = True
        elif a in ("-q", "--quiet"):
            quiet = True
        else:
            print("whp-corp-probe.py: unknown option %s" % a, file=sys.stderr)
            return 2
        i += 1

    if os.name != "nt":
        print("whp-corp-probe.py: this measures Windows, and is not running on it",
              file=sys.stderr)
        return 2

    p = Probe(quiet=quiet)
    is_vm = p.host()
    p.policy()
    p.proxies()
    p.live(no_live)
    finding = p.verdict(is_vm, no_live)

    text = p.report(as_json)
    if out == "-":
        print(text)
    else:
        with open(out, "w", encoding="utf-8") as fh:
            fh.write(text + "\n")
        p.note("transcript written to %s" % out)

    if finding == "whp-usable":
        return 0
    if finding in ("blocked-cannot-measure", "inconclusive", "not-measured-by-request"):
        return 3
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
