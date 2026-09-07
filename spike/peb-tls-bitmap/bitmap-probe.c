/* bitmap-probe: can a process reserve TlsSlots[63] for itself by setting bit
 * 63 of the PEB's TlsBitmap, so that no TlsAlloc, from any DLL loaded later,
 * ever hands that slot out? And, since 0011 § 6 puts the canary and the
 * pointer guard at fixed offsets from the thread pointer, is there room for
 * them above TlsSlots[63], or must they take the two slots below it?
 *
 * Carrier C1 of spike 6 reads the thread pointer at a fixed TlsSlots index,
 * one load through %gs. DR-0003 declined it because TlsAlloc draws from the
 * same 64 bits and an injected DLL could take the slot. The bitmap TlsAlloc
 * consults is in the PEB, which the process owns; if setting the bit is
 * enough, the hazard is not reduced but removed. q1 to q6 measure that.
 *
 * DR-0101 then wrote the ABI as `%gs:TP`, `%gs:TP+8`, `%gs:TP+16` with TP =
 * 0x1678, which places two of the three words past the end of the array. q7
 * to q9 measure where the array ends and whether the two slots below the
 * thread pointer serve instead, which is what the glibc port assumed.
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

#define RELEASE "bitmap-probe 1.1"
#define SLOT 63
#define SLOT_CANARY 62
#define SLOT_GUARD 61

#define TLSSLOTS 0x1480
#define OFF_TP (TLSSLOTS + 8 * SLOT)		/* 0x1678 */
#define OFF_CANARY (TLSSLOTS + 8 * SLOT_CANARY)	/* 0x1670 */
#define OFF_GUARD (TLSSLOTS + 8 * SLOT_GUARD)	/* 0x1668 */

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
	__asm__ __volatile__("movq %%gs:%c1, %0" : "=r"(v) : "i"(OFF_TP));
	return v;
}

static uint64_t canary_via_gs(void)
{
	uint64_t v;
	__asm__ __volatile__("movq %%gs:%c1, %0" : "=r"(v) : "i"(OFF_CANARY));
	return v;
}

static uint64_t guard_via_gs(void)
{
	uint64_t v;
	__asm__ __volatile__("movq %%gs:%c1, %0" : "=r"(v) : "i"(OFF_GUARD));
	return v;
}

static DWORD WINAPI reader(LPVOID p) { *(volatile uint64_t *) p = slot_via_gs(); return 0; }

