#!/usr/bin/env bash
#
# WP-24: generate the variadic veneer from the enumeration.
#
# Reads variadic-exports.tsv and writes two artifacts, both committed and both
# reproduced byte for byte by a rerun:
#
#   veneer.gen.c   one sysv_abi entry point per format-driven variadic export.
#   veneer.gen.h   its sysv_abi declaration, the true prototype.
#
# Each wrapper is the unpack-and-repass pattern: it establishes (or, for a
# v-form, receives) a System V va_list, hands it and the format to __sv2ms_*
# which rebuilds a Microsoft va_list, and calls the MS-ABI core through the
# core.h contract. It cannot be a forward -- the two va_list types disagree at
# twenty-four bytes against eight -- so the wrapper is real code, one function
# per export, generated from its shape rather than written out by hand.
#
# Rows whose shape is PROTOTYPE are the non-format variadics (exec, open,
# fcntl, the SysV IPC ctls, and so on). They carry no format to drive the
# rebuild, each has a fixed and distinct signature, and they are hand-written
# in nonformat.c rather than generated; the generator lists them and skips
# them. See the README.
#
# Usage:
#   gen-veneer.sh [options]
#
# Options:
#   -e FILE, --exports=FILE   enumeration TSV. [default: variadic-exports.tsv]
#   --c=FILE                  veneer output. [default: veneer.gen.c]
#   --h=FILE                  header output. [default: veneer.gen.h]
#   -p STR, --prefix=STR      Prefix every defined wrapper symbol with STR. The
#                             committed artifact takes no prefix; the test
#                             generates a prefixed copy so the wrappers do not
#                             collide with the host libc's own printf family,
#                             which lets the same generator output be compiled
#                             and run without the runtime's veneer headers.
#   -V, --version             Print the version and exit.
#   -h, --help                Print this message and exit.

set -u

prog=gen-veneer
release='gen-veneer 1.0'
here=$(cd "$(dirname "$0")" && pwd)

exports=$here/variadic-exports.tsv
out_c=$here/veneer.gen.c
out_h=$here/veneer.gen.h
prefix=

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)     usage; exit 0 ;;
		-V|--version)  printf '%s\n' "$release"; exit 0 ;;
		-e|--exports)  exports=${2:-}; shift 2 ;;
		--exports=*)   exports=${1#*=}; shift ;;
		--c)           out_c=${2:-}; shift 2 ;;
		--c=*)         out_c=${1#*=}; shift ;;
		--h)           out_h=${2:-}; shift 2 ;;
		--h=*)         out_h=${1#*=}; shift ;;
		-p|--prefix)   prefix=${2:-}; shift 2 ;;
		--prefix=*)    prefix=${1#*=}; shift ;;
		-?*)           printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)             break ;;
	esac
done
[ -f "$exports" ] || die "no enumeration at $exports"

# The shapes. Each format-driven export belongs to one, which fixes four
# things: the return type, the fixed parameters before the format, which
# rebuild the format drives (a printf list or a scanf list, narrow or wide),
# and the arguments the core takes before the format. Everything else about a
# wrapper is the same, so this table plus the two templates below is the whole
# generator. A shape the enumeration names but this does not is a hard error.
shape_attrs() {
	# echoes: RET | PREFIX_PARAMS | CLASS | CORE_PREFIX
	case $1 in
	PRINTF)       echo 'int|| print |stdout' ;;
	WPRINTF)      echo 'int|| wprint |stdout' ;;
	FPRINTF)      echo 'int|FILE *__fp| print |__fp' ;;
	FWPRINTF)     echo 'int|FILE *__fp| wprint |__fp' ;;
	SPRINTF)      echo 'int|char *__s| print |__s' ;;
	SNPRINTF)     echo 'int|char *__s, size_t __n| print |__s, __n' ;;
	SWPRINTF)     echo 'int|wchar_t *__s, size_t __n| wprint |__s, __n' ;;
	DPRINTF)      echo 'int|int __fd| print |__fd' ;;
	ASPRINTF)     echo 'int|char **__sp| print |__sp' ;;
	ASNPRINTF)    echo 'char *|char *__buf, size_t *__lenp| print |__buf, __lenp' ;;
	SPRINTF_CHK)  echo 'int|char *__s, int __flag, size_t __os| print |__s, __flag, __os' ;;
	SNPRINTF_CHK) echo 'int|char *__s, size_t __n, int __flag, size_t __os| print |__s, __n, __flag, __os' ;;
	SCANF)        echo 'int|| scan |stdin' ;;
	WSCANF)       echo 'int|| wscan |stdin' ;;
	FSCANF)       echo 'int|FILE *__fp| scan |__fp' ;;
	FWSCANF)      echo 'int|FILE *__fp| wscan |__fp' ;;
	SSCANF)       echo 'int|const char *__s| scan |__s' ;;
	SWSCANF)      echo 'int|const wchar_t *__s| wscan |__s' ;;
	FSCANF_R)     echo 'int|struct _reent *__r, FILE *__fp| scan |__r, __fp' ;;
	ERR)          echo 'void|int __eval| print |__eval' ;;
	WARN)         echo 'void|| print |' ;;
	SYSLOG)       echo 'void|int __pri| print |__pri' ;;
	ERROR)        echo 'void|int __status, int __errnum| print |__status, __errnum' ;;
	ERRORL)       echo 'void|int __status, int __errnum, const char *__file, unsigned int __line| print |__status, __errnum, __file, __line' ;;
	*)            return 1 ;;
	esac
}

