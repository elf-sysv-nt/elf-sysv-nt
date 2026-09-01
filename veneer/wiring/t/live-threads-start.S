/* WP-56: entry for the live-threads specimen, the same shape as WP-27's
 * elfcall specimen (runtime/face/t/elfcall-start.S) and the earlier live
 * crossings' entries (t/live-io-mux-start.S, t/live-identity-start.S).
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
    movq %rsp, %rdi
    movq %rdx, %rsi
    call live_threads_main
    hlt

    .section .note.GNU-stack,"",@progbits