struct three { uint64_t tp, canary, guard; };
static DWORD WINAPI reader3(LPVOID p)
{
	struct three *t = p;
	t->tp = slot_via_gs();
	t->canary = canary_via_gs();
	t->guard = guard_via_gs();
	return 0;
}

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
	DWORD held[96];
	int nheld = 0;

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
	emit("q1_bit62_at_start", "%d", (int) ((bits_before >> SLOT_CANARY) & 1));
	emit("q1_bit61_at_start", "%d", (int) ((bits_before >> SLOT_GUARD) & 1));

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
		if (nheld < 96) held[nheld++] = got[n];
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
		*(volatile uint64_t *)(teb() + OFF_TP) = 0x1122334455667788ULL;
		emit("q4_tlsgetvalue_sees_raw_write", "%d", TlsGetValue(SLOT) == (void *) 0x1122334455667788ULL);
	}

	/* q5. A DLL loaded after the reservation, then more TlsAllocs: still never 63. */
	{
		HMODULE m = LoadLibraryW(L"version.dll");
		emit("q5_dll_loaded", "%d", m != NULL);
		for (i = 0; i < 10; i++) {
			DWORD x = TlsAlloc();
			if (x == SLOT) hit63_after_dll++;
			if (x != TLS_OUT_OF_INDEXES && nheld < 96) held[nheld++] = x;
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

	/* q7. Where the array ends. TlsSetValue writes TlsSlots[i]; find the byte
	 * offset a written index lands at, and ask whether TP+8 and TP+16 -- the
	 * two words DR-0101's ABI names -- are inside the array at all. Slot 63
	 * is the last, so TP+8 is the first byte past it. */
	{
		uint64_t magic = 0xfeedfacecafe0000ULL;
		int off_63 = -1, off_0 = -1, any_at_tp8 = 0, any_at_tp16 = 0;
		uint8_t *t = teb();

		TlsSetValue(SLOT, (void *)(uintptr_t)(magic | 63));
		for (i = 0; i < 0x40 * 8; i += 8)
			if (*(volatile uint64_t *)(t + TLSSLOTS + i) == (magic | 63)) { off_63 = TLSSLOTS + i; break; }
		emit("q7_slot63_offset", "0x%x", off_63);
		emit("q7_slot63_offset_is_0x1678", "%d", off_63 == OFF_TP);

		/* Slot 0 anchors the array's base, so the stride and the base are
		 * both measured rather than assumed. */
		{
			DWORD z = 0;
			void *save = TlsGetValue(z);
			TlsSetValue(z, (void *)(uintptr_t)(magic | 0xa0));
			for (i = 0; i < 0x40 * 8; i += 8)
				if (*(volatile uint64_t *)(t + TLSSLOTS + i) == (magic | 0xa0)) { off_0 = TLSSLOTS + i; break; }
			TlsSetValue(z, save);
		}
		emit("q7_slot0_offset", "0x%x", off_0);
		emit("q7_array_span_bytes", "%d", (off_63 >= 0 && off_0 >= 0) ? off_63 + 8 - off_0 : -1);
		emit("q7_first_byte_past_array", "0x%x", off_63 >= 0 ? off_63 + 8 : -1);

		/* Every index the process holds, written with a distinct magic: does
		 * any of them land on TP+8 or TP+16? */
		for (i = 0; i < nheld; i++) {
			uint64_t m = magic | (uint64_t)(0x100 + i);
			TlsSetValue(held[i], (void *)(uintptr_t) m);
			if (*(volatile uint64_t *)(t + OFF_TP + 8) == m) any_at_tp8 = 1;
			if (*(volatile uint64_t *)(t + OFF_TP + 16) == m) any_at_tp16 = 1;
		}
		emit("q7_indices_written", "%d", nheld);
		emit("q7_a_slot_lands_on_tp_plus_8", "%d", any_at_tp8);
		emit("q7_a_slot_lands_on_tp_plus_16", "%d", any_at_tp16);
	}

	/* q8. The two slots below the thread pointer, reserved the same way. The
	 * indices q3 and q5 took are released first, so the allocator has the
	 * whole array to hand out again and the reservation is what keeps it off
	 * 61, 62 and 63 rather than exhaustion. */
	{
		int hit = 0, failed = 0, prim = 0, exp = 0, after_dll = 0;
		uint64_t bits3;
		for (i = 0; i < nheld; i++) TlsFree(held[i]);
		nheld = 0;
		p_RtlSetBit(bm, SLOT_CANARY);
		p_RtlSetBit(bm, SLOT_GUARD);
		memcpy(&bits3, bm->Buffer, 8);
		emit("q8_bit62_after_set", "%d", (int) ((bits3 >> SLOT_CANARY) & 1));
		emit("q8_bit61_after_set", "%d", (int) ((bits3 >> SLOT_GUARD) & 1));
		emit("q8_arebitsset_61_3", "%d", p_RtlAreBitsSet ? (int) p_RtlAreBitsSet(bm, SLOT_GUARD, 3) : -1);
		for (n = 0; n < 70; n++) {
			DWORD x = TlsAlloc();
			if (x == TLS_OUT_OF_INDEXES) { failed++; continue; }
			if (x == SLOT || x == SLOT_CANARY || x == SLOT_GUARD) hit++;
			if (x < 64) prim++; else exp++;
			if (nheld < 96) held[nheld++] = x;
		}
		emit("q8_allocs", "%d", n);
		emit("q8_alloc_failures", "%d", failed);
		emit("q8_reserved_slots_handed_out", "%d", hit);
		emit("q8_primary_slots_taken", "%d", prim);
		emit("q8_expansion_slots_taken", "%d", exp);
		{
			HMODULE m = LoadLibraryW(L"winmm.dll");
			emit("q8_dll_loaded", "%d", m != NULL);
			for (i = 0; i < 10; i++) {
				DWORD x = TlsAlloc();
				if (x == SLOT || x == SLOT_CANARY || x == SLOT_GUARD) after_dll++;
			}
		}
		emit("q8_reserved_slots_handed_out_after_dll", "%d", after_dll);
		memcpy(&bits3, bm->Buffer, 8);
		emit("q8_bits61_63_still_set", "%d",
			(int) (((bits3 >> SLOT_GUARD) & 7) == 7));
	}

	/* q9. The three words as the ABI uses them: one %gs load each, at
	 * TP, TP-8 and TP-16, agreeing with TlsGetValue on the same indices, and
	 * all three starting at zero on a new thread. */
	{
		uint64_t tp = 0x00c0ffee00000063ULL, can = 0x00c0ffee00000062ULL, grd = 0x00c0ffee00000061ULL;
		struct three seen = { 0xffffffffffffffffULL, 0xffffffffffffffffULL, 0xffffffffffffffffULL };
		HANDLE h;
		TlsSetValue(SLOT, (void *)(uintptr_t) tp);
		TlsSetValue(SLOT_CANARY, (void *)(uintptr_t) can);
		TlsSetValue(SLOT_GUARD, (void *)(uintptr_t) grd);
		emit("q9_gs_tp_matches", "%d", slot_via_gs() == tp);
		emit("q9_gs_canary_matches", "%d", canary_via_gs() == can);
		emit("q9_gs_guard_matches", "%d", guard_via_gs() == grd);
		emit("q9_canary_is_tp_minus_8", "%d", OFF_CANARY == OFF_TP - 8);
		emit("q9_guard_is_tp_minus_16", "%d", OFF_GUARD == OFF_TP - 16);
		*(volatile uint64_t *)(teb() + OFF_CANARY) = 0x5555aaaa5555aaaaULL;
		emit("q9_tlsgetvalue_62_sees_raw_write", "%d",
			TlsGetValue(SLOT_CANARY) == (void *) 0x5555aaaa5555aaaaULL);
		h = CreateThread(NULL, 0, reader3, &seen, 0, NULL);
		WaitForSingleObject(h, 5000);
		emit("q9_new_thread_tp", "0x%llx", (unsigned long long) seen.tp);
		emit("q9_new_thread_canary", "0x%llx", (unsigned long long) seen.canary);
		emit("q9_new_thread_guard", "0x%llx", (unsigned long long) seen.guard);
	}
	return 0;
}
