/* WP-31: bounds-checked ELF64 parsing. See elf_parse.h and README.md.
 *
 * musl's ldso/dynlink.c is the structural model for the walk, read as
 * reference only. musl is MIT-licensed; no code is copied here, so there is
 * nothing to attribute beyond the debt of the shape. Where musl trusts a
 * field that a mapped, kernel-checked image has already made safe, this parser
 * runs before any such check exists and re-derives the bound itself.
 *
 * The one discipline that makes the rest safe: nothing in the image is read
 * except through region_ok() and the rd_* helpers, which prove a span lies
 * inside [image, image+size) before a byte of it is touched, and read it with
 * memcpy so no misaligned or type-punned access is ever performed. Every
 * offset sum that could exceed 64 bits is formed with add_ov(). A field that
 * fails a check produces a diagnostic naming it and stops the parse; it never
 * advances past the failure.
 */
#include "elf_parse.h"
#include "elf_types.h"

#include <stdio.h>
#include <string.h>

/* ---- primitives ------------------------------------------------------- */

/* r = a + b, returning 1 on unsigned 64-bit overflow. */
static int add_ov(uint64_t a, uint64_t b, uint64_t *r)
{
	if (a > UINT64_MAX - b)
		return 1;
	*r = a + b;
	return 0;
}

/* r = a * b, returning 1 on unsigned 64-bit overflow. */
static int mul_ov(uint64_t a, uint64_t b, uint64_t *r)
{
	if (a != 0 && b > UINT64_MAX / a)
		return 1;
	*r = a * b;
	return 0;
}

/* Does [off, off+len) lie wholly within [0, size)? Overflow-safe: never
 * forms off+len. An empty span (len 0) is in range iff off <= size. */
static int region_ok(uint64_t size, uint64_t off, uint64_t len)
{
	if (off > size)
		return 0;
	return len <= size - off;
}

static void set_diag(elf_diag *d, elf_err code, const char *field,
                     const char *fmt, uint64_t a, uint64_t b)
{
	d->code = code;
	d->field = field;
	/* Two %llu at most; callers pass the offending value(s). */
	snprintf(d->msg, sizeof d->msg, fmt,
	         (unsigned long long)a, (unsigned long long)b);
}

/* Bounds-checked little-endian reads. Each proves its span first, then copies
 * so the access is aligned and free of type punning. Return 1 on success. */
static int rd_u16(const unsigned char *img, uint64_t size, uint64_t off,
                  uint16_t *v)
{
	unsigned char b[2];
	if (!region_ok(size, off, 2)) return 0;
	memcpy(b, img + off, 2);
	*v = (uint16_t)(b[0] | (b[1] << 8));
	return 1;
}
static int rd_u32(const unsigned char *img, uint64_t size, uint64_t off,
                  uint32_t *v)
{
	unsigned char b[4];
	if (!region_ok(size, off, 4)) return 0;
	memcpy(b, img + off, 4);
	*v = (uint32_t)b[0] | ((uint32_t)b[1] << 8)
	   | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	return 1;
}
static int rd_u64(const unsigned char *img, uint64_t size, uint64_t off,
                  uint64_t *v)
{
	unsigned char b[8];
	int i;
	uint64_t r = 0;
	if (!region_ok(size, off, 8)) return 0;
	memcpy(b, img + off, 8);
	for (i = 7; i >= 0; i--)
		r = (r << 8) | b[i];
	*v = r;
	return 1;
}

const char *elf_err_name(elf_err code)
{
	switch (code) {
	case elf_ok:           return "ok";
	case elf_err_size:     return "size";
	case elf_err_magic:    return "magic";
	case elf_err_header:   return "header";
	case elf_err_phdr:     return "phdr";
	case elf_err_overlap:  return "overlap";
	case elf_err_dynamic:  return "dynamic";
	case elf_err_strtab:   return "strtab";
	case elf_err_symtab:   return "symtab";
	case elf_err_version:  return "version";
	case elf_err_overflow: return "overflow";
	}
	return "?";
}

/* Translate a virtual address to a file offset through the PT_LOAD table,
 * requiring the whole [v, v+span) to be backed by file bytes in one segment.
 * Returns 1 and sets *off on success. A vaddr that lands only in a segment's
 * bss tail (past p_filesz) is not file-backed and fails. */
