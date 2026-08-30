/* elfcore.c -- emit an ET_CORE image in the layout the Linux kernel writes.
 *
 * WP-61, under DR-0033. The consumers are gdb's core-reading path and the
 * binutils readers, both of which hard-code the kernel's note layouts, so
 * the layouts are pinned here with offset assertions: a drifted structure
 * fails the build rather than the debug session.
 */
#include <string.h>

#include "elfcore.h"

#define ELF_SASSERT(cond, tag) typedef char sassert_##tag[(cond) ? 1 : -1]

/* ---- ELF object file pieces, 64-bit little-endian ---------------------- */

typedef struct {
	unsigned char e_ident[16];
	uint16_t e_type, e_machine;
	uint32_t e_version;
	uint64_t e_entry, e_phoff, e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize, e_phentsize, e_phnum;
	uint16_t e_shentsize, e_shnum, e_shstrndx;
} ehdr_t;

typedef struct {
	uint32_t p_type, p_flags;
	uint64_t p_offset, p_vaddr, p_paddr;
	uint64_t p_filesz, p_memsz, p_align;
} phdr_t;

typedef struct {
	uint32_t n_namesz, n_descsz, n_type;
} nhdr_t;

ELF_SASSERT(sizeof(ehdr_t) == 64, ehdr);
ELF_SASSERT(sizeof(phdr_t) == 56, phdr);
ELF_SASSERT(sizeof(nhdr_t) == 12, nhdr);

#define ET_CORE 4
#define EM_X86_64 62
#define PT_LOAD 1
#define PT_NOTE 4

#define NT_PRSTATUS 1
#define NT_PRFPREG 2
#define NT_PRPSINFO 3
#define NT_AUXV 6
#define NT_FILE 0x46494c45u

#define CORE_PAGE 4096u

/* ---- the kernel's note payloads, pinned -------------------------------- */

typedef struct {
	int32_t si_signo, si_code, si_errno;
} elf_siginfo_t;

typedef struct {
	int64_t tv_sec, tv_usec;
} elf_timeval_t;

/* struct elf_prstatus, x86_64: 336 bytes, register file at 112. */
typedef struct {
	elf_siginfo_t pr_info;
	int16_t pr_cursig;
	uint64_t pr_sigpend, pr_sighold;
	int32_t pr_pid, pr_ppid, pr_pgrp, pr_sid;
	elf_timeval_t pr_utime, pr_stime, pr_cutime, pr_cstime;
	uint64_t pr_reg[27];
	int32_t pr_fpvalid;
} prstatus_t;

ELF_SASSERT(sizeof(prstatus_t) == 336, prstatus);
ELF_SASSERT(offsetof(prstatus_t, pr_pid) == 32, pr_pid);
ELF_SASSERT(offsetof(prstatus_t, pr_reg) == 112, pr_reg);

/* struct elf_prpsinfo, x86_64: 136 bytes. */
typedef struct {
	char pr_state, pr_sname, pr_zomb, pr_nice;
	uint64_t pr_flag;
	uint32_t pr_uid, pr_gid;
	int32_t pr_pid, pr_ppid, pr_pgrp, pr_sid;
	char pr_fname[16];
	char pr_psargs[80];
} prpsinfo_t;

ELF_SASSERT(sizeof(prpsinfo_t) == 136, prpsinfo);
ELF_SASSERT(offsetof(prpsinfo_t, pr_fname) == 40, pr_fname);

/* user_regs_struct slot indices for pr_reg. */
enum {
	UR_R15, UR_R14, UR_R13, UR_R12, UR_RBP, UR_RBX, UR_R11, UR_R10,
	UR_R9, UR_R8, UR_RAX, UR_RCX, UR_RDX, UR_RSI, UR_RDI, UR_ORIG_RAX,
	UR_RIP, UR_CS, UR_EFLAGS, UR_RSP, UR_SS, UR_FS_BASE, UR_GS_BASE,
	UR_DS, UR_ES, UR_FS, UR_GS
};

/* ---- emission ---------------------------------------------------------- */

typedef struct {
	elfcore_sink_t sink;
	void *cookie;
	int failed;
} out_t;

