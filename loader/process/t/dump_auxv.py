#!/usr/bin/env python3
# Dump the auxiliary vector a real Linux kernel built for this process, in the
# kernel's own order, reading it from /proc/self/auxv. The values (addresses,
# ids, hwcap of the host) move between runs and machines; what is stable, and
# what the WP-40 differential turns on, is the set of a_type keys the kernel
# emits and the order it emits them in. So the human table carries the values
# for the reader and the "linuxkey=" lines carry the keys for the comparison.
#
# This runs under a real Linux (WSL here); it is the reference the auxv WP-40
# builds is held against. It takes no argument and needs no privilege.
import struct

NAMES = {
    0: "AT_NULL", 1: "AT_IGNORE", 2: "AT_EXECFD", 3: "AT_PHDR",
    4: "AT_PHENT", 5: "AT_PHNUM", 6: "AT_PAGESZ", 7: "AT_BASE",
    8: "AT_FLAGS", 9: "AT_ENTRY", 10: "AT_NOTELF", 11: "AT_UID",
    12: "AT_EUID", 13: "AT_GID", 14: "AT_EGID", 15: "AT_PLATFORM",
    16: "AT_HWCAP", 17: "AT_CLKTCK", 23: "AT_SECURE",
    24: "AT_BASE_PLATFORM", 25: "AT_RANDOM", 26: "AT_HWCAP2",
    27: "AT_RSEQ_FEATURE_SIZE", 28: "AT_RSEQ_ALIGN", 31: "AT_EXECFN",
    33: "AT_SYSINFO_EHDR", 51: "AT_MINSIGSTKSZ",
}


def name(t):
    return NAMES.get(t, "AT_%d" % t)


def main():
    data = open("/proc/self/auxv", "rb").read()
    n = len(data) // 16
    print("== auxv a real Linux kernel built (from /proc/self/auxv)\n")
    for i in range(n):
        t, v = struct.unpack_from("QQ", data, i * 16)
        print("    %-22s 0x%x" % (name(t) + ":", v))
    print()
    for i in range(n):
        t, _ = struct.unpack_from("QQ", data, i * 16)
        print("linuxkey=%s" % name(t))


if __name__ == "__main__":
    main()
