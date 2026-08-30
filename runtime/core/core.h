/*
 * core.h -- the host-facing core of elfsysv1.dll (WP-22).
 *
 * These are the entry points Windows calls into the runtime: the DLL entry,
 * thread starts, APCs, PE TLS callbacks, vectored exception handlers, and the
 * fault path. Each is Microsoft x64 by the host's rule and carries SEH unwind
 * data the host recognizes, because Cygwin's own exception and signal delivery
 * ride Windows SEH and its MS-format unwind records. Below each entry the
 * runtime is System V; the convention change happens here and nowhere above.
 *
 * The direction this file covers is Windows calling in. The opposite
 * direction -- a System V function pointer the ELF world hands down to Windows
 * -- is WP-23's callback trampoline, and the seam where it attaches is marked
 * ELFSYSV_WP23_SEAM below rather than filled here.
 *
 * Everything in this unit compiles -mno-red-zone. The boundary between the two
 * halves is invisible to the compiler, so the flag is not partially
 * applicable; DR-0006 records it as scaffolding carried until the delivery-site
 * repair (WP-43) lands.
 *
 * The unwind contract, measured on x86_64-pc-cygwin gcc 7.4.0 and recorded in
 * DR-0012:
 *   - gcc emits .pdata/.xdata for an ms_abi function, and the host's
 *     RtlLookupFunctionEntry finds a RUNTIME_FUNCTION for it. A host-facing
 *     entry point is therefore ms_abi, never sysv_abi.
 *   - gcc emits no unwind record for a sysv_abi function, so the host treats a
 *     System V frame as a leaf and cannot walk it. That is the seam, not a
 *     defect: no host unwinder may be asked to cross a System V frame. The
 *     fault path reaches the runtime through Cygwin's delivery, which restores
 *     a saved context rather than unwinding the intervening System V frames.
 */

#ifndef ELFSYSV_RUNTIME_CORE_H
#define ELFSYSV_RUNTIME_CORE_H

#include <stdint.h>

/*
 * The two convention attributes, named once. MSABI is the host-facing side and
 * carries unwind data; SYSV is the runtime side and does not. noinline keeps a
 * crossing a crossing: an inlined entry point would dissolve the seam the whole
 * unit exists to hold.
 */
#define ELFSYSV_MSABI __attribute__((ms_abi, noinline))
#define ELFSYSV_SYSV  __attribute__((sysv_abi, noinline))

/*
 * A down-call into the ELF/System V world. At one function's width this is a
 * direct typed call: the compiler emits the MS-to-System-V argument shuffle at
 * the call site from the sysv_abi prototype, exactly as spike 3 measured the
 * callee-saved sets surviving it. When WP-23 exists, an entry point that must
 * forward an arbitrary caller-supplied pointer rather than call a known runtime
 * function routes through its trampoline instead; that attachment point is
 * ELFSYSV_WP23_SEAM.
 */
#define ELFSYSV_WP23_SEAM /* handed to WP-23; see README "The trampoline seam" */

/* ---- the System V cores the entry points reach one frame down ---------- */
/* Stand-ins for real runtime bodies, which do not exist until the loader and
 * the rest of phase 2 do. Each is System V so the crossing is real. */
ELFSYSV_SYSV uint64_t elfsysv_core_run(uint64_t token);

/* Observation seam for the stand-in core: how many times a core ran and the
 * last token it saw. A real runtime body has its own observable work and needs
 * neither. Documented in README under "The stand-in cores". */
extern volatile uint64_t elfsysv_core_calls;
extern volatile uint64_t elfsysv_core_last_token;

/* ---- the host-facing entry points -------------------------------------- */

/* The DLL entry. Shape only until a DLL is linked (WP-41); the loader fires a
 * re-faced DLL's entry before any of this unit's machinery is mapped, so this
 * is certified at function width and its firing-by-the-loader is deferred. */
ELFSYSV_MSABI int32_t elfsysv_dllmain(void *hinst, uint32_t reason, void *reserved);

/* A thread entry point: DWORD WINAPI (LPVOID). */
ELFSYSV_MSABI uint32_t elfsysv_thread_entry(void *arg);

/* A queued APC: VOID CALLBACK (ULONG_PTR). */
ELFSYSV_MSABI void elfsysv_apc(uint64_t arg);

/* A PE TLS callback: VOID (PVOID, DWORD, PVOID). Shape and ABI certified here;
 * registration in the PE TLS directory and firing by the loader is deferred to
 * WP-41, which builds the DLL that carries the directory. */
ELFSYSV_MSABI void elfsysv_tls_callback(void *dll, uint32_t reason, void *reserved);

/* A vectored exception handler: LONG CALLBACK (EXCEPTION_POINTERS *). The
 * argument is opaque here so core.h needs no windows.h; entry.c casts it. */
ELFSYSV_MSABI int32_t elfsysv_veh(void *exception_pointers);

/* The signal-handler entry. Cygwin delivers by hijacking the thread and
 * calling a handler; this is the MS-ABI landing that reaches the runtime. The
 * full siginfo_t/ucontext_t frame and the red-zone reservation are WP-43's;
 * this proves the landing reaches the runtime and returns. */
ELFSYSV_MSABI void elfsysv_signal_entry(int signo);

/*
 * A runtime self-check: does the host recognize unwind data for the code at
 * pc? Returns 1 when RtlLookupFunctionEntry finds a RUNTIME_FUNCTION, 0 when it
 * does not. The core uses it to assert its own entry points are walkable and
 * its System V cores are, by design, not.
 */
int elfsysv_core_unwind_present(void *pc);

#endif /* ELFSYSV_RUNTIME_CORE_H */