static void put(out_t *o, const void *buf, size_t n)
{
	if (!o->failed && n && o->sink(o->cookie, buf, n) != (long)n)
		o->failed = 1;
}

static void put_zero(out_t *o, size_t n)
{
	static const char z[512];

	while (n) {
		size_t c = n > sizeof z ? sizeof z : n;

		put(o, z, c);
		n -= c;
	}
}

static uint64_t align_up(uint64_t v, uint64_t a)
{
	return (v + a - 1) & ~(a - 1);
}

/* One note: header, "CORE\0" padded to 4, desc padded to 4. */
static uint64_t note_size(uint64_t descsz)
{
	return sizeof(nhdr_t) + 8 + align_up(descsz, 4);
}

static void put_note(out_t *o, uint32_t type, const void *desc, uint32_t n)
{
	nhdr_t h = { 5, n, type };

	put(o, &h, sizeof h);
	put(o, "CORE\0\0\0\0", 8);
	put(o, desc, n);
	put_zero(o, align_up(n, 4) - n);
}

/* NT_FILE: count and page size, count [start end file_ofs] triples, then
 * the paths NUL-terminated, the whole padded to 4. */
static uint64_t ntfile_desc_size(const elfcore_seg_t *segs, size_t nsegs,
				 uint64_t *nfiles)
{
	uint64_t n = 0, strs = 0;
	size_t i;

	for (i = 0; i < nsegs; i++)
		if (segs[i].path) {
			n++;
			strs += strlen(segs[i].path) + 1;
		}
	*nfiles = n;
	return n ? 16 + n * 24 + strs : 0;
}

static void put_ntfile_desc(out_t *o, const elfcore_seg_t *segs, size_t nsegs,
			    uint64_t nfiles)
{
	uint64_t hdr[2] = { nfiles, CORE_PAGE };
	size_t i;

	put(o, hdr, sizeof hdr);
	for (i = 0; i < nsegs; i++)
		if (segs[i].path) {
			uint64_t t[3] = { segs[i].vaddr,
					  segs[i].vaddr + segs[i].len,
					  segs[i].file_off };
			put(o, t, sizeof t);
		}
	for (i = 0; i < nsegs; i++)
		if (segs[i].path)
			put(o, segs[i].path, strlen(segs[i].path) + 1);
}

static void fill_prstatus(prstatus_t *p, const elfcore_proc_t *proc)
{
	const elfsysv_sigctx_t *c = proc->ctx;
	uint64_t *r = p->pr_reg;

	memset(p, 0, sizeof *p);
	p->pr_info.si_signo = proc->signo;
	p->pr_cursig = (int16_t)proc->signo;
	p->pr_pid = proc->pid;
	r[UR_R15] = c->r15; r[UR_R14] = c->r14; r[UR_R13] = c->r13;
	r[UR_R12] = c->r12; r[UR_RBP] = c->rbp; r[UR_RBX] = c->rbx;
	r[UR_R11] = c->r11; r[UR_R10] = c->r10; r[UR_R9] = c->r9;
	r[UR_R8] = c->r8; r[UR_RAX] = c->rax; r[UR_RCX] = c->rcx;
	r[UR_RDX] = c->rdx; r[UR_RSI] = c->rsi; r[UR_RDI] = c->rdi;
	r[UR_ORIG_RAX] = (uint64_t)-1;
	r[UR_RIP] = c->rip; r[UR_CS] = c->cs; r[UR_EFLAGS] = c->rflags;
	r[UR_RSP] = c->rsp; r[UR_SS] = c->ss;
	r[UR_FS] = c->fs; r[UR_GS] = c->gs;
	p->pr_fpvalid = c->fxsave != NULL;
}

static void fill_prpsinfo(prpsinfo_t *p, const elfcore_proc_t *proc)
{
	memset(p, 0, sizeof *p);
	p->pr_state = 'R';
	p->pr_sname = 'R';
	p->pr_pid = proc->pid;
	if (proc->fname)
		strncpy(p->pr_fname, proc->fname, sizeof p->pr_fname - 1);
	if (proc->psargs)
		strncpy(p->pr_psargs, proc->psargs, sizeof p->pr_psargs - 1);
}