static int vaddr_to_off(const elf_parsed *p, uint64_t v, uint64_t span,
                        uint64_t *off)
{
	unsigned i;
	for (i = 0; i < p->load_count; i++) {
		const elf_load_seg *s = &p->load[i];
		uint64_t end;
		if (v < s->vaddr)
			continue;
		if (add_ov(s->vaddr, s->filesz, &end))
			continue;
		/* need [v, v+span) inside [vaddr, vaddr+filesz) */
		if (v > end || span > end - v)
			continue;
		*off = s->off + (v - s->vaddr);
		return 1;
	}
	return 0;
}

/* ---- header ----------------------------------------------------------- */

static elf_err parse_header(const unsigned char *img, uint64_t size,
                            elf_parsed *out, uint64_t *resolved_phnum,
                            elf_diag *d)
{
	uint8_t cls, data, ver;
	uint16_t e_type, e_machine, e_phentsize, e_phnum, e_ehsize;
	uint16_t e_shentsize;
	uint32_t e_version;
	uint64_t e_phoff, e_shoff;

	if (size < ELF64_EHDR_SIZE) {
		set_diag(d, elf_err_size, "e_ident",
		         "image is %llu bytes, smaller than a %llu-byte ELF64 header",
		         size, (uint64_t)ELF64_EHDR_SIZE);
		return elf_err_size;
	}
	if (img[EI_MAG0] != ELFMAG0 || img[EI_MAG1] != ELFMAG1 ||
	    img[EI_MAG2] != ELFMAG2 || img[EI_MAG3] != ELFMAG3) {
		set_diag(d, elf_err_magic, "e_ident[EI_MAG]",
		         "bad magic 0x%llx; not an ELF image", (uint64_t)img[0], 0);
		return elf_err_magic;
	}
	cls = img[EI_CLASS]; data = img[EI_DATA]; ver = img[EI_VERSION];
	if (cls != ELFCLASS64) {
		set_diag(d, elf_err_magic, "e_ident[EI_CLASS]",
		         "class %llu is not ELFCLASS64", (uint64_t)cls, 0);
		return elf_err_magic;
	}
	if (data != ELFDATA2LSB) {
		set_diag(d, elf_err_magic, "e_ident[EI_DATA]",
		         "data encoding %llu is not little-endian", (uint64_t)data, 0);
		return elf_err_magic;
	}
	if (ver != EV_CURRENT) {
		set_diag(d, elf_err_header, "e_ident[EI_VERSION]",
		         "ident version %llu is not EV_CURRENT", (uint64_t)ver, 0);
		return elf_err_header;
	}

	rd_u16(img, size, 16, &e_type);
	rd_u16(img, size, 18, &e_machine);
	rd_u32(img, size, 20, &e_version);
	rd_u64(img, size, 24, &out->e_entry);
	rd_u64(img, size, 32, &e_phoff);
	rd_u64(img, size, 40, &e_shoff);
	rd_u16(img, size, 52, &e_ehsize);
	rd_u16(img, size, 54, &e_phentsize);
	rd_u16(img, size, 56, &e_phnum);
	rd_u16(img, size, 58, &e_shentsize);

	if (e_version != EV_CURRENT) {
		set_diag(d, elf_err_header, "e_version",
		         "file version %llu is not EV_CURRENT", (uint64_t)e_version, 0);
		return elf_err_header;
	}
	if (e_machine != EM_X86_64) {
		set_diag(d, elf_err_magic, "e_machine",
		         "machine %llu is not EM_X86_64 (62)", (uint64_t)e_machine, 0);
		return elf_err_magic;
	}
	if (e_type != ET_EXEC && e_type != ET_DYN) {
		set_diag(d, elf_err_header, "e_type",
		         "type %llu is neither ET_EXEC nor ET_DYN", (uint64_t)e_type, 0);
		return elf_err_header;
	}
	if (e_ehsize < ELF64_EHDR_SIZE) {
		set_diag(d, elf_err_header, "e_ehsize",
		         "e_ehsize %llu is below the %llu-byte header",
		         (uint64_t)e_ehsize, (uint64_t)ELF64_EHDR_SIZE);
		return elf_err_header;
	}

	/* Resolve the program-header count, expanding PN_XNUM through the first
	 * section header when the 16-bit field is saturated. */
	{
		uint64_t phnum = e_phnum;
		if (e_phnum == PN_XNUM) {
			uint32_t real;
			if (e_shentsize != ELF64_SHDR_SIZE) {
				set_diag(d, elf_err_header, "e_shentsize",
				         "e_shentsize %llu cannot resolve PN_XNUM",
				         (uint64_t)e_shentsize, 0);
				return elf_err_header;
			}
			if (!region_ok(size, e_shoff, ELF64_SHDR_SIZE)) {
				set_diag(d, elf_err_header, "e_shoff",
				         "section headers at %llu are out of a %llu-byte image",
				         e_shoff, size);
				return elf_err_header;
			}
			rd_u32(img, size, e_shoff + 44, &real); /* shdr[0].sh_info */
			phnum = real;
		}
		if (phnum > 0 && e_phentsize != ELF64_PHDR_SIZE) {
			set_diag(d, elf_err_header, "e_phentsize",
			         "e_phentsize %llu is not the %llu-byte ELF64 entry",
			         (uint64_t)e_phentsize, (uint64_t)ELF64_PHDR_SIZE);
			return elf_err_header;
		}
		if (phnum > 0xffffffu) { /* far past anything a real object carries */
			set_diag(d, elf_err_phdr, "e_phnum",
			         "program-header count %llu is implausible", phnum, 0);
			return elf_err_phdr;
		}
		{
			uint64_t span;
			if (mul_ov(phnum, ELF64_PHDR_SIZE, &span)) {
				set_diag(d, elf_err_overflow, "e_phnum",
				         "phnum %llu times entry size overflows", phnum, 0);
				return elf_err_overflow;
			}
			if (phnum > 0 && !region_ok(size, e_phoff, span)) {
				set_diag(d, elf_err_phdr, "e_phoff",
				         "phdr table [%llu,+%llu) is out of range",
				         e_phoff, span);
				return elf_err_phdr;
			}
		}
		out->phnum = (uint16_t)(phnum > 0xffff ? 0xffff : phnum);
		/* keep the full resolved count for iteration */
		out->phoff = e_phoff;
		out->e_type = e_type;
		out->e_machine = e_machine;
		out->phentsize = e_phentsize;
		*resolved_phnum = phnum;
	}
	return elf_ok;
}

