/* NT declarations winternl.h does not carry, shared by the launcher and the
 * ntdll-only child. Everything here is a public shape: the NtCreateUserProcess
 * family from the process-creation documentation and the ntinternals headers,
 * the AFD open packet and IOCTLs from the published msafd/wepoll interface,
 * the loader list from the PEB documentation. No GPL source is copied; these
 * are the field names the ABI fixes, written out so the two probes agree.
 */
#ifndef NTSHARED_H
#define NTSHARED_H

#define WIN32_NO_STATUS
#include <windows.h>
#include <winternl.h>
#include <stdint.h>

#ifndef NTAPI
#define NTAPI __stdcall
#endif

/* ---- process creation (NtCreateUserProcess and friends) ---------------- */

typedef enum _PS_CREATE_STATE {
	PsCreateInitialState,
	PsCreateFailOnFileOpen,
	PsCreateFailOnSectionCreate,
	PsCreateFailExeFormat,
	PsCreateFailMachineMismatch,
	PsCreateFailExeName,
	PsCreateSuccess,
	PsCreateMaximumStates
} PS_CREATE_STATE;

typedef struct _PS_CREATE_INFO {
	SIZE_T Size;
	PS_CREATE_STATE State;
	union {
		struct {
			union {
				ULONG InitFlags;
				struct {
					UCHAR WriteOutputOnExit : 1;
					UCHAR DetectManifest : 1;
					UCHAR IFEOSkipDebugger : 1;
					UCHAR IFEODoNotPropagateKeyState : 1;
					UCHAR SpareBits1 : 4;
					UCHAR SpareBits2 : 8;
					USHORT ProhibitedImageCharacteristics : 16;
				} s1;
			} u1;
			ACCESS_MASK AdditionalFileAccess;
		} InitState;
		struct { HANDLE FileHandle; } FailSection;
		struct { USHORT DllCharacteristics; } ExeFormat;
		struct { HANDLE IFEOKey; } ExeName;
		struct {
			union {
				ULONG OutputFlags;
				struct {
					UCHAR ProtectedProcess : 1;
					UCHAR AddressSpaceOverride : 1;
					UCHAR DevOverrideEnabled : 1;
					UCHAR ManifestDetected : 1;
					UCHAR ProtectedProcessLight : 1;
					UCHAR SpareBits1 : 3;
					UCHAR SpareBits2 : 8;
					USHORT SpareBits3 : 16;
				} s2;
			} u2;
			HANDLE FileHandle;
			HANDLE SectionHandle;
			ULONGLONG UserProcessParametersNative;
			ULONG UserProcessParametersWow64;
			ULONG CurrentParameterFlags;
			ULONGLONG PebAddressNative;
			ULONG PebAddressWow64;
			ULONGLONG ManifestAddress;
			ULONG ManifestSize;
		} SuccessState;
	} u;
} PS_CREATE_INFO, *PPS_CREATE_INFO;

typedef struct _PS_ATTRIBUTE {
	ULONG_PTR Attribute;
	SIZE_T Size;
	union {
		ULONG_PTR Value;
		PVOID ValuePtr;
	} u;
	PSIZE_T ReturnLength;
} PS_ATTRIBUTE, *PPS_ATTRIBUTE;

typedef struct _PS_ATTRIBUTE_LIST {
	SIZE_T TotalLength;
	PS_ATTRIBUTE Attributes[2];
} PS_ATTRIBUTE_LIST, *PPS_ATTRIBUTE_LIST;

#define PS_ATTRIBUTE_IMAGE_NAME 0x20005
#define PS_ATTRIBUTE_CLIENT_ID  0x10003

#define RTL_USER_PROC_PARAMS_NORMALIZED 0x00000001

#endif /* NTSHARED_H */