fmt_type() { case $1 in wprint|wscan) echo 'const wchar_t *' ;; *) echo 'const char *' ;; esac; }
bridge_fn() { case $1 in print) echo __sv2ms_print ;; wprint) echo __sv2ms_wprint ;; scan) echo __sv2ms_scan ;; wscan) echo __sv2ms_wscan ;; esac; }

# Build the fixed parameter list (everything up to and including the format),
# the core-call argument list, and the body, then print one wrapper.
emit_one() {
	name=$1 kind=$2 shape=$3 core=$4
	attrs=$(shape_attrs "$shape") || die "unknown shape $shape for $name"
	ret=$(printf '%s' "$attrs" | cut -d'|' -f1)
	pparams=$(printf '%s' "$attrs" | cut -d'|' -f2)
	class=$(printf '%s' "$attrs" | cut -d'|' -f3 | tr -d ' ')
	corepre=$(printf '%s' "$attrs" | cut -d'|' -f4)
	ft=$(fmt_type "$class")
	bridge=$(bridge_fn "$class")

	# fixed params before the variadic tail
	if [ -n "$pparams" ]; then fixed="$pparams, ${ft}__fmt"; else fixed="${ft}__fmt"; fi
	# core arguments
	if [ -n "$corepre" ]; then coreargs="$corepre, __fmt, __ms"; else coreargs="__fmt, __ms"; fi

	if [ "$kind" = valist ]; then
		params="$fixed, __sysv_va_list __ap"
	else
		params="$fixed, ..."
	fi

	sym=$prefix$name

	# declaration
	printf '__attribute__((sysv_abi)) %s %s(%s);\n' "$ret" "$sym" "$params" >> "$out_h"

	# definition
	{
		printf '__attribute__((sysv_abi))\n'
		printf '%s %s(%s)\n{\n' "$ret" "$sym" "$params"
		printf '\tunsigned long long __slots[VARARGS_MAX_SLOTS];\n'
		if [ "$kind" != valist ]; then
			printf '\t__sysv_va_list __ap;\n'
			printf '\t__sysv_va_start(__ap, __fmt);\n'
		fi
		printf '\tva_list __ms = %s(__slots, VARARGS_MAX_SLOTS, __fmt, __ap);\n' "$bridge"
		if [ "$ret" = void ]; then
			printf '\t%s(%s);\n' "$core" "$coreargs"
			[ "$kind" != valist ] && printf '\t__sysv_va_end(__ap);\n'
		else
			printf '\t%s __ret = %s(%s);\n' "$ret" "$core" "$coreargs"
			[ "$kind" != valist ] && printf '\t__sysv_va_end(__ap);\n'
			printf '\treturn __ret;\n'
		fi
		printf '}\n\n'
	} >> "$out_c"
}

# Header preamble.
{
	printf '/* Generated by gen-veneer.sh from variadic-exports.tsv. Do not edit. */\n'
	printf '/* WP-24 variadic veneer: the sysv_abi prototype of each format-driven export. */\n'
	printf '#ifndef ELFSYSV_RUNTIME_VARARGS_VENEER_GEN_H\n'
	printf '#define ELFSYSV_RUNTIME_VARARGS_VENEER_GEN_H\n\n'
	printf '#include <stddef.h>\n#include <stdio.h>\n#include <wchar.h>\n'
	printf '#include "sv2ms.h"\n\n'
	printf 'struct _reent;\n\n'
	printf '#ifdef __cplusplus\nextern "C" {\n#endif\n\n'
} > "$out_h" || die "cannot write $out_h"

# Source preamble.
{
	printf '/* Generated by gen-veneer.sh from variadic-exports.tsv. Do not edit. */\n'
	printf '/* WP-24 variadic veneer: one sysv_abi entry point per format-driven export.\n'
	printf ' *\n'
	printf ' * Each establishes or receives a System V va_list, rebuilds a Microsoft one\n'
	printf ' * with __sv2ms_* (driven by the format), and repasses through the MS-ABI core\n'
	printf ' * named in core.h. There is no forwarding: the two va_list types disagree at\n'
	printf ' * twenty-four bytes against eight. See the README and sv2ms.c.\n'
	printf ' */\n'
	printf '#include <stddef.h>\n#include <stdio.h>\n#include <wchar.h>\n'
	printf '#include "sv2ms.h"\n#include "core.h"\n\n'
} > "$out_c" || die "cannot write $out_c"

n=0 skipped=0
while IFS=$'\t' read -r name kind shape core; do
	[ -n "$name" ] || continue
	if [ "$shape" = PROTOTYPE ]; then skipped=$((skipped + 1)); continue; fi
	emit_one "$name" "$kind" "$shape" "$core"
	n=$((n + 1))
done < "$exports"

{
	printf '\n#ifdef __cplusplus\n}\n#endif\n\n'
	printf '#endif /* ELFSYSV_RUNTIME_VARARGS_VENEER_GEN_H */\n'
} >> "$out_h"

printf '%s: wrote %s and %s, %d wrappers (%d prototype-driven skipped)\n' \
	"$prog" "$(basename "$out_c")" "$(basename "$out_h")" "$n" "$skipped" >&2
