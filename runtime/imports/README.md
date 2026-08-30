# The down-call wrappers (WP-21)

Every call the runtime makes down into Windows goes through a generated
`ms_abi` thunk, one per imported function, so the System V side never names
`ntdll` or `kernel32` directly. The list of imports is cut from the built
`cygwin1.dll`'s import table rather than hand-kept, the thunks are generated
from that list, and both the list and the thunks are certified to reproduce
byte for byte. This is the other face of WP-20: that inventory is the outward
surface, the exports; this one is the surface the runtime calls into.

## Source of record

The import table of the built `cygwin1.dll` of the runtime base DR-0007 names,
Cygwin **3.6.10**, at `/c/-/cygwin/root/bin/cygwin1.dll`. The imports do not
appear in `cygwin.din` at all -- that file is the exports -- so the binary is
the only place the resolved set exists. It is read with binutils `objdump -p`,
whose interpreted `.idata` dump carries the DLL name and the imported member
for every entry. The reproduce test pins the DLL by SHA-256
(`d66788fce4ef1ce787fc1a83f2dd1e063e58bbf0d48ad93164ee195a983c035e`); a swapped
or rebuilt binary fails rather than quietly reproducing a different list. A
different base is a new generation, not an edit to these files, and its own
record pointing back at DR-0007.

## The files

`extract-imports.sh` reads the DLL and writes the inventory.
`cygwin-imports.tsv` is what it produced, committed. `gen-wrappers.sh` reads the
inventory and writes the thunks and their header. `wrappers.gen.c` and
`wrappers.gen.h` are what it produced, committed. `audit-imports.sh` is the
link-map audit. `t/reproduce.sh` ties them together and certifies the whole
chain.

## The inventory

Tab-separated, no header, in the DLL's import-table order (KERNEL32 then ntdll,
each alphabetised by the linker):

    dll             symbol              variadic
    KERNEL32.dll    CloseHandle         no
    ntdll.dll       NtClose             no

`variadic` marks a function whose Windows prototype takes a variable argument
list. Such an import cannot be forwarded by a generic thunk -- a System V
`va_list` is a twenty-four-byte descriptor and Microsoft's an eight-byte
pointer, and the thunk's whole correctness rests on repacking nothing -- so a
variadic import is WP-24's, and the generator refuses to emit a plain wrapper
for one. The classifier recognises the documented variadic entry points of the
two DLLs (KERNEL32 exports none; ntdll the `DbgPrint` family and a few
CRT-style formatters). None of them is imported, so every row reads `no`; the
column and the refusal exist so a future base that imports one is caught at
generation rather than at run time.

## The counts

At the pinned DLL:

    total          370
    KERNEL32.dll   238
    ntdll.dll      132
    variadic         0

`extract-imports.sh --terse` prints these. That the down-call surface carries
no variadic function is the finding WP-24 inherits from here: nothing on the
import side hands it a list to unpack.

## The thunk

The runtime is compiled to the System V AMD64 ABI; `ntdll` and `kernel32` are
compiled to the Microsoft x64 ABI. A down-call crosses between them. WP-21's
rule is that the crossing happens in exactly one place per import, a wrapper,
and that the raw import is named nowhere else. A thunk is

    __attribute__((ms_abi)) void w_NtClose(void)
    { __asm__ __volatile__ ("jmp *%0" : : "m"(__imp_NtClose)); }

It receives an MS-ABI call -- the compiler emits the System V to MS shuffle at
the caller's site, from the `ms_abi` prototype the caller declares -- and
tail-jumps through the import's address slot to the real function, MS ABI to MS
ABI, touching neither the arguments nor the stack. Because it repacks nothing it
is correct for any arity and any argument types without the thunk ever knowing
the signature. That the translation lands at the call site rather than inside
the thunk is the deliberate split recorded in DR-0008: the typed prototype
belongs to whoever calls the wrapper, which is WP-22 as it consumes each
import, and WP-21 owns only the mechanical, signature-agnostic forward.

`gen-wrappers.sh` emits it so that it compiles to a load of the slot followed by
an indirect jump, a bare tail jump with no stack frame. gcc 7.4.0 ignores
`__attribute__((naked))` on x86_64, so a frame would be the compiler's to add;
the reproduce test disassembles every thunk and fails if one carries a push, a
pop, a leave, an `%rsp` adjustment, or touches `%rbp`, because a frame breaks
the tail jump. `__imp_<Name>` is the PE name for the import's address-table
slot, filled by the loader at run time; a standalone `-c` compile leaves it an
undefined extern, which is what the audit reads.

## The header

`wrappers.gen.h` declares each `w_<Name>` as an `ms_abi` symbol. It fixes the
set of wrapper symbols and their ABI; it is not the typed call interface, which
the consumer writes per function as it declares the real prototype. The header
is what the audit and the smoke test read to know exactly which symbols must
exist.

## The audit

The done-condition is that no System V-side path calls an import directly.
`audit-imports.sh` checks it against real object files: for each object it reads
the undefined symbols the object still needs and intersects them with the
inventory, counting both the bare name and the `__imp_` slot. Any object other
than the wrappers unit that needs an import symbol is a direct down-call and
fails the audit. Run over the runtime's objects once WP-22 has built them:

    ./audit-imports.sh /path/to/build/*.o

Before the rest of the runtime exists, it runs over the wrappers object alone
and confirms the wrappers are the sole namer of the 370 import slots, which is
the property every later object must preserve. For a final linked image the same
rule reads off the link map -- the only input object contributing references to
the import slots is the wrappers unit -- and the script works on the objects
because `nm` keeps the per-object attribution a stripped image has lost.

## Regenerating and testing

    ./extract-imports.sh -o cygwin-imports.tsv          # re-cut the inventory
    ./gen-wrappers.sh                                    # re-generate the thunks
    ./t/reproduce.sh                                     # certify the whole chain

`t/reproduce.sh` runs five gates: the DLL is the pinned one, the inventory
reproduces, the wrappers reproduce, they compile with no frame, and the audit
sees the imports named only by the wrappers object. The extractor and generator
fail rather than drop or mis-shape a row, so a format change in a future DLL
surfaces as an error at generation rather than as a missing wrapper -- a direct
down-call -- at run time.
