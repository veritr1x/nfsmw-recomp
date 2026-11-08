/* pacing.c - frame pacing at Present, and the streaming hints' hand-off.
 *
 * FRAME PACING. The engine simulates in fixed ticks (Scheduler::Run,
 * 00642e60; core.nfsmw sets the tick to 1/sim_rate). Each main-loop pass adds
 * the time measured since the previous pass to an accumulator and runs as
 * many whole ticks as it holds. A pass that measures less than a tick asks
 * THREAD_yield (bThreadYield 0064cf00 -> 007ec298 -> SleepEx; the call returns
 * to 00642ed7) for the rest in whole milliseconds, but its time stamp was
 * taken before the sleep, so the sleep is charged to the next pass: passes
 * alternate long and short, and a short one runs no tick and shows the same
 * picture again (the 20-30% repeated frames and the two-refresh P95 in
 * docs/analysis.md). The Sleep the kit counts as returning to 007ec2a6 is
 * this one: 007ec2a6 is THREAD_yield's own call site, 00642ed7 is the
 * scheduler's call of THREAD_yield.
 *
 * With pacing on (after elforeign's nfs-most-wanted-mac runtime/pacing.c,
 * MIT): that limiter sleep is skipped, and after the main loop's Present
 * (006e7220 called from 0064a7c0, returning to 0064a7f3) the game thread waits
 * until one tick after the previous frame, so every pass measures one tick and
 * runs exactly one. The period is nudged so the accumulator's leftover
 * fraction (Scheduler +0x1c) stays near 0.5, where measurement jitter can
 * never make a pass of zero or two ticks. A frame that ran several ticks (the
 * tick counter at Scheduler +0x28 moved by more than one) is behind: no wait,
 * and the slot restarts from now. The wait is the guest's own SleepEx (so other
 * guest threads run and alertable I/O completes, as in the skipped sleep) for
 * whole milliseconds, then os_sleep_us for the rest.
 *
 * On when core.nfsmw's frame_pacing setting writes 1 to SLOT_PACING (0 when
 * the mod is absent); RECOMP_NFSMW_PACING=0|1 overrides. The kit's in-run A/B
 * (RECOMP_AB_PERIOD, switch "pacing", platform/ab_phase.h) turns it off in
 * the "earlier" phase; its phase is recomputed here from the same clock and
 * switches, because ab_phase.h is C++.
 *
 * STREAMING HINTS. core.nfsmw (stream_ahead) predicts the world sections the
 * camera is about to need and queues their byte ranges in a ring in the
 * section padding; after each Present this hands them to the web player's
 * fetch backend (recomp_web_stream_prefetch, web/player/fetch-backend.js),
 * which brings them into its cache without the guest reading them. Native
 * builds read local files and drop the ring. */
#include "funcs.h"
#include "platform/os.h"

#include <stdlib.h>
#include <string.h>

void native_0064cf00(X86 *c);
void native_006e7220(X86 *c);

#define SCHEDULER_PTR 0x009203b0u   /* Scheduler::fgScheduler */
#define SCHED_FRACTION 0x1cu        /* float: leftover ticks after Run */
#define SCHED_STEP 0x20u            /* float: seconds per tick */
#define SCHED_TICKS 0x28u           /* ticks run so far */
#define LIMITER_RET 0x00642ed7u     /* Scheduler::Run's call of bThreadYield */
#define MAIN_PRESENT_RET 0x0064a7f3u /* the main loop's call of 006e7220 */
#define IAT_SLEEPEX 0x008902c4u     /* THREAD_yield's CALL [0x008902c4] */
#define SLEEPEX_RET 0x007ec2a6u     /* ... and its return site */
#define SLOT_PACING 0x00a37b60u     /* written by core.nfsmw */
#define STREAM_RING_HEAD 0x00a37b70u
#define STREAM_RING_TAIL 0x00a37b74u
#define STREAM_RING 0x00a37c00u     /* 32 x {name address, offset, length} */
#define STREAM_RING_SIZE 32u

/* ---- switches ------------------------------------------------------------ */

static int pacing_enabled(void) {
    static int env = -2;
    if (env == -2) {
        const char *e = recomp_env("NFSMW_PACING");
        env = e && *e ? (e[0] != '0') : -1;
    }
    if (env >= 0)
        return env;
    return rd32(SLOT_PACING) == 1u;
}

/* recomp_ab::old(PACING): the earlier behaviour this period. */
static int ab_earlier(uint64_t now) {
    static int read = 0, want = 0, force = 0;
    static uint64_t period = 0;
    if (!read) {
        read = 1;
        const char *p = recomp_env("AB_PERIOD");
        long s = p ? strtol(p, NULL, 10) : 0;
        period = s > 0 ? (uint64_t)s * 1000000000ull : 0;
        const char *w = recomp_env("AB_SWITCHES");
        want = !w || !*w || strstr(w, "pacing") != NULL;
        const char *f = recomp_env("AB_FORCE_OLD");
        force = f && !strcmp(f, "1");
    }
    if (!want)
        return 0;
    if (force)
        return 1;
    return period ? (int)((now / period) & 1u) : 0;
}

/* ---- the wait ------------------------------------------------------------ */

static uint64_t g_last_present; /* when the main loop last presented */
static uint64_t g_deadline;     /* the next frame's slot */
static uint32_t g_ticks_at_present;
static int g_active;            /* pacing applied to the frame now running */