/* ---- program headers -------------------------------------------------- */

/* Walk the program-header table, collecting the segments the loader needs and
 * checking each one's file range. PT_LOAD segments are also checked pairwise
 * for overlap in virtual space, which a well-formed object never has. */
static elf_err parse_phdrs(const unsigned char *img, uint64_t size,
                           uint64_t phnum, elf_parsed *out,
                           uint64_t *dyn_off, uint64_t *dyn_filesz,
                           elf_diag *d)
{
	uint64_t i;
	int have_dyn = 0;

	for (i = 0; i < phnum; i++) {
		uint64_t base = out->phoff + i * ELF64_PHDR_SIZE;
		uint32_t p_type, p_flags;
		uint64_t p_offset, p_vaddr, p_filesz, p_memsz, p_align, end;

		/* base is in range: the whole table was checked in parse_header. */
		rd_u32(img, size, base + 0,  &p_type);
		rd_u32(img, size, base + 4,  &p_flags);
		rd_u64(img, size, base + 8,  &p_offset);
		rd_u64(img, size, base + 16, &p_vaddr);
		rd_u64(img, size, base + 32, &p_filesz);
		rd_u64(img, size, base + 40, &p_memsz);
		rd_u64(img, size, base + 48, &p_align);

		/* Every segment with file content must name a real file range. */
		if (p_type == PT_LOAD || p_type == PT_DYNAMIC ||
		    p_type == PT_INTERP || p_type == PT_TLS ||
		    p_type == PT_GNU_RELRO || p_type == PT_NOTE) {
			if (!region_ok(size, p_offset, p_filesz)) {
				set_diag(d, elf_err_phdr, "p_offset",
				         "segment file range [%llu,+%llu) is out of the image",
				         p_offset, p_filesz);
				return elf_err_phdr;
			}
		}
		/* Virtual span must not overflow, and file content must fit it. */
		if (add_ov(p_vaddr, p_memsz, &end)) {
			set_diag(d, elf_err_overflow, "p_memsz",
			         "p_vaddr %llu + p_memsz %llu overflows", p_vaddr, p_memsz);
			return elf_err_overflow;
		}

		switch (p_type) {
		case PT_LOAD:
			if (p_memsz == 0) {
				set_diag(d, elf_err_phdr, "PT_LOAD.p_memsz",
				         "PT_LOAD at index %llu has zero memory size", i, 0);
				return elf_err_phdr;
			}
			if (p_filesz > p_memsz) {
				set_diag(d, elf_err_phdr, "PT_LOAD.p_filesz",
				         "PT_LOAD file size %llu exceeds memory size %llu",
				         p_filesz, p_memsz);
				return elf_err_phdr;
			}
			if (out->load_count >= ELF_MAX_LOAD) {
				set_diag(d, elf_err_phdr, "PT_LOAD",
				         "more than %llu PT_LOAD segments",
				         (uint64_t)ELF_MAX_LOAD, 0);
				return elf_err_phdr;
			}
			{
				elf_load_seg *s = &out->load[out->load_count];
				unsigned j;
				uint64_t a0 = p_vaddr, a1 = end; /* [a0, a1) */
				for (j = 0; j < out->load_count; j++) {
					uint64_t b0 = out->load[j].vaddr;
					uint64_t b1 = b0 + out->load[j].memsz;
					if (a0 < b1 && b0 < a1) {
						set_diag(d, elf_err_overlap, "PT_LOAD.p_vaddr",
						         "PT_LOAD at %llu overlaps an earlier one at %llu",
						         a0, b0);
						return elf_err_overlap;
					}
				}
				s->off = p_offset; s->vaddr = p_vaddr;
				s->filesz = p_filesz; s->memsz = p_memsz;
				s->flags = p_flags; s->align = p_align;
				out->load_count++;
			}
			break;
		case PT_DYNAMIC:
			if (have_dyn) {
				set_diag(d, elf_err_phdr, "PT_DYNAMIC",
				         "more than one PT_DYNAMIC segment", 0, 0);
				return elf_err_phdr;
			}
			have_dyn = 1;
			*dyn_off = p_offset;
			*dyn_filesz = p_filesz;
			break;
		case PT_INTERP:
			out->has_interp = 1;
			out->interp_off = p_offset;
			out->interp_size = p_filesz;
			break;
		case PT_TLS:
			out->has_tls = 1;
			out->tls_off = p_offset; out->tls_filesz = p_filesz;
			out->tls_memsz = p_memsz; out->tls_align = p_align;
			break;
		case PT_GNU_RELRO:
			out->has_relro = 1;
			out->relro_off = p_offset; out->relro_size = p_filesz;
			break;
		default:
			break;
		}
	}

	out->has_dynamic = have_dyn;
	return elf_ok;
}

