/* bitmap-probe: can a process reserve TlsSlots[63] for itself by setting bit
 * 63 of the PEB's TlsBitmap, so that no TlsAlloc, from any DLL loaded later,
 * ever hands that slot out?
 *
 * Carrier C1 of spike 6 reads the thread pointer at a fixed TlsSlots index,
 * one load through %gs. DR-0003 declined it because TlsAlloc draws from the
 * same 64 bits and an injected DLL could take the slot. The bitmap TlsAlloc
 * consults is in the PEB, which the process owns; if setting the bit is
 * enough, the hazard is not reduced but removed. This measures that.
 *
 * Native, built with x86_64-w64-mingw32-gcc. The offsets are the x64 TEB and
 * PEB layouts as shipped since Vista: TEB+0x60 the PEB pointer, PEB+0x78 the
 * RTL_BITMAP pointer for TlsBitmap, PEB+0x80 its two words of bits,
 * TEB+0x1480 the TlsSlots array.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

#define RELEASE "bitmap-probe 1.0"
#define SLOT 63

static void emit(const char *k, const char *fmt, ...)
{
	va_list ap;
	printf("%s=", k);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

typedef struct { ULONG SizeOfBitMap; PULONG Buffer; } RTL_BITMAP_X;
typedef VOID (NTAPI *fn_RtlSetBit)(RTL_BITMAP_X *, ULONG);
typedef BOOLEAN (NTAPI *fn_RtlAreBitsSet)(RTL_BITMAP_X *, ULONG, ULONG);

static uint8_t *teb(void)
{
	uint8_t *t;
	__asm__ __volatile__("movq %%gs:0x30, %0" : "=r"(t));
	return t;
}

static uint64_t slot_via_gs(void)
{
	uint64_t v;
	__asm__ __volatile__("movq %%gs:%c1, %0" : "=r"(v) : "i"(0x1480 + 8 * SLOT));
	return v;
}

static DWORD WINAPI reader(LPVOID p) { *(volatile uint64_t *) p = slot_via_gs(); return 0; }

static int lowest_free(uint64_t bits)
{
	int i;
	for (i = 0; i < 64; i++)
		if (!((bits >> i) & 1)) return i;
	return -1;
}

int main(int argc, char **argv)
{
	uint8_t *peb = *(uint8_t **)(teb() + 0x60);
	RTL_BITMAP_X *bm = *(RTL_BITMAP_X **)(peb + 0x78);
	uint64_t bits_before, bits_after, bits_final;
	HMODULE nt = GetModuleHandleW(L"ntdll.dll");
	fn_RtlSetBit p_RtlSetBit = (fn_RtlSetBit)(void *) GetProcAddress(nt, "RtlSetBit");
	fn_RtlAreBitsSet p_RtlAreBitsSet = (fn_RtlAreBitsSet)(void *) GetProcAddress(nt, "RtlAreBitsSet");
	DWORD got[80];
	int i, n, hit63 = 0, hit63_after_dll = 0, alloc_failed = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--version")) { puts(RELEASE); return 0; }
		fprintf(stderr, "bitmap-probe: unknown argument %s\n", argv[i]);
		return 2;
	}
	setvbuf(stdout, NULL, _IOLBF, 0);

	/* q1. Where the bitmap is, and what the process starts with. */
	emit("q1_bitmap_size", "%lu", (unsigned long) bm->SizeOfBitMap);
	emit("q1_bitmap_buffer_is_peb_0x80", "%d", (uint8_t *) bm->Buffer == peb + 0x80);
	memcpy(&bits_before, bm->Buffer, 8);
	emit("q1_bits_at_start", "0x%016llx", (unsigned long long) bits_before);
	emit("q1_lowest_free_at_start", "%d", lowest_free(bits_before));
	emit("q1_bit63_at_start", "%d", (int) ((bits_before >> SLOT) & 1));

	/* q2. Reserve the slot: one bit, through the export the loader itself uses. */
	if (!p_RtlSetBit) { emit("q2_rtlsetbit", "absent"); return 1; }
	p_RtlSetBit(bm, SLOT);
	memcpy(&bits_after, bm->Buffer, 8);
	emit("q2_bit63_after_set", "%d", (int) ((bits_after >> SLOT) & 1));
	emit("q2_arebitsset", "%d", p_RtlAreBitsSet ? (int) p_RtlAreBitsSet(bm, SLOT, 1) : -1);

	/* q3. Seventy TlsAllocs: enough to exhaust the 64 primary slots and spill
	 * into the expansion slots. None may return 63. */
	for (n = 0; n < 70; n++) {
		got[n] = TlsAlloc();
		if (got[n] == TLS_OUT_OF_INDEXES) { alloc_failed++; got[n] = 0xffff; continue; }
		if (got[n] == SLOT) hit63++;
	}
	emit("q3_allocs", "%d", n);
	emit("q3_alloc_failures", "%d", alloc_failed);
	emit("q3_slot63_handed_out", "%d", hit63);
	{
		int maxi = -1, primary = 0, expansion = 0;
		for (i = 0; i < n; i++) {
			if (got[i] == 0xffff) continue;
			if ((int) got[i] > maxi) maxi = (int) got[i];
			if (got[i] < 64) primary++; else expansion++;
		}
		emit("q3_highest_index", "%d", maxi);
		emit("q3_primary_slots_taken", "%d", primary);
		emit("q3_expansion_slots_taken", "%d", expansion);
	}
	memcpy(&bits_final, bm->Buffer, 8);
	emit("q3_bits_after_allocs", "0x%016llx", (unsigned long long) bits_final);
	emit("q3_bit63_still_set", "%d", (int) ((bits_final >> SLOT) & 1));

	/* q4. The slot works as a carrier regardless of the bitmap: TlsSetValue
	 * and the raw %gs read agree, and a value written through %gs is what
	 * TlsGetValue returns. */
	{
		void *want = (void *) 0x00c0ffee12345678ULL;
		uint64_t raw;
		int set_ok = TlsSetValue(SLOT, want);
		raw = slot_via_gs();
		emit("q4_tlssetvalue_63_ok", "%d", set_ok);
		emit("q4_gs_read_matches", "%d", raw == (uint64_t)(uintptr_t) want);
		*(volatile uint64_t *)(teb() + 0x1480 + 8 * SLOT) = 0x1122334455667788ULL;
		emit("q4_tlsgetvalue_sees_raw_write", "%d", TlsGetValue(SLOT) == (void *) 0x1122334455667788ULL);
	}

	/* q5. A DLL loaded after the reservation, then more TlsAllocs: still never 63. */
	{
		HMODULE m = LoadLibraryW(L"version.dll");
		emit("q5_dll_loaded", "%d", m != NULL);
		for (i = 0; i < 10; i++) {
			DWORD x = TlsAlloc();
			if (x == SLOT) hit63_after_dll++;
		}
		emit("q5_slot63_handed_out_after_dll", "%d", hit63_after_dll);
	}

	/* q6. A new thread: the slot starts at zero there, as every carrier did in
	 * spike 6, so tp_set has to write it before the thread runs user code. */
	{
		volatile uint64_t seen = 0xffffffffffffffffULL;
		HANDLE h = CreateThread(NULL, 0, reader, (void *) &seen, 0, NULL);
		WaitForSingleObject(h, 5000);
		emit("q6_new_thread_slot_at_start", "0x%llx", (unsigned long long) seen);
	}
	return 0;
}
