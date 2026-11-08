/* overrides.h - native replacements for hot game functions.
 *
 * Included by the generated funcs.h (game.toml [translate] native): each
 * FN_<addr> below sends every direct call to that address to the C function
 * named. The translated fn_<addr> still exists; each replacement falls back to
 * it when RECOMP_NATIVE=0, and RECOMP_NATIVE_SELFTEST=1 checks the two
 * against each other on random input at start-up. */
#ifndef NFSMW_NATIVE_OVERRIDES_H
#define NFSMW_NATIVE_OVERRIDES_H

#define FN_006c9440 native_006c9440 /* clip a segment against one plane */
#define FN_006c9510 native_006c9510 /* sphere inside all six frustum planes */
#define FN_006c9570 native_006c9570 /* swept sphere against the frustum */
#define FN_006be800 native_006be800 /* a box through a matrix, as a box */
#define FN_006cf2b0 native_006cf2b0 /* a box against the frustum */
#define FN_007c4b80 native_007c4b80 /* _ftol2: ST(0) to EDX:EAX, toward zero */

/* Not hot: frame pacing and the streaming hints (native/pacing.c). Each
 * calls the translation and adds to it; RECOMP_NATIVE does not switch them
 * (core.nfsmw frame_pacing and RECOMP_NFSMW_PACING do). */
#define FN_0064cf00 native_0064cf00 /* bThreadYield: the scheduler's limiter */
#define FN_006e7220 native_006e7220 /* the frame's Present */

#endif