/* ---- dynamic section -------------------------------------------------- */

/* Raw values collected from PT_DYNAMIC in one pass, before any is resolved
 * against the string table it may need. */
typedef struct {
	int      have_strtab, have_strsz, have_symtab, have_syment;
	int      have_versym, have_verdef, have_verdefnum;
	int      have_verneed, have_verneednum, have_soname;
	uint64_t strtab_v, strsz, symtab_v, syment;
	uint64_t versym_v, verdef_v, verdefnum, verneed_v, verneednum;
	uint64_t soname;
	uint64_t needed[ELF_MAX_NEEDED];
	unsigned needed_count;
} dyn_raw;

static elf_err collect_dynamic(const unsigned char *img, uint64_t size,
                               uint64_t dyn_off, uint64_t dyn_filesz,
                               dyn_raw *r, uint64_t *dyn_count, elf_diag *d)
{
	uint64_t i, max = dyn_filesz / ELF64_DYN_SIZE;
	int saw_null = 0;

	memset(r, 0, sizeof *r);
	for (i = 0; i < max; i++) {
		uint64_t base = dyn_off + i * ELF64_DYN_SIZE;
		uint64_t tag, val;
		/* dyn_off+dyn_filesz was checked in parse_phdrs; each 16-byte
		 * entry within it is therefore in range, but re-prove it so a
		 * future change to the caller cannot silently unground us. */
		if (!rd_u64(img, size, base, &tag) ||
		    !rd_u64(img, size, base + 8, &val)) {
			set_diag(d, elf_err_dynamic, "Elf64_Dyn",
			         "dynamic entry %llu runs past the segment", i, 0);
			return elf_err_dynamic;
		}
		if ((int64_t)tag == DT_NULL) { saw_null = 1; *dyn_count = i; break; }

		switch ((int64_t)tag) {
		case DT_STRTAB:  r->strtab_v = val; r->have_strtab = 1; break;
		case DT_STRSZ:   r->strsz = val; r->have_strsz = 1; break;
		case DT_SYMTAB:  r->symtab_v = val; r->have_symtab = 1; break;
		case DT_SYMENT:  r->syment = val; r->have_syment = 1; break;
		case DT_VERSYM:  r->versym_v = val; r->have_versym = 1; break;
		case DT_VERDEF:  r->verdef_v = val; r->have_verdef = 1; break;
		case DT_VERDEFNUM: r->verdefnum = val; r->have_verdefnum = 1; break;
		case DT_VERNEED: r->verneed_v = val; r->have_verneed = 1; break;
		case DT_VERNEEDNUM: r->verneednum = val; r->have_verneednum = 1; break;
		case DT_SONAME:  r->soname = val; r->have_soname = 1; break;
		case DT_NEEDED:
			if (r->needed_count >= ELF_MAX_NEEDED) {
				set_diag(d, elf_err_dynamic, "DT_NEEDED",
				         "more than %llu DT_NEEDED entries",
				         (uint64_t)ELF_MAX_NEEDED, 0);
				return elf_err_dynamic;
			}
			r->needed[r->needed_count++] = val;
			break;
		default:
			break;
		}
	}
	if (!saw_null) {
		set_diag(d, elf_err_dynamic, "DT_NULL",
		         "dynamic section has no DT_NULL terminator within %llu bytes",
		         dyn_filesz, 0);
		return elf_err_dynamic;
	}
	return elf_ok;
}

