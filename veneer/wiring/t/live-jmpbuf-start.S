/* WP-56: entry for the live-jmpbuf specimen, the same shape as
 * live-runtime's (t/live-runtime-start.S) and live-math's before it -- the
 * entry contract is generic across freestanding specimens driven through
 * WP-41's branch, not specific to any one slice.
 *
 * The entry contract delivers argc at (%rsp) and the termination handler in
 * %rdx. This hands both to the C body and never expects it back: the body
 * leaves through the handler with a status in %rdi, the same protocol as
 * WP-41's specimen. The call leaves %rsp eight past sixteen-aligned, which
 * is exactly the state the psABI promises a function on entry.
 */
	.text
	.globl _start
_start:
	movq	%rsp, %rdi
	movq	%rdx, %rsi
	call	live_jmpbuf_main
	hlt

	.section .note.GNU-stack,"",@progbits
