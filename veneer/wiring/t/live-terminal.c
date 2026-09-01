/*
 * live-terminal -- WP-56's twelfth live crossing, and the second whose
 * finding is negative: the bind loop resolves the terminal slice's real
 * table against a real elfsysv1.dll, and the slice's three NOSIGFE forwards
 * -- cfgetispeed, cfgetospeed, cfmakeraw -- reach the real body. Two of them
 * read a `struct termios` at an offset the body owns and the el8 face does
 * not share: the body's `NCCS` is eighteen, the face's is thirty-two, so the
 * speed fields that follow `c_cc` sit fourteen bytes apart. The cf-speed rows
 * do not cross as value-preserving forwards the way string's and misc's did.
 *
 * The terminal slice is wire-terminal.gen.c: thirty rows, one shim (ioctl),
 * the rest forwards. Its bind check is math's, stdlib's, sockets's, posix's,
 * time's, misc's and wchar's shape -- every row must resolve, missing == 0 --
 * and it holds: every terminal name is exported by the real DLL, so a
 * resolved thunk reaches the real body. What the earlier crossings then went
 * on to confirm, and this one instead refutes for the speed rows, is that a
 * resolved terminal row crosses as a plain tail jump. For a row that reads a
 * caller-owned `struct termios`, it does not.
 *
 * cfgetispeed (w00000), cfgetospeed (w00001) and cfmakeraw (w00002; see
 * wire-terminal.gen.s for the index -> name mapping) are the rows this
 * specimen calls -- the three the slice marks NOSIGFE, standing on no reent,
 * locale, table or kernel, exactly the freestanding shape every earlier
 * crossing chose. So the thunk reaches the body cleanly. The body then reads
 * or writes the caller's `struct termios` by its own idea of where the fields
 * lie, and that idea is the finding.
 *
 * The two layouts, measured with each side's own headers:
 *
 *            c_iflag c_oflag c_cflag c_lflag c_line c_cc  NCCS  c_ispeed c_ospeed size
 *   face(el8)   0       4       8      12      16    17    32      52       56     60
 *   body(cyg)   0       4       8      12      16    17    18      36       40     44
 *
 * The leading flag words and the start of `c_cc` share their offsets exactly;
 * the divergence is `NCCS`, which makes `c_cc` fourteen bytes longer at the
 * face and pushes `c_ispeed`/`c_ospeed` from 36/40 in the body to 52/56 in the
 * face. cfgetispeed and cfgetospeed in the body are plain field reads --
 * `return t->c_ispeed;`, measured on Cygwin -- so called on a face-laid struct
 * they read offset 36/40, the body's field, not 52/56, the face's. A caller
 * that filled the face field hands the body a struct whose speed the body
 * reads from the wrong place.
 *
 * cfmakeraw, by contrast, touches only the leading flag words the two layouts
 * share (and `c_cc[VMIN]`/`c_cc[VTIME]`, which start at the shared `c_cc`),
 * so it crosses value-preserving on that shared region: from an all-ones
 * struct the body leaves c_iflag=0xfffefa1c, c_oflag=0xfffffffe,
 * c_cflag=0xfffffeff, c_lflag=0xfffffed8, measured on Cygwin, and a face-laid
 * struct reads those back at the same offsets. The crossing thus pins both
 * halves of the finding: the shared leading region crosses, the speed tail
 * does not.
 *
 * Every buffer is the specimen's own byte array, laid out by hand to the
 * face's offsets and never a libc `struct termios`, so what the checks read
 * is the body's own placement against the face's and not a header the two
 * sides lay out for the compiler differently.
 *
 * Reports one bit per check through the terminator the stub puts in %rdx, so
 * 31 is the only passing status (five checks), a pass meaning the negative
 * finding holds and reproduces:
 *
 *   0x01  the bind resolved every row of the terminal table (missing 0);
 *         every terminal name is exported by the real DLL
 *   0x02  cfgetispeed reaches the real body and reads the body's c_ispeed at
 *         offset 36 -- returning the value placed there, not the one at the
 *         face's offset 52
 *   0x04  cfgetospeed reproduces the divergence on the output speed: it reads
 *         offset 40, the body's c_ospeed, not the face's 56
 *   0x08  the body never consults the face's offset: a struct with the face
 *         speed fields set but the body's offsets zero returns 0
 *   0x10  cfmakeraw crosses the shared leading region exactly -- the four flag
 *         words come back the body's measured raw-mode values -- run last so a
 *         body that had drifted on the shared region would show it here
 */