/* ---- version-record chains ------------------------------------------- */

/* Walk the Elf64_Verdef chain: exactly `count` records linked by vd_next,
 * each carrying vd_cnt Verdaux linked by vda_next. Every record and aux must
 * lie in the file, every name offset must lie in the string table, and every
 * link must advance strictly forward by at least a whole record so the chain
 * can neither loop nor overlap. A vd_next of zero is the terminator and is
 * legal only on the last of the `count` records. */
static elf_err walk_verdef(const unsigned char *img, uint64_t size,
                           uint64_t off, uint64_t count, uint64_t strsz,
                           elf_diag *d)
{
	uint64_t i, cur = off;
	for (i = 0; i < count; i++) {
		uint16_t vd_version, vd_cnt;
		uint32_t vd_aux, vd_next;
		uint64_t auxcur, j;
		if (!region_ok(size, cur, ELF64_VERDEF_SIZE)) {
			set_diag(d, elf_err_version, "Elf64_Verdef",
			         "verdef record %llu at %llu is out of range", i, cur);
			return elf_err_version;
		}
		rd_u16(img, size, cur + 0, &vd_version);
		rd_u16(img, size, cur + 6, &vd_cnt);
		rd_u32(img, size, cur + 12, &vd_aux);
		rd_u32(img, size, cur + 16, &vd_next);
		if (vd_version != 1) {
			set_diag(d, elf_err_version, "Elf64_Verdef.vd_version",
			         "verdef version %llu is not 1", (uint64_t)vd_version, 0);
			return elf_err_version;
		}
		if (vd_cnt == 0) {
			set_diag(d, elf_err_version, "Elf64_Verdef.vd_cnt",
			         "verdef record %llu names no version", i, 0);
			return elf_err_version;
		}
		if (add_ov(cur, vd_aux, &auxcur)) {
			set_diag(d, elf_err_overflow, "Elf64_Verdef.vd_aux",
			         "vd_aux %llu overflows", (uint64_t)vd_aux, 0);
			return elf_err_overflow;
		}
		for (j = 0; j < vd_cnt; j++) {
			uint32_t vda_name, vda_next;
			if (!region_ok(size, auxcur, ELF64_VERDAUX_SIZE)) {
				set_diag(d, elf_err_version, "Elf64_Verdaux",
				         "verdaux %llu of record %llu is out of range", j, i);
				return elf_err_version;
			}
			rd_u32(img, size, auxcur + 0, &vda_name);
			rd_u32(img, size, auxcur + 4, &vda_next);
			if (vda_name >= strsz) {
				set_diag(d, elf_err_version, "Elf64_Verdaux.vda_name",
				         "vda_name %llu is past the %llu-byte string table",
				         (uint64_t)vda_name, strsz);
				return elf_err_version;
			}
			if (j + 1 < vd_cnt) {
				if (vda_next < ELF64_VERDAUX_SIZE) {
					set_diag(d, elf_err_version, "Elf64_Verdaux.vda_next",
					         "vda_next %llu does not advance a full record",
					         (uint64_t)vda_next, 0);
					return elf_err_version;
				}
				if (add_ov(auxcur, vda_next, &auxcur)) {
					set_diag(d, elf_err_overflow, "Elf64_Verdaux.vda_next",
					         "vda_next %llu overflows", (uint64_t)vda_next, 0);
					return elf_err_overflow;
				}
			}
		}
		if (i + 1 < count) {
			if (vd_next < ELF64_VERDEF_SIZE) {
				set_diag(d, elf_err_version, "Elf64_Verdef.vd_next",
				         "vd_next %llu does not advance a full record (loop?)",
				         (uint64_t)vd_next, 0);
				return elf_err_version;
			}
			if (add_ov(cur, vd_next, &cur)) {
				set_diag(d, elf_err_overflow, "Elf64_Verdef.vd_next",
				         "vd_next %llu overflows", (uint64_t)vd_next, 0);
				return elf_err_overflow;
			}
		}
	}
	return elf_ok;
}