int elfcore_write(elfcore_sink_t sink, void *cookie,
		  const elfcore_proc_t *proc,
		  const elfcore_seg_t *segs, size_t nsegs)
{
	out_t o = { sink, cookie, 0 };
	uint64_t nfiles, ntfile_sz, notes_sz, off, cur;
	size_t i;
	ehdr_t eh;
	phdr_t ph;
	prstatus_t prs;
	prpsinfo_t psi;

	if (!sink || !proc || !proc->ctx || !segs || !nsegs || nsegs > 65000)
		return -2;
	for (i = 0; i < nsegs; i++)
		if (!segs[i].bytes || !segs[i].len)
			return -2;

	ntfile_sz = ntfile_desc_size(segs, nsegs, &nfiles);
	notes_sz = note_size(sizeof prs) + note_size(sizeof psi);
	if (proc->ctx->fxsave)
		notes_sz += note_size(512);
	if (proc->auxv && proc->auxv_len)
		notes_sz += note_size(proc->auxv_len);
	if (nfiles)
		notes_sz += note_size(ntfile_sz);

	off = sizeof eh + (1 + nsegs) * sizeof ph;

	/* The ELF header, then one PT_NOTE and a PT_LOAD per segment. */
	memset(&eh, 0, sizeof eh);
	memcpy(eh.e_ident, "\177ELF\2\1\1", 7);
	eh.e_type = ET_CORE;
	eh.e_machine = EM_X86_64;
	eh.e_version = 1;
	eh.e_phoff = sizeof eh;
	eh.e_ehsize = sizeof eh;
	eh.e_phentsize = sizeof ph;
	eh.e_phnum = (uint16_t)(1 + nsegs);
	put(&o, &eh, sizeof eh);

	memset(&ph, 0, sizeof ph);
	ph.p_type = PT_NOTE;
	ph.p_flags = ELFCORE_R;
	ph.p_offset = off;
	ph.p_filesz = notes_sz;
	ph.p_align = 4;
	put(&o, &ph, sizeof ph);

	cur = align_up(off + notes_sz, CORE_PAGE);
	for (i = 0; i < nsegs; i++) {
		memset(&ph, 0, sizeof ph);
		ph.p_type = PT_LOAD;
		ph.p_flags = segs[i].prot;
		ph.p_offset = cur;
		ph.p_vaddr = segs[i].vaddr;
		ph.p_filesz = segs[i].len;
		ph.p_memsz = segs[i].len;
		ph.p_align = CORE_PAGE;
		put(&o, &ph, sizeof ph);
		cur = align_up(cur + segs[i].len, CORE_PAGE);
	}

	/* The notes, in the order the kernel writes them. */
	fill_prstatus(&prs, proc);
	put_note(&o, NT_PRSTATUS, &prs, sizeof prs);
	fill_prpsinfo(&psi, proc);
	put_note(&o, NT_PRPSINFO, &psi, sizeof psi);
	if (proc->auxv && proc->auxv_len)
		put_note(&o, NT_AUXV, proc->auxv, (uint32_t)proc->auxv_len);
	if (nfiles) {
		nhdr_t nh = { 5, (uint32_t)ntfile_sz, NT_FILE };

		put(&o, &nh, sizeof nh);
		put(&o, "CORE\0\0\0\0", 8);
		put_ntfile_desc(&o, segs, nsegs, nfiles);
		put_zero(&o, align_up(ntfile_sz, 4) - ntfile_sz);
	}
	if (proc->ctx->fxsave)
		put_note(&o, NT_PRFPREG, proc->ctx->fxsave, 512);

	/* The segments, each at the page-aligned offset its phdr promised. */
	off = align_up(off + notes_sz, CORE_PAGE) - (off + notes_sz);
	put_zero(&o, off);
	for (i = 0; i < nsegs; i++) {
		put(&o, segs[i].bytes, segs[i].len);
		put_zero(&o, align_up(segs[i].len, CORE_PAGE) - segs[i].len);
	}

	return o.failed ? -1 : 0;
}
