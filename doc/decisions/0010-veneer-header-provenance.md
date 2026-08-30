# DR-0010 — the veneer's features.h is el8's arithmetic, copied not paraphrased

Status: accepted
Date: 2026-08-30
Deciding: WP-50 implementation, a defensible call under the non-reserved
decision policy in AGENTS.md; the operator may ratify or reopen
Proposal: none; taken when WP-50 needed a certified feature-test header

## What was decided

The veneer's `features.h` reproduces the feature-test-macro engine of el8's
glibc 2.28 `<features.h>` verbatim — the `__USE_*` selection arithmetic is
copied line for line rather than rewritten to the same intent. The vendor
reference is the installed header from `glibc-headers-2.28-251.el8_10.40`,
pinned by sha256. What the veneer adds is the identity it reports and one
marker macro; what it does not change is a single conditional in the selection
logic.

The certified header set lives at `veneer/include/`, and that is the canonical
location. The three-file draft under `toolchain/sysroot/include/`, written for
the WP-14 bootstrap before there was a vendor header to check against, is
superseded by it.

The reference is obtained the way DR-0002 obtains all el8 material: fetched at a
pinned version, verified by checksum, cached outside version control. It is not
vendored into the repository.

## Why verbatim rather than a paraphrase

WP-14 shipped a hand-written `features.h` that paraphrased glibc's behaviour
from its documentation. It read correctly and was never diffed against a real
2.28 header, and when it finally was, it diverged: it never defined
`__GLIBC_USE_DEPRECATED_GETS` or `__USE_FORTIFY_LEVEL`, it tied `__USE_ISOC99`
and `__USE_ISOC95` to `_DEFAULT_SOURCE` where glibc ties them to the ISO C
source macros, it carried a `__USE_REENTRANT` glibc had removed, and it missed
`__USE_ATFILE` under `_XOPEN_SOURCE=700`. None of these is visible in a default
compile, which is exactly why a paraphrase is the wrong instrument: the failures
hide in the input combinations nobody happened to test, and surface later as a
package that compiled a different program than it would have on el8.

Several thousand packages branch on these macros. The only way to be right for
all of their input combinations, rather than for the ones a test matrix
remembers, is to run glibc's own arithmetic. So the arithmetic is glibc's,
copied, and the test in `veneer/t/ftm-diff.sh` certifies the copy against the
header it was copied from over a matrix of inputs. A verbatim copy makes that
test a check on transcription; a paraphrase would make it a check on
understanding, which is the thing that already failed once.

## Why this is licence-clean

glibc's `features.h` is LGPLv2.1-or-later. This tree is LGPLv3-or-later
(DR-0004). 2.1-or-later and 3-or-later combine, because the "or later" on the
glibc file reaches version 3. The file keeps glibc's copyright line and adds the
veneer's for the framing, and AGENTS.md already lists glibc among the LGPL prior
art this project may read and, with the distribution obligation met, carry. The
obligation here is the ordinary LGPL one and is met by the tree's own licence.

`stdc-predef.h` is the exception in the set: rather than copy glibc's, the
veneer authors its own, because that file carries no arithmetic, only a few
`__STDC_*` facts, and authoring it avoids a second copied file for no loss. Its
values match el8's so a probe still gets the vendor's answer.

## Why fetched rather than vendored

DR-0002 settled that el8 source is not vendored and nothing here depends on it
being present. A committed reference header would break both halves of that: it
would put vendor material in the tree and make the test depend on it. So the
test fetches the pinned package, checks its sha256, and caches the one header it
needs under a gitignored path. Reproducibility is the checksum, not a committed
copy, which is the same bargain DR-0002 struck for the whole el8 corpus.

The cost is that the test needs the network, or a local copy pointed at by
`EL8_GLIBC_HEADERS_RPM`, or a warm cache. When it has none of these it skips
with the pin printed rather than passing vacuously or failing spuriously.

## Why veneer/include is canonical and the sysroot draft is left alone

The header set belongs with the rest of the veneer (ROADMAP §10), so
`veneer/include/` is where the certified set lives and where WP-51 through WP-53
will find it. The WP-14 draft in `toolchain/sysroot/include/` is a closed
package's artifact; editing it to match would reopen a closed package for no
gain WP-50 can attest to, since WP-50 cannot rerun WP-14's link test. The
supersession is recorded instead — here, and in `veneer/README.md` — and the
sysroot's regeneration from `veneer/include/` is left as the WP-14/WP-15
follow-up it naturally is. The sysroot build is idempotent and reseeded from a
template, so that follow-up is a change of source path rather than a rewrite.

## What it does not decide

The rest of the header surface. This record is about `features.h` and the three
files it pulls in, which is WP-50's scope. `stdio.h` and the several hundred
other headers are later work and will raise their own questions.

Whether the reference should later be pinned to a specific el8 minor. It is
pinned to `-251.el8_10.40` today, the build present when WP-50 ran. A newer el8
glibc that moved `features.h` would be a new pin and, if it changed the
arithmetic, a new record pointing back here.

## What it costs to reverse

Cheap. `features.h` is one file with one consumer that has not been wired yet,
the reference is refetched not stored, and the test would repin in one edit.
Reversal is a new record pointing back here, not an edit to this one.