/* Walk the Elf64_Verneed chain: the same discipline as walk_verdef, over
 * vn_next and its vn_cnt Vernaux linked by vna_next. */
static elf_err walk_verneed(const unsigned char *img, uint64_t size,
                            uint64_t off, uint64_t count, uint64_t strsz,
                            elf_diag *d)
{
	uint64_t i, cur = off;
	for (i = 0; i < count; i++) {
		uint16_t vn_version, vn_cnt;
		uint32_t vn_file, vn_aux, vn_next;
		uint64_t auxcur, j;
		if (!region_ok(size, cur, ELF64_VERNEED_SIZE)) {
			set_diag(d, elf_err_version, "Elf64_Verneed",
			         "verneed record %llu at %llu is out of range", i, cur);
			return elf_err_version;
		}
		rd_u16(img, size, cur + 0, &vn_version);
		rd_u16(img, size, cur + 2, &vn_cnt);
		rd_u32(img, size, cur + 4, &vn_file);
		rd_u32(img, size, cur + 8, &vn_aux);
		rd_u32(img, size, cur + 12, &vn_next);
		if (vn_version != 1) {
			set_diag(d, elf_err_version, "Elf64_Verneed.vn_version",
			         "verneed version %llu is not 1", (uint64_t)vn_version, 0);
			return elf_err_version;
		}
		if (vn_file >= strsz) {
			set_diag(d, elf_err_version, "Elf64_Verneed.vn_file",
			         "vn_file %llu is past the %llu-byte string table",
			         (uint64_t)vn_file, strsz);
			return elf_err_version;
		}
		if (vn_cnt == 0) {
			set_diag(d, elf_err_version, "Elf64_Verneed.vn_cnt",
			         "verneed record %llu names no version", i, 0);
			return elf_err_version;
		}
		if (add_ov(cur, vn_aux, &auxcur)) {
			set_diag(d, elf_err_overflow, "Elf64_Verneed.vn_aux",
			         "vn_aux %llu overflows", (uint64_t)vn_aux, 0);
			return elf_err_overflow;
		}
		for (j = 0; j < vn_cnt; j++) {
			uint32_t vna_name, vna_next;
			if (!region_ok(size, auxcur, ELF64_VERNAUX_SIZE)) {
				set_diag(d, elf_err_version, "Elf64_Vernaux",
				         "vernaux %llu of record %llu is out of range", j, i);
				return elf_err_version;
			}
			rd_u32(img, size, auxcur + 8, &vna_name);
			rd_u32(img, size, auxcur + 12, &vna_next);
			if (vna_name >= strsz) {
				set_diag(d, elf_err_version, "Elf64_Vernaux.vna_name",
				         "vna_name %llu is past the %llu-byte string table",
				         (uint64_t)vna_name, strsz);
				return elf_err_version;
			}
			if (j + 1 < vn_cnt) {
				if (vna_next < ELF64_VERNAUX_SIZE) {
					set_diag(d, elf_err_version, "Elf64_Vernaux.vna_next",
					         "vna_next %llu does not advance a full record",
					         (uint64_t)vna_next, 0);
					return elf_err_version;
				}
				if (add_ov(auxcur, vna_next, &auxcur)) {
					set_diag(d, elf_err_overflow, "Elf64_Vernaux.vna_next",
					         "vna_next %llu overflows", (uint64_t)vna_next, 0);
					return elf_err_overflow;
				}
			}
		}
		if (i + 1 < count) {
			if (vn_next < ELF64_VERNEED_SIZE) {
				set_diag(d, elf_err_version, "Elf64_Verneed.vn_next",
				         "vn_next %llu does not advance a full record (loop?)",
				         (uint64_t)vn_next, 0);
				return elf_err_version;
			}
			if (add_ov(cur, vn_next, &cur)) {
				set_diag(d, elf_err_overflow, "Elf64_Verneed.vn_next",
				         "vn_next %llu overflows", (uint64_t)vn_next, 0);
				return elf_err_overflow;
			}
		}
	}
	return elf_ok;
}