#include <stdint.h>
#include <stddef.h>
#include "../wire.h"

#define AT_BASE 7

typedef void (*terminator_fn)(uint64_t status);

/* The two layouts, as constants the specimen lays out by hand. speed_t is
 * four bytes on both sides; only the offset of the speed fields differs. */
#define FACE_ISPEED_OFF 52
#define FACE_OSPEED_OFF 56
#define BODY_ISPEED_OFF 36
#define BODY_OSPEED_OFF 40
#define TERMIOS_ROOM    64      /* >= max(face 60, body 44), specimen-owned */

extern struct esn_wire_ent __esn_wire_terminal[];
extern const unsigned long __esn_wire_terminal_n;

/* The wired thunks this specimen calls directly, by their generated labels.
 * cfgetispeed/cfgetospeed return speed_t (four bytes); cfmakeraw returns
 * void. Declared with a raw pointer, which is exactly the point: the body
 * imposes its own struct layout on the bytes behind it. */
extern uint32_t w00000(const void *t);   /* cfgetispeed, .weak */
extern uint32_t w00001(const void *t);   /* cfgetospeed, .weak */
extern void     w00002(void *t);         /* cfmakeraw,   .weak */

static uint16_t rd16(const uint8_t *p)
{
	return (uint16_t)(p[0] | ((uint16_t) p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
	return p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
	       ((uint32_t) p[3] << 24);
}

static int name_is(const uint8_t *p, const char *want)
{
	while (*want && *p == (uint8_t) *want) {
		p++;
		want++;
	}
	return *want == 0 && *p == 0;
}

/* Resolve one export by name from a loaded PE image -- the same walk
 * runtime/face/t/elfcall.c uses, adapted to wire.h's resolver shape so it
 * can stand in for the runtime's eventual GetProcAddress callback. */
static void *pe_export(const uint8_t *base, const char *name)
{
	uint32_t lfanew, nnames, i;
	const uint8_t *opt, *dir;

	if (rd16(base) != 0x5A4D)
		return 0;
	lfanew = rd32(base + 0x3C);
	if (rd32(base + lfanew) != 0x00004550)
		return 0;
	opt = base + lfanew + 4 + 20;
	if (rd16(opt) != 0x20B)
		return 0;
	if (rd32(opt + 108) < 1 || rd32(opt + 112) == 0)
		return 0;
	dir = base + rd32(opt + 112);
	nnames = rd32(dir + 24);
	for (i = 0; i < nnames; i++) {
		if (name_is(base + rd32(base + rd32(dir + 32) + 4u * i), name)) {
			uint16_t ord = rd16(base + rd32(dir + 36) + 2u * i);
			return (void *)(base + rd32(base + rd32(dir + 28)
						    + 4u * ord));
		}
	}
	return 0;
}

static void *resolve(const char *export_name, void *ctx)
{
	return pe_export((const uint8_t *) ctx, export_name);
}

/* Lay a four-byte little-endian value into the specimen's byte buffer at a
 * chosen offset -- the specimen builds the struct itself, so no libc type
 * decides where a field lands. */
static void put32(uint8_t *buf, int off, uint32_t val)
{
	buf[off + 0] = (uint8_t) val;
	buf[off + 1] = (uint8_t)(val >> 8);
	buf[off + 2] = (uint8_t)(val >> 16);
	buf[off + 3] = (uint8_t)(val >> 24);
}

static uint32_t get32(const uint8_t *buf, int off)
{
	return rd32(buf + off);
}

static void zero(uint8_t *buf, int n)
{
	int i;

	for (i = 0; i < n; i++)
		buf[i] = 0;
}

/* Distinct markers: the value the body reads if it consults its own offset,
 * and a different one at the face's offset, so a body reading the face field
 * could not match the body-offset check by luck. */
#define BODY_ISPEED 0x00006002u
#define FACE_ISPEED 0x00003001u
#define BODY_OSPEED 0x00006004u
#define FACE_OSPEED 0x00003003u

void live_terminal_main(uint64_t *sp, terminator_fn leave)
{
	uint64_t status = 0;
	uint64_t *p;
	const uint8_t *rt = 0;
	size_t missing;

	/* Past argv and its terminator, past envp and its terminator. */
	p = sp + 1 + sp[0] + 1;
	while (*p)
		p++;
	p++;
	for (; p[0]; p += 2) {
		if (p[0] == AT_BASE) {
			rt = (const uint8_t *)(uintptr_t) p[1];
			break;
		}
	}

	if (rt) {
		missing = __esn_wire_bind(__esn_wire_terminal,
					  __esn_wire_terminal_n,
					  resolve, (void *) rt);
		if (missing == 0 && __esn_wire_terminal_n > 0)
			status |= 0x01;

		/* cfgetispeed on a face-laid struct: the body reads its own
		 * c_ispeed at offset 36, returning BODY_ISPEED, not FACE_ISPEED
		 * at the face's offset 52. */
		{
			uint8_t t[TERMIOS_ROOM];

			zero(t, TERMIOS_ROOM);
			put32(t, FACE_ISPEED_OFF, FACE_ISPEED);
			put32(t, BODY_ISPEED_OFF, BODY_ISPEED);
			{
				uint32_t got = w00000(t);

				if (got == BODY_ISPEED && got != FACE_ISPEED)
					status |= 0x02;
			}
		}

		/* cfgetospeed reproduces the divergence on the output speed:
		 * the body reads offset 40, returning BODY_OSPEED, not the
		 * face's offset 56. */
		{
			uint8_t t[TERMIOS_ROOM];

			zero(t, TERMIOS_ROOM);
			put32(t, FACE_OSPEED_OFF, FACE_OSPEED);
			put32(t, BODY_OSPEED_OFF, BODY_OSPEED);
			{
				uint32_t got = w00001(t);

				if (got == BODY_OSPEED && got != FACE_OSPEED)
					status |= 0x04;
			}
		}

		/* The body never consults the face's offset: fill only the face
		 * speed fields, leave the body's offsets zero, and cfgetispeed
		 * returns 0 -- reading its own now-empty field. */
		{
			uint8_t t[TERMIOS_ROOM];

			zero(t, TERMIOS_ROOM);
			put32(t, FACE_ISPEED_OFF, FACE_ISPEED);
			put32(t, FACE_OSPEED_OFF, FACE_OSPEED);
			if (w00000(t) == 0)
				status |= 0x08;
		}

		/* cfmakeraw crosses the shared leading region exactly. From an
		 * all-ones struct the body leaves the four flag words at their
		 * measured raw-mode values; the face shares those offsets, so a
		 * face-laid struct reads them back unchanged. Run last so a body
		 * that had drifted on the shared region would show it here. */
		{
			uint8_t t[TERMIOS_ROOM];
			int i;

			for (i = 0; i < TERMIOS_ROOM; i++)
				t[i] = 0xFF;
			w00002(t);
			if (get32(t, 0) == 0xfffefa1cu &&
			    get32(t, 4) == 0xfffffffeu &&
			    get32(t, 8) == 0xfffffeffu &&
			    get32(t, 12) == 0xfffffed8u)
				status |= 0x10;
		}
	}

	leave(status);
}