/* SleepEx(ms, TRUE) through the guest's import, as THREAD_yield calls it: the
 * scheduler hands the baton on meanwhile. Registers are as they were. */
static void guest_sleep_ms(X86 *c, uint32_t ms) {
    uint32_t r[8], eip = c->eip;
    uint32_t flags[7] = {c->eflags_cf, c->eflags_zf, c->eflags_sf, c->eflags_of,
                         c->eflags_pf, c->eflags_af, c->eflags_df};
    memcpy(r, c->r, sizeof r);
    c->r[R_ESP] -= 4;
    wr32(c->r[R_ESP], 1u); /* alertable */
    c->r[R_ESP] -= 4;
    wr32(c->r[R_ESP], ms);
    c->r[R_ESP] -= 4;
    wr32(c->r[R_ESP], SLEEPEX_RET);
    recomp_call(c, rd32(IAT_SLEEPEX));
    memcpy(c->r, r, sizeof r);
    c->eip = eip;
    c->eflags_cf = flags[0];
    c->eflags_zf = flags[1];
    c->eflags_sf = flags[2];
    c->eflags_of = flags[3];
    c->eflags_pf = flags[4];
    c->eflags_af = flags[5];
    c->eflags_df = flags[6];
}

static void wait_until(X86 *c, uint64_t deadline) {
    uint64_t now = os_monotonic_ns();
    if (deadline <= now)
        return;
    /* Whole milliseconds in the guest's sleep, leaving at least half a
     * millisecond; a short wait still yields once (SleepEx(0)). */
    uint64_t left = deadline - now;
    uint32_t ms = left > 1500000u ? (uint32_t)((left - 500000u) / 1000000u) : 0u;
    guest_sleep_ms(c, ms);
    now = os_monotonic_ns();
    if (deadline > now)
        os_sleep_us((deadline - now + 999u) / 1000u);
}

static void pace_after_present(X86 *c) {
    uint64_t now = os_monotonic_ns();
    uint32_t sched = rd32(SCHEDULER_PTR);
    g_active = 0;
    if (!sched || !pacing_enabled() || ab_earlier(now))
        return;
    float step = rdf32(sched + SCHED_STEP), frac = rdf32(sched + SCHED_FRACTION);
    if (!(step > 0.001f && step < 0.1f) || !(frac >= 0.0f && frac <= 1.0f))
        return;
    g_active = 1;
    g_last_present = now;
    uint32_t ticks = rd32(sched + SCHED_TICKS);
    int behind = ticks - g_ticks_at_present > 1u;
    g_ticks_at_present = ticks;
    uint64_t period = (uint64_t)((double)step * 1e9);
    if (behind) {
        /* The frame needed more than one tick: recover without a wait. */
        g_deadline = now;
        return;
    }
    int64_t adjust = (int64_t)((0.5f - frac) * 0.1f * (float)period);
    g_deadline += (uint64_t)((int64_t)period + adjust);
    if (g_deadline + period < now || g_deadline > now + 2 * period)
        g_deadline = now; /* a frame behind, or the first one */
    wait_until(c, g_deadline);
}

/* ---- streaming hints ---------------------------------------------------- */

#ifdef __EMSCRIPTEN__
/* web/player/fetch-backend.js */
extern void recomp_web_stream_prefetch(const char *path, double offset, double length);
#endif

static void stream_drain(void) {
    uint32_t head = rd32(STREAM_RING_HEAD), tail = rd32(STREAM_RING_TAIL);
    if (head == tail)
        return;
    for (uint32_t n = 0; tail != head && n < STREAM_RING_SIZE; ++n, ++tail) {
        uint32_t e = STREAM_RING + 12u * (tail % STREAM_RING_SIZE);
        uint32_t name = rd32(e), offset = rd32(e + 4), length = rd32(e + 8);
        char path[64];
        uint32_t i = 0;
        for (; i < sizeof path - 1; ++i) {
            char ch = (char)rd8(name + i);
            if (!ch)
                break;
            path[i] = ch == '\\' ? '/' : ch;
        }
        path[i] = 0;
#ifdef __EMSCRIPTEN__
        if (i && length)
            recomp_web_stream_prefetch(path, (double)offset, (double)length);
#else
        (void)path;
        (void)offset;
        (void)length;
#endif
    }
    wr32(STREAM_RING_TAIL, head);
}

/* ---- the replaced calls --------------------------------------------------- */

/* bThreadYield(ms), cdecl. Skipped for the scheduler's limiter while the main
 * loop presents paced frames; the wait after Present replaces it. Anything
 * else, or a loop that stopped presenting, sleeps as before. */
void native_0064cf00(X86 *c) {
    uint32_t esp = c->r[R_ESP];
    if (g_active && rd32(esp) == LIMITER_RET) {
        uint32_t sched = rd32(SCHEDULER_PTR);
        float step = sched ? rdf32(sched + SCHED_STEP) : 0.0f;
        uint64_t now = os_monotonic_ns();
        if (step > 0.001f && step < 0.1f && now - g_last_present < (uint64_t)((double)step * 3e9)) {
            c->eip = LIMITER_RET;
            c->r[R_ESP] = esp + 4;
            return;
        }
    }
    fn_0064cf00(c);
}

/* The frame's end: device Present and lost-device handling. */
void native_006e7220(X86 *c) {
    uint32_t ret = rd32(c->r[R_ESP]);
    fn_006e7220(c);
    if (ret == MAIN_PRESENT_RET)
        pace_after_present(c);
    stream_drain();
}