/* ---- resolution ------------------------------------------------------- */

/* Turn the raw dynamic values into checked file offsets in `out`, validating
 * every table reached through the dynamic section and every name offset that
 * indexes the string table. */
static elf_err resolve_dynamic(const unsigned char *img, uint64_t size,
                               const dyn_raw *r, elf_parsed *out, elf_diag *d)
{
	unsigned k;

	/* String table first: names cannot be checked without its size. */
	if (r->have_strtab || r->have_strsz) {
		uint64_t off;
		if (!r->have_strtab || !r->have_strsz) {
			set_diag(d, elf_err_strtab,
			         r->have_strtab ? "DT_STRSZ" : "DT_STRTAB",
			         "the dynamic section has one of DT_STRTAB/DT_STRSZ, not both",
			         0, 0);
			return elf_err_strtab;
		}
		if (r->strsz == 0) {
			set_diag(d, elf_err_strtab, "DT_STRSZ",
			         "string table has zero size", 0, 0);
			return elf_err_strtab;
		}
		if (!vaddr_to_off(out, r->strtab_v, r->strsz, &off)) {
			set_diag(d, elf_err_strtab, "DT_STRTAB",
			         "string table vaddr %llu (size %llu) is not file-backed",
			         r->strtab_v, r->strsz);
			return elf_err_strtab;
		}
		out->has_strtab = 1;
		out->strtab_off = off;
		out->strsz = r->strsz;
	}

	/* Anything that indexes the string table needs one present. */
	if ((r->needed_count || r->have_soname ||
	     r->have_verdef || r->have_verneed) && !out->has_strtab) {
		set_diag(d, elf_err_strtab, "DT_STRTAB",
		         "a name or version record needs a string table, which is absent",
		         0, 0);
		return elf_err_strtab;
	}

	for (k = 0; k < r->needed_count; k++) {
		if (r->needed[k] >= out->strsz) {
			set_diag(d, elf_err_strtab, "DT_NEEDED",
			         "DT_NEEDED name offset %llu is past the %llu-byte strtab",
			         r->needed[k], out->strsz);
			return elf_err_strtab;
		}
		out->needed[out->needed_count++] = r->needed[k];
	}
	if (r->have_soname) {
		if (r->soname >= out->strsz) {
			set_diag(d, elf_err_strtab, "DT_SONAME",
			         "DT_SONAME name offset %llu is past the %llu-byte strtab",
			         r->soname, out->strsz);
			return elf_err_strtab;
		}
		out->has_soname = 1;
		out->soname = r->soname;
	}

	if (r->have_symtab) {
		uint64_t off, ent = r->have_syment ? r->syment : ELF64_SYM_SIZE;
		if (r->have_syment && r->syment != ELF64_SYM_SIZE) {
			set_diag(d, elf_err_symtab, "DT_SYMENT",
			         "DT_SYMENT %llu is not the %llu-byte ELF64 symbol",
			         r->syment, (uint64_t)ELF64_SYM_SIZE);
			return elf_err_symtab;
		}
		if (!vaddr_to_off(out, r->symtab_v, ent, &off)) {
			set_diag(d, elf_err_symtab, "DT_SYMTAB",
			         "symbol table vaddr %llu is not file-backed", r->symtab_v, 0);
			return elf_err_symtab;
		}
		out->has_symtab = 1;
		out->symtab_off = off;
		out->syment = ent;
	}

	if (r->have_versym) {
		uint64_t off;
		if (!vaddr_to_off(out, r->versym_v, ELF64_VERSYM_SIZE, &off)) {
			set_diag(d, elf_err_version, "DT_VERSYM",
			         "version-symbol table vaddr %llu is not file-backed",
			         r->versym_v, 0);
			return elf_err_version;
		}
		out->has_versym = 1;
		out->versym_off = off;
	}

	if (r->have_verdef) {
		uint64_t off;
		uint64_t num = r->have_verdefnum ? r->verdefnum : 0;
		if (num == 0) {
			set_diag(d, elf_err_version, "DT_VERDEFNUM",
			         "DT_VERDEF present with a zero or absent DT_VERDEFNUM",
			         0, 0);
			return elf_err_version;
		}
		if (!vaddr_to_off(out, r->verdef_v, ELF64_VERDEF_SIZE, &off)) {
			set_diag(d, elf_err_version, "DT_VERDEF",
			         "verdef vaddr %llu is not file-backed", r->verdef_v, 0);
			return elf_err_version;
		}
		{
			elf_err e = walk_verdef(img, size, off, num, out->strsz, d);
			if (e != elf_ok) return e;
		}
		out->has_verdef = 1;
		out->verdef_off = off;
		out->verdefnum = num;
	}

	if (r->have_verneed) {
		uint64_t off;
		uint64_t num = r->have_verneednum ? r->verneednum : 0;
		if (num == 0) {
			set_diag(d, elf_err_version, "DT_VERNEEDNUM",
			         "DT_VERNEED present with a zero or absent DT_VERNEEDNUM",
			         0, 0);
			return elf_err_version;
		}
		if (!vaddr_to_off(out, r->verneed_v, ELF64_VERNEED_SIZE, &off)) {
			set_diag(d, elf_err_version, "DT_VERNEED",
			         "verneed vaddr %llu is not file-backed", r->verneed_v, 0);
			return elf_err_version;
		}
		{
			elf_err e = walk_verneed(img, size, off, num, out->strsz, d);
			if (e != elf_ok) return e;
		}
		out->has_verneed = 1;
		out->verneed_off = off;
		out->verneednum = num;
	}

	return elf_ok;
}

/* ---- entry point ------------------------------------------------------ */

elf_err elf_parse(const unsigned char *image, size_t size,
                  elf_parsed *out, elf_diag *diag)
{
	elf_err e;
	uint64_t phnum = 0, dyn_off = 0, dyn_filesz = 0;
	elf_diag local;

	if (diag == 0) diag = &local;
	diag->code = elf_ok; diag->field = 0; diag->msg[0] = 0;

	if (out == 0) {
		set_diag(diag, elf_err_header, "out", "null output pointer", 0, 0);
		return elf_err_header;
	}
	memset(out, 0, sizeof *out);
	if (size == 0) {
		set_diag(diag, elf_err_size, "size", "image is empty", 0, 0);
		return elf_err_size;
	}
	if (image == 0) {
		set_diag(diag, elf_err_size, "image", "null image with nonzero size",
		         (uint64_t)size, 0);
		return elf_err_size;
	}

	e = parse_header(image, size, out, &phnum, diag);
	if (e != elf_ok) return e;

	e = parse_phdrs(image, size, phnum, out, &dyn_off, &dyn_filesz, diag);
	if (e != elf_ok) return e;

	if (out->has_dynamic) {
		dyn_raw raw;
		e = collect_dynamic(image, size, dyn_off, dyn_filesz, &raw,
		                    &out->dyn_count, diag);
		if (e != elf_ok) return e;
		out->dyn_off = dyn_off;
		e = resolve_dynamic(image, size, &raw, out, diag);
		if (e != elf_ok) return e;
	}

	return elf_ok;
}
