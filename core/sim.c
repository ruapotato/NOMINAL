/* sim.c — the ship.
 *
 * Nobody is aboard. The ship is a chassis with slots, and you are the software
 * that administers it. Nothing below is hardcoded to a subsystem: the physics
 * reads whatever cards are installed, so pulling the sensor removes
 * /dev/sensor and every script that touched it starts failing. See D13.
 *
 * The loop everything hangs off:
 *
 *     power -> instructions -> waste heat -> warm optics
 *                                  |
 *                                  +-> too much heat throttles the computer
 *                                  +-> and ages the ship, which makes less power
 */
#include "nom.h"
#include "lang.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

bool wreck_mount(Sim *s, const char *name, const char *at, char *err, size_t errsz);
void wreck_reset(void);

/* ---------------------------------------------------------------- dials */
#define REACTOR_PRIME_TICKS   20
#define REACTOR_SPINUP_TICKS  30
#define REACTOR_PRIME_DRAW    0.3

#define BAY_START            -12.0
#define BAY_AMBIENT          -40.0
#define HEAT_PER_KINSTR       0.60   /* per 1000 instructions actually run */
#define HEAT_LEAK             0.020
#define HEAT_RATE             0.50
#define BAY_THROTTLE_TEMP     45.0
#define BAY_ANNEAL_TEMP       25.0
#define BAY_FAIL_TEMP        110.0
#define HULL_FREEZE_TEMP     -25.0

#define SENSOR_CLEAR_TEMP     12.0
#define SENSOR_ERROR_SPAN     25.0

#define HELM_DAMPING          0.985
#define BEACON_WIN_RANGE      60.0
#define DELIVERY_PAY          500
#define FAULT_MIN_GAP         700
#define FAULT_MAX_GAP        1400
#define SYM_RENOTIFY          80
#define O2_CONSUME            0.040   /* you breathing */
#define O2_START              82.0
/* The maintenance controller: a small battery-backed core that runs whether or
 * not the ship has power. Without it there is no way to bootstrap — the script
 * that turns the reactor on would need the reactor to be on. Every real
 * machine has one of these and every sysadmin has been grateful for it. */
#define BMC_POOL              240

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---------------------------------------------------------------- events */
void sim_log(Sim *s, const char *fmt, ...)
{
    if (s->nevents >= SIM_MAX_EVENTS) return;
    SimEvent *e = &s->event[s->nevents++];
    e->tick = s->tick;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e->text, sizeof e->text, fmt, ap);
    va_end(ap);

    /* logs accumulate on disk the way they would on a real box */
    VNode *lg = vfs_lookup(&s->fs, "/var/log/messages");
    if (lg && lg->kind == VN_FILE && lg->data.len < 262144) {
        char line[200];
        int n = snprintf(line, sizeof line, "%llu %s\n", (unsigned long long)e->tick, e->text);
        buf_put(&lg->data, line, (size_t)n);
    }
}

static const char *SYM_NAME[SYM_COUNT] = {
    "bearing_unstable", "bay_overheating", "power_shortfall", "hardware_fault", "air_falling", "compute_starved"
};

static void alarm_push(Sim *s, const char *msg)
{
    int next = (s->alarm_tail + 1) % 16;
    if (next == s->alarm_head) return;
    snprintf(s->alarm_q[s->alarm_tail], sizeof s->alarm_q[0], "%s", msg);
    s->alarm_tail = next;
}

static bool alarm_pop(Sim *s, Buf *out)
{
    if (s->alarm_head == s->alarm_tail) return false;
    buf_puts(out, s->alarm_q[s->alarm_head]);   /* one token, no trailing newline */
    s->alarm_head = (s->alarm_head + 1) % 16;
    return true;
}

static void symptom_set(Sim *s, SymptomId id, bool on)
{
    if (on && !s->symptom[id]) {
        sim_log(s, "SYMPTOM: %s", SYM_NAME[id]);
        alarm_push(s, SYM_NAME[id]);
        s->symptom_since[id] = s->tick;
    } else if (on && s->tick - s->symptom_since[id] >= SYM_RENOTIFY) {
        alarm_push(s, SYM_NAME[id]);
        s->symptom_since[id] = s->tick;
    }
    s->symptom[id] = on;
}

/* ------------------------------------------------------------ slot helpers */
static const PartSpec *slot_spec(Sim *s, int i)
{
    if (i < 0 || i >= SLOT_COUNT || s->slot[i].part < 0) return NULL;
    int n;
    const PartSpec *c = catalog(&n);
    if (s->slot[i].part >= n) return NULL;
    return &c[s->slot[i].part];
}

static bool slot_live(Sim *s, int i)
{
    Slot *sl = &s->slot[i];
    if (sl->part < 0 || !sl->enabled || sl->state == SLOT_FAILED) return false;
    return true;
}

/* Which link (rail / spine) a slot is patched into for a given need, and how
 * loaded that link is. Saturating a shared rail degrades EVERY device on it,
 * not just the one you added last — which is how a cable you patched in
 * segment 5 becomes a cold lab in segment 4. See D15. */
static double link_load(Sim *s, int linkslot, PartKind kind)
{
    bool power = (kind == K_PWRBUS);
    double load = 0.0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (!p || !s->slot[i].enabled || s->slot[i].state == SLOT_FAILED) continue;
        if (s->slot[i].link[power ? 0 : 1] != linkslot) continue;
        load += power ? p->draw : p->data;
    }
    /* Segments hang off the same rails and spines as the hardware does. If
     * this did not count them, `trace` would report headroom on a rail that is
     * visibly starving three tenants — a diagnostic that lies is worse than
     * none at all. */
    for (int i = 0; i < s->nsegs; i++) {
        if (!s->seg[i].docked) continue;
        if ((power ? s->seg[i].rail : s->seg[i].spine) != linkslot) continue;
        load += power ? s->seg[i].power_need : s->seg[i].data_need;
    }
    return load;
}

/* 1.0 while the link has headroom; below that, shared proportionally. */
static double link_derate(Sim *s, int linkslot, PartKind kind)
{
    const PartSpec *lp = slot_spec(s, linkslot);
    if (!lp || lp->kind != kind) return 0.0;
    if (!s->slot[linkslot].enabled || s->slot[linkslot].state == SLOT_FAILED) return 0.0;
    double cap = lp->spec * s->slot[linkslot].health;
    double load = link_load(s, linkslot, kind);
    if (load <= cap || load <= 0.0) return 1.0;
    return cap / load;
}

/* The derate a slot suffers from the links it is patched into. A slot that
 * needs a utility and is not patched into one gets nothing at all. */
static double slot_link_factor(Sim *s, int i)
{
    const PartSpec *p = slot_spec(s, i);
    if (!p) return 0.0;
    double f = 1.0;
    if (p->needs & NEEDS_PWR) {
        int l = s->slot[i].link[0];
        if (l < 0) return 0.0;
        f *= link_derate(s, l, K_PWRBUS);
    }
    if (p->needs & NEEDS_DATA) {
        int l = s->slot[i].link[1];
        if (l < 0) return 0.0;
        f *= link_derate(s, l, K_DATABUS);
    }
    return f;
}

/* How well a card is doing its job: duty, health, degradation, and the power
 * it actually received. Every subsystem below is scaled by this one number. */
static double slot_effect(Sim *s, int i)
{
    Slot *sl = &s->slot[i];
    const PartSpec *p = slot_spec(s, i);
    if (!p || !slot_live(s, i)) return 0.0;
    double powered = (p->draw > 0.0) ? clampd(sl->supplied / p->draw, 0.0, 1.0) : 1.0;
    double degrade = (sl->state == SLOT_DEGRADED) ? 0.45 : 1.0;
    return clampd(sl->duty, 0.0, 1.0) * sl->health * powered * degrade
         * slot_link_factor(s, i);
}

const PartSpec *sim_part_of_slot(Sim *s, int i) { return slot_spec(s, i); }
const char     *sim_dev_of_slot(Sim *s, int i)
{
    return (i >= 0 && i < SLOT_COUNT && s->slot[i].part >= 0) ? s->slot[i].dev : "-";
}

/* A consumer's share of a shared link. Segments and cards compete on the same
 * rails, which is what makes "which rail do I use" the decision. */
double sim_link_load_of(Sim *s, int linkslot, bool power)
{
    return link_load(s, linkslot, power ? K_PWRBUS : K_DATABUS);
}

double sim_link_share(Sim *s, int linkslot, double want, bool power)
{
    const PartSpec *lp = slot_spec(s, linkslot);
    PartKind k = power ? K_PWRBUS : K_DATABUS;
    if (!lp || lp->kind != k) return 0.0;
    if (!s->slot[linkslot].enabled || s->slot[linkslot].state == SLOT_FAILED) return 0.0;
    double cap = lp->spec * s->slot[linkslot].health;
    double load = link_load(s, linkslot, k);
    double share = (load > cap && load > 0.0) ? cap / load : 1.0;

    /* A rail can only pass along what the powerplant actually makes. This is
     * what gives the choke points an order: you are reactor-limited first,
     * then rail-limited, then spine-limited, and each one needs a different
     * purchase to clear. */
    if (power) {
        double demand = 0.0;
        for (int i = 0; i < SLOT_COUNT; i++) {
            const PartSpec *dp = slot_spec(s, i);
            if (dp && s->slot[i].enabled && s->slot[i].state != SLOT_FAILED) demand += dp->draw;
        }
        for (int i = 0; i < s->nsegs; i++)
            if (s->seg[i].docked) demand += s->seg[i].power_need;
        if (demand > s->reactor.output && demand > 0.0) {
            double sup = s->reactor.output / demand;
            if (sup < share) share = sup;
        }
    }
    (void)want;
    return share;
}

int sim_slot_of_dev(Sim *s, const char *dev)
{
    for (int i = 0; i < SLOT_COUNT; i++)
        if (s->slot[i].part >= 0 && strcmp(s->slot[i].dev, dev) == 0) return i;
    return -1;
}

static int first_of_kind(Sim *s, PartKind k)
{
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (p && p->kind == k) return i;
    }
    return -1;
}

static double sum_effect_spec(Sim *s, PartKind k)
{
    double total = 0.0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (p && p->kind == k) total += p->spec * slot_effect(s, i);
    }
    return total;
}

/* ------------------------------------------------------------------ devices */
enum {
    DEV_SLOT_STATUS = 1, DEV_SLOT_CTL,
    DEV_BUS_STATUS, DEV_BUS_CTL, DEV_BUS_CHANNEL,
    DEV_REACTOR_CTL, DEV_REACTOR_STATUS,
    DEV_CPU_STATUS,
    DEV_SENSOR_STATUS, DEV_SENSOR_CONTACTS, DEV_SENSOR_CTL,
    DEV_HULL_STATUS,
    DEV_TIME, DEV_LOG, DEV_ALARM, DEV_MISSION_STATUS, DEV_LIFE_STATUS, DEV_MSG,
    DEV_RADIATOR_CTL,
    DEV_SEG_HEARTBEAT, DEV_SEG_STATUS,
    DEV_FIELD
};

static const char *reactor_state_name(ReactorState st)
{
    switch (st) {
    case REACTOR_COLD: return "cold";       case REACTOR_PRIMING:  return "priming";
    case REACTOR_IDLE: return "idle";       case REACTOR_SPINUP:   return "spinup";
    case REACTOR_ONLINE: return "online";   case REACTOR_SCRAMMED: return "scrammed";
    }
    return "?";
}

static const char *slot_state_name(SlotState st)
{
    switch (st) {
    case SLOT_EMPTY: return "empty";  case SLOT_OK:     return "ok";
    case SLOT_DEGRADED: return "degraded"; case SLOT_FAILED: return "failed";
    }
    return "?";
}

static void kv(Buf *b, const char *k, double val, int dec)
{
    buf_puts(b, k); buf_putc(b, ' ');
    buf_putnum(b, val, dec);
    buf_putc(b, '\n');
}

static IoStatus render_slot(Sim *s, int i, Buf *out)
{
    Slot *sl = &s->slot[i];
    const PartSpec *p = slot_spec(s, i);
    if (!p) { buf_puts(out, "state empty\n"); return IO_OK; }
    buf_printf(out, "part %s\n", p->id);
    buf_printf(out, "name %s\n", p->name);
    buf_printf(out, "kind %s\n", part_kind_name(p->kind));
    buf_printf(out, "state %s\n", slot_state_name(sl->state));
    kv(out, "health", sl->health * 100.0, 1);
    buf_printf(out, "enabled %d\n", sl->enabled ? 1 : 0);
    kv(out, "duty", sl->duty, 2);
    kv(out, "draw", p->draw, 2);
    kv(out, "supplied", sl->supplied, 2);
    kv(out, "spec", p->spec, 2);
    buf_printf(out, "firmware %d\n", sl->firmware);
    buf_printf(out, "firmware_latest %d\n", p->firmware);
    kv(out, "effect", slot_effect(s, i) * 100.0, 1);
    buf_printf(out, "slot %d\n", i);
    return IO_OK;
}

static IoStatus render_status(Sim *s, int id, int slotno, Buf *out)
{
    switch (id) {
    case DEV_SLOT_STATUS:
        return render_slot(s, slotno, out);

    case DEV_BUS_STATUS: {
        kv(out, "supply", s->reactor.output, 2);
        kv(out, "rated",  s->reactor.rated, 2);
        buf_printf(out, "reactor %s\n", reactor_state_name(s->reactor.state));
        kv(out, "battery", s->bus.charge, 2);
        kv(out, "capacity", s->bus.capacity, 1);
        buf_printf(out, "brownout %d\n", s->bus.brownout ? 1 : 0);
        double demand = 0.0;
        for (int i = 0; i < SLOT_COUNT; i++) demand += s->slot[i].request;
        kv(out, "demand", demand, 2);
        for (int i = 0; i < SLOT_COUNT; i++) {
            if (s->slot[i].part < 0) continue;
            buf_printf(out, "%s ", s->slot[i].dev);
            buf_putnum(out, s->slot[i].supplied, 2);
            buf_putc(out, '\n');
        }
        return IO_OK;
    }

    case DEV_REACTOR_STATUS:
        buf_printf(out, "state %s\n", reactor_state_name(s->reactor.state));
        kv(out, "output", s->reactor.output, 2);
        kv(out, "rated",  s->reactor.rated, 2);
        kv(out, "timer",  (double)s->reactor.timer, 0);
        buf_printf(out, "ready %d\n",
                   (s->reactor.state == REACTOR_IDLE || s->reactor.state == REACTOR_SPINUP
                    || s->reactor.state == REACTOR_ONLINE) ? 1 : 0);
        return IO_OK;

    case DEV_CPU_STATUS:
        buf_printf(out, "pool %d\n", s->cpu.pool);
        buf_printf(out, "rated %d\n", s->cpu.rated);
        buf_printf(out, "per_script %d\n", s->cpu.per_script);
        buf_printf(out, "spare %d\n", s->cpu.spare);
        buf_printf(out, "executed %llu\n", (unsigned long long)s->cpu.executed);
        kv(out, "bay_temp", s->cpu.bay_temp, 2);
        buf_printf(out, "throttled %d\n", s->cpu.throttled ? 1 : 0);
        kv(out, "heat_in", s->cpu.heat_in, 3);
        kv(out, "heat_out", s->cpu.heat_out, 3);
        return IO_OK;

    case DEV_SENSOR_STATUS: {
        int si = first_of_kind(s, K_SENSOR);
        buf_printf(out, "online %d\n", s->sensor.online ? 1 : 0);
        buf_printf(out, "warmup %d\n", s->sensor.warmup);
        kv(out, "temp", s->cpu.bay_temp, 2);
        buf_printf(out, "calibrated %d\n",
                   (s->sensor.online && nom_fabs(s->sensor.bias_full) < 3.0) ? 1 : 0);
        buf_printf(out, "installed %d\n", si >= 0 ? 1 : 0);
        buf_printf(out, "fault %d\n", (si >= 0 && s->slot[si].state != SLOT_OK) ? 1 : 0);
        kv(out, "drift", nom_fabs(s->sensor.bias_full), 2);
        return IO_OK;
    }

    case DEV_SENSOR_CONTACTS:
        if (!s->sensor.online || !s->sensor.has_reading) return IO_BLOCK;
        buf_puts(out, "contact waypoint\n");
        kv(out, "bearing", s->sensor.last_bearing, 2);
        kv(out, "range", s->sensor.last_range, 2);
        kv(out, "age", (double)(s->tick - s->sensor.last_sample_tick), 0);
        return IO_OK;

    case DEV_HULL_STATUS:
        kv(out, "integrity", s->hull_integrity, 2);
        kv(out, "temp", s->cpu.bay_temp, 2);
        return IO_OK;

    case DEV_TIME:
        buf_printf(out, "%llu\n", (unsigned long long)s->tick);
        return IO_OK;

    case DEV_ALARM:
        if (!alarm_pop(s, out)) return IO_BLOCK;
        return IO_OK;

    /* Blocks until somebody messages you. A script can wait on the pager the
     * same way you can. */
    case DEV_MSG: {
        for (int i = 0; i < s->nmsgs; i++) {
            if (!s->msg[i].unread) continue;
            s->msg[i].unread = false;
            buf_printf(out, "%s: %s", s->msg[i].from, s->msg[i].text);
            return IO_OK;
        }
        return IO_BLOCK;
    }

    case DEV_LIFE_STATUS:
        kv(out, "o2", s->o2, 2);
        kv(out, "rate", s->o2_rate, 3);
        kv(out, "consume", O2_CONSUME, 3);
        kv(out, "ticks_left", s->o2_rate >= O2_CONSUME ? 99999.0
                            : s->o2 / (O2_CONSUME - s->o2_rate), 0);
        return IO_OK;

    /* /srv/<seg>/status — what this tenant needs and how it is doing. */
    case DEV_SEG_STATUS: {
        if (slotno < 0 || slotno >= s->nsegs) return IO_ERR;
        Segment *sg = &s->seg[slotno];
        buf_printf(out, "name %s\n", sg->name);
        buf_printf(out, "kind %s\n", sg->kind);
        kv(out, "service",  sg->service * 100.0, 0);
        kv(out, "freshness", sim_freshness(s, slotno) * 100.0, 0);
        kv(out, "age", (double)(s->tick - sg->last_served), 0);
        kv(out, "power_need", sg->power_need, 2);
        kv(out, "data_need",  sg->data_need, 2);
        kv(out, "cpu_need",   (double)sg->cpu_need, 0);
        kv(out, "pays",       sg->pay, 2);
        return IO_OK;
    }

    case DEV_MISSION_STATUS:
        buf_printf(out, "segments %d\n", s->nsegs);
        kv(out, "credits", s->credits, 1);
        kv(out, "income", s->income_last, 2);
        kv(out, "power_bill", s->power_bill_last, 2);
        kv(out, "wear", s->wear * 100.0, 1);
        kv(out, "elapsed", (double)s->tick, 0);
        for (int i = 0; i < SYM_COUNT; i++)
            buf_printf(out, "%s %d\n", SYM_NAME[i], s->symptom[i] ? 1 : 0);
        return IO_OK;
    }
    return IO_ERR;
}

static void mkfield(Sim *s, const char *dir, const char *field, int srcid, int slotno);

static IoStatus dev_read(VNode *n, Buf *out, void *ctx)
{
    Sim *s = (Sim *)ctx;
    IoStatus st = render_status(s, n->id, n->src, out);
    if (st == IO_ERR) snprintf(s->fs.err, sizeof s->fs.err, "%s: device is write-only", n->name);
    return st;
}

/* A field file renders its source device and picks one line out of it. The
 * node's own name is the key, so adding a field to a status block and adding a
 * file are the same edit. */
static IoStatus dev_field_read(VNode *n, Buf *out, void *ctx)
{
    Sim *s = (Sim *)ctx;
    Buf all; buf_init(&all);
    IoStatus st = render_status(s, n->src, (int)n->last_read_tick, &all);
    if (st != IO_OK) {
        if (st == IO_ERR) snprintf(s->fs.err, sizeof s->fs.err, "%s: no source device", n->name);
        buf_free(&all);
        return st;
    }
    size_t klen = strlen(n->name);
    const char *p = all.p ? all.p : "";
    while (*p) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        if ((size_t)(e - p) > klen && memcmp(p, n->name, klen) == 0 && p[klen] == ' ') {
            buf_put(out, p + klen + 1, (size_t)(e - p) - klen - 1);
            buf_free(&all);
            return IO_OK;
        }
        p = *e ? e + 1 : e;
    }
    buf_free(&all);
    snprintf(s->fs.err, sizeof s->fs.err, "%s: no such field", n->name);
    return IO_ERR;
}

static int word_split(const char *data, size_t len, char w[4][40])
{
    int n = 0; size_t i = 0;
    while (i < len && n < 4) {
        while (i < len && (data[i]==' '||data[i]=='\t'||data[i]=='\n'||data[i]=='\r')) i++;
        if (i >= len) break;
        size_t st = i;
        while (i < len && data[i]!=' ' && data[i]!='\t' && data[i]!='\n' && data[i]!='\r') i++;
        size_t l = i - st; if (l > 39) l = 39;
        memcpy(w[n], data + st, l); w[n][l] = 0; n++;
    }
    return n;
}

static bool parse_double(const char *s, double *out)
{
    Value v;
    if (!nom_parse_number(s, strlen(s), &v)) return false;
    *out = val_num(v);
    return true;
}

static void write_slot_conf(Sim *s, int i);

static IoStatus dev_write(VNode *n, const char *data, size_t len, void *ctx)
{
    Sim *s = (Sim *)ctx;
    char w[4][40];
    int nw = word_split(data, len, w);

    switch (n->id) {
    case DEV_REACTOR_CTL: {
        if (!nw) { snprintf(s->fs.err, sizeof s->fs.err, "reactor: empty command"); return IO_ERR; }
        if (first_of_kind(s, K_REACTOR) < 0) {
            snprintf(s->fs.err, sizeof s->fs.err, "reactor: no reactor card installed");
            return IO_ERR;
        }
        if (strcmp(w[0], "prime") == 0) {
            if (s->reactor.state != REACTOR_COLD && s->reactor.state != REACTOR_SCRAMMED) {
                snprintf(s->fs.err, sizeof s->fs.err, "reactor: already primed"); return IO_ERR;
            }
            s->reactor.state = REACTOR_PRIMING; s->reactor.timer = REACTOR_PRIME_TICKS;
            sim_log(s, "reactor: priming, %d ticks on battery", REACTOR_PRIME_TICKS);
            return IO_OK;
        }
        if (strcmp(w[0], "start") == 0) {
            if (s->reactor.state != REACTOR_IDLE) {
                snprintf(s->fs.err, sizeof s->fs.err, "reactor: not primed (state %s)",
                         reactor_state_name(s->reactor.state));
                return IO_ERR;
            }
            s->reactor.state = REACTOR_SPINUP; s->reactor.timer = REACTOR_SPINUP_TICKS;
            sim_log(s, "reactor: spinup, %d ticks", REACTOR_SPINUP_TICKS);
            return IO_OK;
        }
        if (strcmp(w[0], "scram") == 0) {
            s->reactor.state = REACTOR_SCRAMMED; s->reactor.output = 0; s->reactor.timer = 0;
            sim_log(s, "reactor: SCRAM");
            return IO_OK;
        }
        snprintf(s->fs.err, sizeof s->fs.err, "reactor: prime|start|scram, not '%s'", w[0]);
        return IO_ERR;
    }

    case DEV_BUS_CHANNEL: {
        int i = sim_slot_of_dev(s, n->name);
        if (i < 0) { snprintf(s->fs.err, sizeof s->fs.err, "bus: no device '%s'", n->name); return IO_ERR; }
        const PartSpec *p = slot_spec(s, i);
        double amt;
        if (!nw || !parse_double(w[0], &amt)) {
            snprintf(s->fs.err, sizeof s->fs.err, "%s: expected MW (0..%g)", n->name, p ? p->draw : 0.0);
            return IO_ERR;
        }
        s->slot[i].request = clampd(amt, 0.0, p ? p->draw : 0.0);
        return IO_OK;
    }

    case DEV_BUS_CTL: {
        if (nw >= 2 && strcmp(w[0], "all") == 0) {
            double amt;
            if (!parse_double(w[1], &amt)) { snprintf(s->fs.err, sizeof s->fs.err, "bus: not a number"); return IO_ERR; }
            for (int i = 0; i < SLOT_COUNT; i++) {
                const PartSpec *p = slot_spec(s, i);
                if (p) s->slot[i].request = clampd(amt, 0.0, p->draw);
            }
            return IO_OK;
        }
        if (nw >= 2) {
            int i = sim_slot_of_dev(s, w[0]);
            if (i < 0) { snprintf(s->fs.err, sizeof s->fs.err, "bus: no device '%s'", w[0]); return IO_ERR; }
            double amt;
            if (!parse_double(w[1], &amt)) { snprintf(s->fs.err, sizeof s->fs.err, "bus: not a number"); return IO_ERR; }
            const PartSpec *p = slot_spec(s, i);
            s->slot[i].request = clampd(amt, 0.0, p ? p->draw : 0.0);
            return IO_OK;
        }
        snprintf(s->fs.err, sizeof s->fs.err, "bus: expected '<device> <MW>' or 'all <MW>'");
        return IO_ERR;
    }

    /* The physical bay: how you repair, reseat, reflash and throttle a card. */
    case DEV_SLOT_CTL: {
        int i = n->src;
        Slot *sl = &s->slot[i];
        const PartSpec *p = slot_spec(s, i);
        if (!nw) { snprintf(s->fs.err, sizeof s->fs.err, "slot%d: empty command", i); return IO_ERR; }
        if (!p)  { snprintf(s->fs.err, sizeof s->fs.err, "slot%d: nothing installed", i); return IO_ERR; }

        if (strcmp(w[0], "enable") == 0)  { sl->enabled = true;  write_slot_conf(s, i); return IO_OK; }
        if (strcmp(w[0], "disable") == 0) { sl->enabled = false; sl->request = 0; write_slot_conf(s, i); return IO_OK; }
        if (strcmp(w[0], "duty") == 0 && nw >= 2) {
            double d;
            if (!parse_double(w[1], &d)) { snprintf(s->fs.err, sizeof s->fs.err, "slot%d: duty wants 0..1", i); return IO_ERR; }
            sl->duty = clampd(d, 0.0, 1.0);
            write_slot_conf(s, i);
            return IO_OK;
        }
        if (strcmp(w[0], "reseat") == 0) {
            if (sl->state == SLOT_DEGRADED) {
                sl->state = SLOT_OK;
                sim_log(s, "%s: reseated, fault cleared", sl->dev);
                return IO_OK;
            }
            snprintf(s->fs.err, sizeof s->fs.err, "%s: reseat did not help (state %s)",
                     sl->dev, slot_state_name(sl->state));
            return IO_ERR;
        }
        if (strcmp(w[0], "flash") == 0) {
            if (sl->firmware >= p->firmware) {
                snprintf(s->fs.err, sizeof s->fs.err, "%s: already at firmware %d", sl->dev, sl->firmware);
                return IO_ERR;
            }
            sl->firmware = p->firmware;
            sim_log(s, "%s: flashed to firmware %d", sl->dev, sl->firmware);
            return IO_OK;
        }
        snprintf(s->fs.err, sizeof s->fs.err, "slot%d: enable|disable|duty <0..1>|reseat|flash", i);
        return IO_ERR;
    }

    case DEV_SENSOR_CTL: {
        int si = first_of_kind(s, K_SENSOR);
        if (si < 0) { snprintf(s->fs.err, sizeof s->fs.err, "sensor: none installed"); return IO_ERR; }
        if (nw && strcmp(w[0], "calibrate") == 0) {
            const PartSpec *p = slot_spec(s, si);
            s->sensor.warmup = (int)p->spec2;
            s->sensor.online = false;
            s->sensor.bias_full = (rng_unit(&s->rng) * 2.0 - 1.0) * 1.5;
            sim_log(s, "%s: recalibrating, %d ticks offline", s->slot[si].dev, s->sensor.warmup);
            return IO_OK;
        }
        snprintf(s->fs.err, sizeof s->fs.err, "sensor: expected 'calibrate'");
        return IO_ERR;
    }

    case DEV_RADIATOR_CTL: {
        int ri = first_of_kind(s, K_RADIATOR);
        if (ri < 0) { snprintf(s->fs.err, sizeof s->fs.err, "radiator: none installed"); return IO_ERR; }
        if (nw && strcmp(w[0], "purge") == 0) {
            if (s->slot[ri].state == SLOT_DEGRADED) {
                s->slot[ri].state = SLOT_OK;
                sim_log(s, "%s: purged, heat rejection restored", s->slot[ri].dev);
                return IO_OK;
            }
            snprintf(s->fs.err, sizeof s->fs.err, "%s: nothing to purge", s->slot[ri].dev);
            return IO_ERR;
        }
        snprintf(s->fs.err, sizeof s->fs.err, "radiator: expected 'purge'");
        return IO_ERR;
    }

    /* Writing here says "ops is looking after this tenant". Anything at all. */
    case DEV_SEG_HEARTBEAT:
        sim_serve(s, n->src);
        return IO_OK;

    case DEV_LOG: {
        char tmp[160];
        size_t l = len < sizeof tmp - 1 ? len : sizeof tmp - 1;
        memcpy(tmp, data, l); tmp[l] = 0;
        while (l && (tmp[l-1]=='\n' || tmp[l-1]=='\r')) tmp[--l] = 0;
        sim_log(s, "%s", tmp);
        return IO_OK;
    }
    }
    snprintf(s->fs.err, sizeof s->fs.err, "%s: read-only", n->name);
    return IO_ERR;
}

/* ------------------------------------------------------------- device tree */
/* A field node stores its source device id in `src` and its slot number in
 * `last_read_tick`, which is otherwise unused on a device node. */
static void mkfield(Sim *s, const char *dir, const char *field, int srcid, int slotno)
{
    char path[NOM_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s", dir, field);
    VNode *n = vfs_lookup(&s->fs, path);
    if (n && n->kind == VN_DEV) {
        n->read = dev_field_read; n->src = srcid; n->last_read_tick = (uint64_t)slotno;
        return;
    }
    n = vfs_mkdev(&s->fs, path, dev_field_read, NULL, DEV_FIELD);
    if (n) { n->src = srcid; n->last_read_tick = (uint64_t)slotno; }
}

static const char *SLOT_FIELDS[] = { "state","health","enabled","duty","draw",
                                     "supplied","effect","firmware","firmware_latest",
                                     "kind","part","spec", NULL };
static const char *CPU_FIELDS[]  = { "pool","rated","per_script","spare","executed",
                                     "bay_temp","throttled", NULL };
static const char *SEN_FIELDS[]  = { "online","warmup","calibrated","temp","fault","drift", NULL };

/* /srv/<seg>/ — one directory per tenant. This is the surface your daemons
 * actually talk to, and it is the reason the OS is the game. */
void sim_rebuild_srv(Sim *s)
{
    vfs_remove(&s->fs, "/srv");
    vfs_mkdir(&s->fs, "/srv");
    for (int i = 0; i < s->nsegs; i++) {
        if (!s->seg[i].docked) continue;
        char p[NOM_PATH_MAX];
        snprintf(p, sizeof p, "/srv/%s/heartbeat", s->seg[i].name);
        VNode *n = vfs_mkdev(&s->fs, p, NULL, dev_write, DEV_SEG_HEARTBEAT);
        if (n) n->src = i;
        snprintf(p, sizeof p, "/srv/%s/status", s->seg[i].name);
        n = vfs_mkdev(&s->fs, p, dev_read, NULL, DEV_SEG_STATUS);
        if (n) n->src = i;
        char dir[NOM_PATH_MAX];
        snprintf(dir, sizeof dir, "/srv/%s", s->seg[i].name);
        for (const char **f = (const char *[]){ "service","freshness","age",
                                                "power_need","data_need","cpu_need","pays",NULL }; *f; f++)
            mkfield(s, dir, *f, DEV_SEG_STATUS, i);
    }
}

void sim_rebuild_devtree(Sim *s)
{
    /* preserve whatever the player has arranged at /dev/scrubber */
    Buf keep; buf_init(&keep);
    bool had = false;
    VNode *old = vfs_lookup(&s->fs, "/dev/scrubber");
    char bindtarget[NOM_PATH_MAX]; bindtarget[0] = 0;
    if (old) {
        had = true;
        if (old->kind == VN_BIND) snprintf(bindtarget, sizeof bindtarget, "%s", old->target);
        else buf_put(&keep, old->data.p, old->data.len);
    }
    vfs_remove(&s->fs, "/dev");
    Vfs *fs = &s->fs;
    vfs_mkdir(fs, "/dev");

    vfs_mkdev(fs, "/dev/time",  dev_read, NULL, DEV_TIME);
    vfs_mkdev(fs, "/dev/log",   NULL, dev_write, DEV_LOG);
    vfs_mkdev(fs, "/dev/alarm", dev_read, NULL, DEV_ALARM);
    vfs_mkdev(fs, "/dev/msg",   dev_read, NULL, DEV_MSG);
    vfs_mkdev(fs, "/dev/bus/status", dev_read, NULL, DEV_BUS_STATUS);
    vfs_mkdev(fs, "/dev/bus/ctl",    NULL, dev_write, DEV_BUS_CTL);
    vfs_mkdev(fs, "/dev/hull/status", dev_read, NULL, DEV_HULL_STATUS);
    vfs_mkdev(fs, "/dev/mission/status", dev_read, NULL, DEV_MISSION_STATUS);
    vfs_mkdev(fs, "/dev/life/status", dev_read, NULL, DEV_LIFE_STATUS);
    for (const char **f = (const char *[]){ "o2","rate","consume","ticks_left",NULL }; *f; f++)
        mkfield(s, "/dev/life", *f, DEV_LIFE_STATUS, 0);

    for (const char **f = (const char *[]){ "supply","battery","brownout","demand","reactor",NULL }; *f; f++)
        mkfield(s, "/dev/bus", *f, DEV_BUS_STATUS, 0);
    mkfield(s, "/dev/hull", "integrity", DEV_HULL_STATUS, 0);
    for (const char **f = (const char *[]){ "segments","credits","income","power_bill","wear",NULL }; *f; f++)
        mkfield(s, "/dev/mission", *f, DEV_MISSION_STATUS, 0);

    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (!p) continue;
        Slot *sl = &s->slot[i];
        char dir[NOM_PATH_MAX], path[NOM_PATH_MAX];
        snprintf(dir, sizeof dir, "/dev/%s", sl->dev);

        snprintf(path, sizeof path, "%s/status", dir);
        VNode *n = vfs_mkdev(fs, path, dev_read, NULL, DEV_SLOT_STATUS);
        if (n) n->src = i;
        snprintf(path, sizeof path, "%s/ctl", dir);
        n = vfs_mkdev(fs, path, NULL, dev_write, DEV_SLOT_CTL);
        if (n) n->src = i;
        for (const char **f = SLOT_FIELDS; *f; f++) mkfield(s, dir, *f, DEV_SLOT_STATUS, i);

        snprintf(path, sizeof path, "/dev/bus/%s", sl->dev);
        n = vfs_mkdev(fs, path, dev_field_read, dev_write, DEV_BUS_CHANNEL);
        if (n) { n->src = DEV_BUS_STATUS; n->last_read_tick = 0; }

        switch (p->kind) {
        case K_REACTOR:
            snprintf(path, sizeof path, "%s/ctl", dir);
            n = vfs_lookup(fs, path);
            if (n) { n->id = DEV_REACTOR_CTL; n->src = i; }
            vfs_mkdev(fs, "/dev/reactor/ctl",    NULL, dev_write, DEV_REACTOR_CTL);
            vfs_mkdev(fs, "/dev/reactor/status", dev_read, NULL, DEV_REACTOR_STATUS);
            for (const char **f = (const char *[]){ "state","output","rated","ready","timer",NULL }; *f; f++)
                mkfield(s, "/dev/reactor", *f, DEV_REACTOR_STATUS, 0);
            break;
        case K_CPU:
            vfs_mkdev(fs, "/dev/cpu/status", dev_read, NULL, DEV_CPU_STATUS);
            for (const char **f = CPU_FIELDS; *f; f++) mkfield(s, "/dev/cpu", *f, DEV_CPU_STATUS, 0);
    mkfield(s, "/dev/cpu", "spare", DEV_CPU_STATUS, 0);
            break;
        case K_SENSOR:
            vfs_mkdev(fs, "/dev/sensor/status",   dev_read, NULL, DEV_SENSOR_STATUS);
            vfs_mkdev(fs, "/dev/sensor/contacts", dev_read, NULL, DEV_SENSOR_CONTACTS);
            vfs_mkdev(fs, "/dev/sensor/ctl",      NULL, dev_write, DEV_SENSOR_CTL);
            for (const char **f = SEN_FIELDS; *f; f++) mkfield(s, "/dev/sensor", *f, DEV_SENSOR_STATUS, 0);
            mkfield(s, "/dev/sensor", "bearing", DEV_SENSOR_CONTACTS, 0);
            mkfield(s, "/dev/sensor", "range",   DEV_SENSOR_CONTACTS, 0);
            break;
        case K_RADIATOR:
            vfs_mkdev(fs, "/dev/radiator/ctl", NULL, dev_write, DEV_RADIATOR_CTL);
            break;
        default: break;
        }
    }

    if (had) {
        if (bindtarget[0]) vfs_bind(&s->fs, bindtarget, "/dev/scrubber");
        else vfs_mkfile(&s->fs, "/dev/scrubber", keep.p ? keep.p : "rate 0.0\n");
    }
    buf_free(&keep);
}

/* --------------------------------------------------------------- /sys, /etc */
static void write_slot_conf(Sim *s, int i)
{
    Slot *sl = &s->slot[i];
    if (sl->part < 0) return;
    char path[NOM_PATH_MAX];
    snprintf(path, sizeof path, "/etc/%s.conf", sl->dev);
    Buf b; buf_init(&b);
    buf_printf(&b, "# %s - read every tick. Edit it and it takes effect at once.\n", sl->dev);
    buf_printf(&b, "enabled %d\n", sl->enabled ? 1 : 0);
    buf_puts(&b, "duty "); buf_putnum(&b, sl->duty, 2); buf_putc(&b, '\n');
    vfs_mkfile(&s->fs, path, b.p ? b.p : "");
    buf_free(&b);
}

/* Config is authoritative: the tick reads /etc, not the other way round, so
 * editing a file in a terminal is a real administrative action. */
static void read_slot_conf(Sim *s, int i)
{
    Slot *sl = &s->slot[i];
    if (sl->part < 0) return;
    char path[NOM_PATH_MAX];
    snprintf(path, sizeof path, "/etc/%s.conf", sl->dev);
    VNode *n = vfs_lookup(&s->fs, path);
    if (!n || n->kind != VN_FILE) return;
    const char *p = n->data.p ? n->data.p : "";
    while (*p) {
        const char *e = p;
        while (*e && *e != '\n') e++;
        if (*p != '#') {
            char line[128];
            size_t l = (size_t)(e - p); if (l > 127) l = 127;
            memcpy(line, p, l); line[l] = 0;
            char w[4][40];
            int nw = word_split(line, l, w);
            if (nw >= 2 && strcmp(w[0], "enabled") == 0) sl->enabled = (w[1][0] == '1');
            if (nw >= 2 && strcmp(w[0], "duty") == 0) {
                double d; if (parse_double(w[1], &d)) sl->duty = clampd(d, 0, 1);
            }
        }
        p = *e ? e + 1 : e;
    }
}

static void rebuild_sys(Sim *s)
{
    vfs_remove(&s->fs, "/sys");
    vfs_mkdir(&s->fs, "/sys/slot");
    for (int i = 0; i < SLOT_COUNT; i++) {
        char path[NOM_PATH_MAX];
        snprintf(path, sizeof path, "/sys/slot/%d", i);
        VNode *n = vfs_mkdev(&s->fs, path, dev_read, dev_write, DEV_SLOT_STATUS);
        if (n) n->src = i;
    }
}

static void write_catalog(Sim *s)
{
    int n; const PartSpec *c = catalog(&n);
    Buf b; buf_init(&b);
    buf_puts(&b, "# Parts catalog.  buy <id> <slot>   in a terminal.\n");
    buf_puts(&b, "# id             price   draw    spec  kind      name\n");
    for (int i = 0; i < n; i++) {
        buf_printf(&b, "%-14s %6d  ", c[i].id, c[i].price);
        buf_putnum(&b, c[i].draw, 1); buf_puts(&b, "  ");
        buf_putnum(&b, c[i].spec, 1);
        buf_printf(&b, "  %-9s %s\n", part_kind_name(c[i].kind), c[i].name);
        buf_printf(&b, "%-14s         %s\n", "", c[i].desc);
    }
    vfs_mkfile(&s->fs, "/mnt/catalog/parts", b.p ? b.p : "");
    buf_free(&b);
}

/* -------------------------------------------------------------- install */
/* Ratings are a property of the installed hardware, not of the last tick, so
 * they must be right the moment a card goes in. */
static void recompute_ratings(Sim *s)
{
    s->cpu.rated = 0;
    s->reactor.rated = 0.0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (!p) continue;
        if (p->kind == K_CPU)     s->cpu.rated += (int)p->spec;
        if (p->kind == K_REACTOR) s->reactor.rated += p->spec;
    }
}

static void assign_devname(Sim *s, int slotno)
{
    const PartSpec *p = slot_spec(s, slotno);
    if (!p) { s->slot[slotno].dev[0] = 0; return; }
    static const char *PREFIX[K_KIND_COUNT] = { "", "reactor", "batt", "cpu", "rad", "sen", "thr", "scrub", "rail", "data" };
    const char *pre = PREFIX[p->kind];
    for (int idx = 0; idx < SLOT_COUNT; idx++) {
        char cand[PART_ID_MAX];
        snprintf(cand, sizeof cand, "%s%d", pre, idx);
        bool taken = false;
        for (int j = 0; j < SLOT_COUNT; j++)
            if (j != slotno && s->slot[j].part >= 0 && strcmp(s->slot[j].dev, cand) == 0) taken = true;
        if (!taken) { snprintf(s->slot[slotno].dev, PART_ID_MAX, "%s", cand); return; }
    }
}

bool sim_install(Sim *s, int slotno, const char *part_id, char *err, size_t errsz)
{
    if (slotno < 0 || slotno >= SLOT_COUNT) { snprintf(err, errsz, "no slot %d (0..%d)", slotno, SLOT_COUNT-1); return false; }
    int ci = catalog_find(part_id);
    if (ci < 0) { snprintf(err, errsz, "no part '%s' in the catalog", part_id); return false; }
    if (s->slot[slotno].part >= 0) { snprintf(err, errsz, "slot %d already holds %s", slotno, s->slot[slotno].dev); return false; }

    int nc; const PartSpec *c = catalog(&nc);
    Slot *sl = &s->slot[slotno];
    memset(sl, 0, sizeof *sl);
    sl->part = ci; sl->state = SLOT_OK; sl->health = 1.0;
    sl->enabled = true; sl->duty = 1.0;
    sl->firmware = c[ci].firmware;
    sl->link[0] = sl->link[1] = -1;
    /* where the previous owner left it: switches on the wall, cards in a rack */
    if (c[ci].kind == K_PWRBUS || c[ci].kind == K_DATABUS) {
        int n = 0;
        for (int j = 0; j < SLOT_COUNT; j++) {
            const PartSpec *q = slot_spec(s, j);
            if (q && j != slotno && (q->kind == K_PWRBUS || q->kind == K_DATABUS)) n++;
        }
        sl->x = 58; sl->y = 150 + n * 132;
    } else {
        int n = 0;
        for (int j = 0; j < SLOT_COUNT; j++) {
            const PartSpec *q = slot_spec(s, j);
            if (q && j != slotno && q->kind != K_PWRBUS && q->kind != K_DATABUS) n++;
        }
        sl->x = 408; sl->y = 124 + n * 48;
    }
    sl->installed_tick = s->tick;
    assign_devname(s, slotno);

    sim_rebuild_devtree(s);
    rebuild_sys(s);
    recompute_ratings(s);
    write_slot_conf(s, slotno);
    sim_log(s, "installed %s in slot %d as %s", c[ci].id, slotno, sl->dev);
    return true;
}

/* The cable tool: it measures the run, replicates a patch cable of exactly that
 * length, and teleports it into place. You never route it by hand — you pay
 * for it. That is what makes WHERE you rack something a real decision instead
 * of decoration, without making cable routing a chore. */
#define CABLE_PER_M   14.0     /* credits per metre, replicator energy included */
#define CABLE_MIN_M    1.5     /* even a card next to the plate needs a patch  */

double sim_cable_rate(void)      { return CABLE_PER_M; }
double sim_cable_spent(Sim *s)   { return s->cable_spent; }

/* Patch a device into a rail or spine. Ports are finite, which is what makes
 * "which one do I use" a decision rather than a formality. */
bool sim_connect(Sim *s, const char *dev, const char *linkdev, double metres, char *err, size_t errsz)
{
    int d = sim_slot_of_dev(s, dev);
    int l = sim_slot_of_dev(s, linkdev);
    if (d < 0) { snprintf(err, errsz, "no device '%s'", dev); return false; }
    if (l < 0) { snprintf(err, errsz, "no link '%s'", linkdev); return false; }
    const PartSpec *dp = slot_spec(s, d), *lp = slot_spec(s, l);
    if (lp->kind != K_PWRBUS && lp->kind != K_DATABUS) {
        snprintf(err, errsz, "%s is a %s, not a rail or a spine", linkdev, part_kind_name(lp->kind));
        return false;
    }
    int need = (lp->kind == K_PWRBUS) ? 0 : 1;
    uint8_t bit = (lp->kind == K_PWRBUS) ? NEEDS_PWR : NEEDS_DATA;
    if (!(dp->needs & bit)) {
        snprintf(err, errsz, "%s does not need %s", dev, lp->kind == K_PWRBUS ? "power" : "data");
        return false;
    }
    int used = 0;
    for (int i = 0; i < SLOT_COUNT; i++)
        if (i != d && s->slot[i].part >= 0 && s->slot[i].link[need] == l) used++;
    if (used >= (int)lp->spec2) {
        snprintf(err, errsz, "%s has no free ports (%d of %d used)", linkdev, used, (int)lp->spec2);
        return false;
    }
    if (metres < CABLE_MIN_M) metres = CABLE_MIN_M;
    double cost = metres * CABLE_PER_M;
    if (cost > s->credits) {
        snprintf(err, errsz, "a %.1f m patch cable costs %.0f cr and you have %.0f",
                 metres, cost, s->credits);
        return false;
    }
    s->credits -= cost;
    s->cable_spent += cost;
    s->slot[d].link[need] = l;
    s->slot[d].cable_m[need] = metres;
    sim_log(s, "cable tool: measured %.1f m, printed and fitted %s -> %s (-%.0f cr)",
            metres, dev, linkdev, cost);
    return true;
}

bool sim_disconnect(Sim *s, const char *dev, const char *linkdev, char *err, size_t errsz)
{
    int d = sim_slot_of_dev(s, dev);
    int l = sim_slot_of_dev(s, linkdev);
    if (d < 0 || l < 0) { snprintf(err, errsz, "no such device or link"); return false; }
    const PartSpec *lp = slot_spec(s, l);
    int need = (lp->kind == K_PWRBUS) ? 0 : 1;
    if (s->slot[d].link[need] != l) { snprintf(err, errsz, "%s is not on %s", dev, linkdev); return false; }
    /* the cable comes back to the spool, less what the tool wastes reclaiming it */
    double back = s->slot[d].cable_m[need] * CABLE_PER_M * 0.5;
    s->credits += back;
    s->slot[d].link[need] = -1;
    s->slot[d].cable_m[need] = 0.0;
    sim_log(s, "unpatched %s from %s (+%.0f cr reclaimed)", dev, linkdev, back);
    return true;
}

/* trace — a traceroute for infrastructure. A topology you cannot see is just
 * unfair, so this is the diagnostic that makes the whole mechanic playable. */
void sim_trace(Sim *s, const char *dev, Buf *out)
{
    int d = sim_slot_of_dev(s, dev);
    if (d < 0) { buf_printf(out, "%s: no such device\n", dev); return; }
    const PartSpec *dp = slot_spec(s, d);
    buf_printf(out, "%s  (%s)\n", dev, dp->name);
    buf_printf(out, "  state %s, health ", slot_state_name(s->slot[d].state));
    buf_putnum(out, s->slot[d].health * 100, 0);
    buf_puts(out, "%, effect ");
    buf_putnum(out, slot_effect(s, d) * 100, 0);
    buf_puts(out, "%\n");

    for (int k = 0; k < 2; k++) {
        uint8_t bit = k == 0 ? NEEDS_PWR : NEEDS_DATA;
        if (!(dp->needs & bit)) continue;
        PartKind lk = k == 0 ? K_PWRBUS : K_DATABUS;
        const char *what = k == 0 ? "power" : "data";
        int l = s->slot[d].link[k];
        if (l < 0) { buf_printf(out, "  <- %s: NOT PATCHED INTO ANYTHING\n", what); continue; }
        const PartSpec *lp = slot_spec(s, l);
        double cap = lp->spec * s->slot[l].health, load = link_load(s, l, lk);
        buf_printf(out, "  <- %s: %s  (%.1f m cable)  ", what, s->slot[l].dev,
                   s->slot[d].cable_m[k]);
        buf_putnum(out, cap, 2);
        buf_puts(out, " capacity, ");
        buf_putnum(out, load, 2);
        buf_puts(out, " demanded");
        if (load > cap) buf_puts(out, "   ** SATURATED **");
        buf_putc(out, '\n');
        buf_puts(out, "       also on it:");
        for (int i = 0; i < SLOT_COUNT; i++) {
            if (i == d || s->slot[i].part < 0 || s->slot[i].link[k] != l) continue;
            const PartSpec *op = slot_spec(s, i);
            buf_printf(out, " %s(", s->slot[i].dev);
            buf_putnum(out, k == 0 ? op->draw : op->data, 2);
            buf_puts(out, ")");
        }
        for (int i = 0; i < s->nsegs; i++) {
            if (!s->seg[i].docked) continue;
            if ((k == 0 ? s->seg[i].rail : s->seg[i].spine) != l) continue;
            buf_printf(out, " %s(", s->seg[i].name);
            buf_putnum(out, k == 0 ? s->seg[i].power_need : s->seg[i].data_need, 2);
            buf_puts(out, ")");
        }
        buf_putc(out, '\n');
    }
    if (dp->needs & NEEDS_PWR) {
        buf_puts(out, "  <- reactor: ");
        buf_putnum(out, s->reactor.output, 2);
        buf_puts(out, " of ");
        buf_putnum(out, s->reactor.rated, 2);
        buf_puts(out, " MW\n");
    }
}

bool sim_remove(Sim *s, int slotno, char *err, size_t errsz)
{
    if (slotno < 0 || slotno >= SLOT_COUNT) { snprintf(err, errsz, "no slot %d", slotno); return false; }
    if (s->slot[slotno].part < 0) { snprintf(err, errsz, "slot %d is empty", slotno); return false; }
    char dev[PART_ID_MAX], cpath[NOM_PATH_MAX];
    snprintf(dev, sizeof dev, "%s", s->slot[slotno].dev);
    snprintf(cpath, sizeof cpath, "/etc/%s.conf", dev);
    vfs_remove(&s->fs, cpath);
    memset(&s->slot[slotno], 0, sizeof s->slot[slotno]);
    s->slot[slotno].part = -1;
    sim_rebuild_devtree(s);
    rebuild_sys(s);
    recompute_ratings(s);
    sim_log(s, "removed %s from slot %d", dev, slotno);
    return true;
}

/* -------------------------------------------------------------- lifecycle */
void sim_reset(Sim *s, uint64_t seed)
{
    for (int i = 0; i < s->nscripts; i++) {
        if (s->script[i].vm) vm_free(s->script[i].vm);
        if (s->script[i].prog) prog_free(s->script[i].prog);
        s->script[i].vm = NULL; s->script[i].prog = NULL;
        s->script[i].alive = false; s->script[i].launched = false; s->script[i].err[0] = 0;
    }
    s->tick = 0; s->seed = seed;
    rng_seed(&s->rng, seed);
    s->run = RUN_SETUP; s->outcome[0] = 0; s->nevents = 0;
    s->digest = 1469598103934665603ULL;
    buf_clear(&s->replay);

    memset(&s->reactor, 0, sizeof s->reactor);
    memset(&s->cpu, 0, sizeof s->cpu);
    s->cpu.pool = BMC_POOL;
    s->cpu.spare = BMC_POOL;
    memset(&s->sensor, 0, sizeof s->sensor);
    s->cpu.bay_temp = BAY_START;
    s->sensor.warmup = 12;
    s->bus.brownout = false; s->bus.charge = 0.0;
    s->hull_integrity = 100.0;
    s->wear = 0.0;
    s->o2 = O2_START; s->o2_rate = 0.0;
    s->credits = 250.0;
    s->cable_spent = 0.0;
    s->fuel_rate = 0.22;          /* credits per MW-tick */
    s->nsegs = 0;
    snprintf(s->here, sizeof s->here, "core");
    for (int i = 0; i < ORDER_MAX; i++) s->order[i].active = false;
    s->nmsgs = 0;
    s->next_dock_tick = (uint64_t)rng_range(&s->rng, 600, 900);
    s->alarm_head = s->alarm_tail = 0;
    for (int i = 0; i < SYM_COUNT; i++) { s->symptom[i] = false; s->symptom_since[i] = 0; }
    s->next_fault_tick = (uint64_t)rng_range(&s->rng, FAULT_MIN_GAP, FAULT_MAX_GAP);

    for (int i = 0; i < SLOT_COUNT; i++) {
        s->slot[i].part = -1; s->slot[i].request = 0; s->slot[i].supplied = 0;
    }
    vfs_remove(&s->fs, "/etc");
    vfs_mkdir(&s->fs, "/etc");

    /* the salvage loadout: everything free, everything mediocre */
    char err[NOM_ERR_MAX];
    sim_install(s, 0, "reactor-a1", err, sizeof err);
    sim_install(s, 1, "batt-s",     err, sizeof err);
    sim_install(s, 6, "pwr-a",      err, sizeof err);   /* rail0 */
    sim_install(s, 7, "data-a",     err, sizeof err);   /* data0 */
    sim_install(s, 2, "cpu-mk1",    err, sizeof err);
    sim_install(s, 3, "rad-a",      err, sizeof err);
    sim_install(s, 4, "sen-a",      err, sizeof err);
    sim_install(s, 5, "scrub-a",    err, sizeof err);
    /* patched as the previous owner left it: everything on the one rail */
    sim_connect(s, "cpu0", "rail0", 0.0, err, sizeof err);
    sim_connect(s, "cpu0", "data0", 0.0, err, sizeof err);
    sim_connect(s, "rad0", "rail0", 0.0, err, sizeof err);
    sim_connect(s, "sen0", "rail0", 0.0, err, sizeof err);
    sim_connect(s, "sen0", "data0", 0.0, err, sizeof err);
    sim_connect(s, "scrub0", "rail0", 0.0, err, sizeof err);
    /* the previous owner's cabling is already installed and paid for */
    s->credits = 600.0;      /* enough runway to fumble the first shift */
    s->cable_spent = 0.0;
    s->nudged_reactor = false;
    s->bus.charge = 12.0;

    int si = first_of_kind(s, K_SENSOR);
    const PartSpec *sp = si >= 0 ? slot_spec(s, si) : NULL;
    s->sensor.bias_full = (rng_unit(&s->rng) * 2.0 - 1.0) * (sp ? sp->spec : 30.0);


    /* Your own atmosphere plant is dead. That is the situation. */
    vfs_mkfile(&s->fs, "/dev/scrubber", "rate 0.0\n");
    vfs_remove(&s->fs, "/n");
    vfs_mkdir(&s->fs, "/n");
    wreck_reset();

    /* The machines you know about, and where they get mounted. Both files are
     * yours to edit; the file manager reads fstab to decide what to show. */
    vfs_mkfile(&s->fs, "/etc/hosts",
        "# name        description\n"
        "station       this machine\n"
        "wreck-01      kel-morrin survey tender, adrift, in umbilical range\n");
    vfs_mkfile(&s->fs, "/etc/fstab",
        "# host        mountpoint      options\n"
        "# `mount-all` brings these up. The file manager shows whatever is here.\n"
        "wreck-01      /n/wreck-01     ro\n");

    vfs_mkdir(&s->fs, "/mnt/replicator");
    {
        char e2[NOM_ERR_MAX];
        sim_dock_segment(s, "habitat", e2, sizeof e2);
        sim_dock_segment(s, "lab", e2, sizeof e2);
        sim_seg_patch(s, "hab-1", "rail0", e2, sizeof e2);
        sim_seg_patch(s, "hab-1", "data0", e2, sizeof e2);
        sim_seg_patch(s, "lab-1", "rail0", e2, sizeof e2);
        sim_seg_patch(s, "lab-1", "data0", e2, sizeof e2);
    }

    s->nevents = 0;
    vfs_mkfile(&s->fs, "/var/log/messages", "");
    write_catalog(s);
}

Sim *sim_new(uint64_t seed)
{
    Sim *s = nom_alloc(sizeof(Sim));
    vfs_init(&s->fs);
    s->fs.ctx = s;
    buf_init(&s->replay);
    s->budget_max = BUDGET_MAX_DEFAULT;
    s->max_ticks = 6000;
    s->recording = true;
    for (int i = 0; i < SLOT_COUNT; i++) s->slot[i].part = -1;
    vfs_mkdir(&s->fs, "/etc");
    vfs_mkdir(&s->fs, "/var/log");
    vfs_mkdir(&s->fs, "/mnt/catalog");
    vfs_mkdir(&s->fs, "/mnt/replicator");
    vfs_mkdir(&s->fs, "/proc");
    vfs_mkdir(&s->fs, "/home/scripts");
    sim_reset(s, seed);
    return s;
}

void sim_free(Sim *s)
{
    if (!s) return;
    for (int i = 0; i < s->nscripts; i++) {
        if (s->script[i].vm) vm_free(s->script[i].vm);
        if (s->script[i].prog) prog_free(s->script[i].prog);
    }
    buf_free(&s->replay);
    vfs_free(&s->fs);
    nom_free(s);
}

/* ----------------------------------------------------------------- scripts */
void sim_detach_all(Sim *s)
{
    for (int i = 0; i < s->nscripts; i++) {
        if (s->script[i].vm) vm_free(s->script[i].vm);
        if (s->script[i].prog) prog_free(s->script[i].prog);
    }
    s->nscripts = 0;
    vfs_remove(&s->fs, "/proc");
    vfs_mkdir(&s->fs, "/proc");
}

bool sim_attach(Sim *s, const char *vfspath, char *err, size_t errsz)
{
    if (s->nscripts >= SIM_MAX_SCRIPTS) { snprintf(err, errsz, "too many scripts (max %d)", SIM_MAX_SCRIPTS); return false; }
    VNode *n = vfs_lookup(&s->fs, vfspath);
    if (!n || n->kind != VN_FILE) { snprintf(err, errsz, "%s: no such script", vfspath); return false; }
    for (int i = 0; i < s->nscripts; i++)
        if (strcmp(s->script[i].path, vfspath) == 0) { snprintf(err, errsz, "%s already attached", vfspath); return false; }
    Script *sc = &s->script[s->nscripts];
    memset(sc, 0, sizeof *sc);
    snprintf(sc->path, sizeof sc->path, "%s", vfspath);
    const char *base = strrchr(vfspath, '/');
    snprintf(sc->name, sizeof sc->name, "%s", base ? base + 1 : vfspath);
    s->nscripts++;
    return true;
}

bool sim_launch(Sim *s, char *err, size_t errsz)
{
    if (s->nscripts == 0) { snprintf(err, errsz, "nothing to launch: attach a script first"); return false; }
    for (int i = 0; i < s->nscripts; i++) {
        Script *sc = &s->script[i];
        if (sc->vm) { vm_free(sc->vm); sc->vm = NULL; }
        if (sc->prog) { prog_free(sc->prog); sc->prog = NULL; }
        sc->err[0] = 0;
        VNode *n = vfs_lookup(&s->fs, sc->path);
        if (!n || n->kind != VN_FILE) { snprintf(err, errsz, "%s: no such script", sc->path); return false; }
        char cerr[NOM_ERR_MAX];
        sc->prog = prog_compile(n->data.p ? n->data.p : "", sc->name, cerr, sizeof cerr);
        if (!sc->prog) {
            snprintf(err, errsz, "%s: %s", sc->name, cerr);
            snprintf(sc->err, sizeof sc->err, "%s", cerr);
            return false;
        }
        sc->vm = vm_new(sc->prog, &s->fs, s);
        sc->alive = true; sc->launched = true; sc->status = VM_YIELD;
    }
    s->run = RUN_ACTIVE;
    /* the maintenance controller is on before anything else is, including on
     * the very first tick — otherwise nothing can bootstrap */
    s->cpu.pool = BMC_POOL;
    s->cpu.spare = BMC_POOL;
    s->cpu.per_script = BMC_POOL;
    sim_log(s, "launch: seed %llu, %d script(s)", (unsigned long long)s->seed, s->nscripts);
    return true;
}

/* --------------------------------------------------------------- job control */
static bool compile_into(Sim *s, Script *sc, char *err, size_t errsz)
{
    VNode *n = vfs_lookup(&s->fs, sc->path);
    if (!n || n->kind != VN_FILE) { snprintf(err, errsz, "%s: no such script", sc->path); return false; }
    if (sc->vm)   { vm_free(sc->vm); sc->vm = NULL; }
    if (sc->prog) { prog_free(sc->prog); sc->prog = NULL; }
    char cerr[NOM_ERR_MAX];
    sc->prog = prog_compile(n->data.p ? n->data.p : "", sc->name, cerr, sizeof cerr);
    if (!sc->prog) {
        snprintf(err, errsz, "%s: %s", sc->name, cerr);
        snprintf(sc->err, sizeof sc->err, "%s", cerr);
        sc->alive = false;
        return false;
    }
    sc->err[0] = 0;
    sc->vm = vm_new(sc->prog, &s->fs, s);
    sc->alive = true;
    sc->launched = true;
    sc->status = VM_YIELD;
    return true;
}

bool sim_kill(Sim *s, int pid, char *err, size_t errsz)
{
    if (pid < 1 || pid > s->nscripts) { snprintf(err, errsz, "no pid %d", pid); return false; }
    Script *sc = &s->script[pid - 1];
    if (!sc->alive) { snprintf(err, errsz, "%s is not running", sc->name); return false; }
    sc->alive = false;
    sc->killed = true;
    sim_log(s, "%s (pid %d) killed", sc->name, pid);
    sim_publish_proc(s);          /* so `ps` tells the truth immediately */
    return true;
}

/* Recompiles from disk, so this is also how you deploy an edit. */
bool sim_restart(Sim *s, int pid, char *err, size_t errsz)
{
    if (pid < 1 || pid > s->nscripts) { snprintf(err, errsz, "no pid %d", pid); return false; }
    Script *sc = &s->script[pid - 1];
    sc->killed = false;
    if (!compile_into(s, sc, err, errsz)) { sim_publish_proc(s); return false; }
    sim_log(s, "%s (pid %d) restarted", sc->name, pid);
    sim_publish_proc(s);
    return true;
}

/* Attach and start in one go, without stopping anything else. */
bool sim_start_script(Sim *s, const char *path, char *err, size_t errsz)
{
    if (!sim_attach(s, path, err, errsz)) return false;
    Script *sc = &s->script[s->nscripts - 1];
    if (!compile_into(s, sc, err, errsz)) { s->nscripts--; return false; }
    sim_log(s, "%s started as pid %d", sc->name, s->nscripts);
    sim_publish_proc(s);
    return true;
}

/* ------------------------------------------------------------------ physics */
static void step_reactor(Sim *s)
{
    Powerplant *r = &s->reactor;
    r->rated = 0.0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (p && p->kind == K_REACTOR) r->rated += p->spec;
    }
    double avail = sum_effect_spec(s, K_REACTOR) * (1.0 - 0.40 * s->wear);

    switch (r->state) {
    case REACTOR_PRIMING:
        if (s->bus.charge < REACTOR_PRIME_DRAW) {
            r->state = REACTOR_COLD; r->timer = 0;
            sim_log(s, "reactor: prime aborted, battery flat");
            break;
        }
        s->bus.charge -= REACTOR_PRIME_DRAW;
        if (--r->timer <= 0) { r->state = REACTOR_IDLE; sim_log(s, "reactor: primed"); }
        break;
    case REACTOR_SPINUP: {
        int el = REACTOR_SPINUP_TICKS - r->timer + 1;
        r->output = avail * (double)el / (double)REACTOR_SPINUP_TICKS;
        if (--r->timer <= 0) { r->state = REACTOR_ONLINE; r->output = avail; sim_log(s, "reactor: online"); }
        break;
    }
    case REACTOR_ONLINE: r->output = avail; break;
    default: r->output = 0.0; break;
    }
}

/* Power is allocated in slot order, so where you physically install a card
 * decides who browns out first. That is a real decision at install time. */
static void step_power(Sim *s)
{
    s->bus.capacity = 0.0; s->bus.discharge_max = 0.0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (p && p->kind == K_BATTERY && slot_live(s, i)) {
            s->bus.capacity += p->spec * s->slot[i].health;
            s->bus.discharge_max += p->spec2;
        }
    }
    if (s->bus.charge > s->bus.capacity) s->bus.charge = s->bus.capacity;

    double total = 0.0, req[SLOT_COUNT];
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        req[i] = (p && slot_live(s, i)) ? clampd(s->slot[i].request, 0.0, p->draw) : 0.0;
        total += req[i];
    }

    double avail = s->reactor.output;
    if (total > avail) {
        double deficit = total - avail;
        double fromb = deficit;
        if (fromb > s->bus.discharge_max) fromb = s->bus.discharge_max;
        if (fromb > s->bus.charge) fromb = s->bus.charge;
        s->bus.charge -= fromb;
        avail += fromb;
    } else {
        double into = avail - total;
        if (into > 2.0) into = 2.0;
        double room = s->bus.capacity - s->bus.charge;
        if (into > room) into = room;
        s->bus.charge += into;
    }

    bool brown = false;
    double left = avail;
    for (int i = 0; i < SLOT_COUNT; i++) {
        double give = req[i] < left ? req[i] : left;
        if (give < req[i] - 1e-9) brown = true;
        s->slot[i].supplied = give;
        left -= give;
    }
    if (brown && !s->bus.brownout) sim_log(s, "bus: BROWNOUT, demand exceeds supply");
    s->bus.brownout = brown;
}

static void step_compute(Sim *s)
{
    Compute *c = &s->cpu;
    c->rated = 0;
    double pool = 0.0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (!p || p->kind != K_CPU) continue;
        c->rated += (int)p->spec;
        pool += p->spec * slot_effect(s, i);
    }
    double tf = 1.0;
    if (c->bay_temp > BAY_THROTTLE_TEMP)
        tf = clampd(1.0 - (c->bay_temp - BAY_THROTTLE_TEMP) / 40.0, 0.0, 1.0);
    c->throttled = tf < 0.999;
    pool *= tf;
    if (pool < BMC_POOL) pool = BMC_POOL;    /* the maintenance controller */
    c->pool = (int)pool;

    int alive = 0;
    for (int i = 0; i < s->nscripts; i++) if (s->script[i].alive) alive++;
    c->per_script = alive > 0 ? c->pool / alive : c->pool;
    if (c->per_script < 1) c->per_script = 1;
}

static void step_thermal(Sim *s)
{
    Compute *c = &s->cpu;
    double heat = 0.0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (!p) continue;
        heat += p->heat * slot_effect(s, i);
    }
    heat += (double)c->executed / 1000.0 * HEAT_PER_KINSTR;
    c->heat_in = heat;

    double reject = 0.0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (p && p->kind == K_RADIATOR) reject += p->spec * slot_effect(s, i);
    }
    /* A radiator sheds heat to a sink, so its throughput falls off as the bay
     * approaches ambient and is zero at it. Without this, radiators pumped a
     * fixed load out of an already-cold bay and drove it to hull freeze while
     * the station sat idle — cooling was a hazard rather than a fix. */
    reject *= clampd((c->bay_temp - BAY_AMBIENT) / (BAY_THROTTLE_TEMP - BAY_AMBIENT),
                     0.0, 1.25);
    c->heat_out = HEAT_LEAK * (c->bay_temp - BAY_AMBIENT) + reject;
    c->bay_temp += (c->heat_in - c->heat_out) * HEAT_RATE;

    if (c->bay_temp < HULL_FREEZE_TEMP) s->hull_integrity = clampd(s->hull_integrity - 0.2, 0, 100);
    if (c->bay_temp > BAY_THROTTLE_TEMP)
        s->wear = clampd(s->wear + 0.0006 * (c->bay_temp - BAY_THROTTLE_TEMP) / 40.0, 0, 1);
    else if (c->bay_temp < BAY_ANNEAL_TEMP)
        s->wear = clampd(s->wear - 0.00022, 0, 1);
}

static void step_sensor(Sim *s)
{
    Sensor *sn = &s->sensor;
    int si = first_of_kind(s, K_SENSOR);
    if (si < 0) { sn->online = false; sn->has_reading = false; return; }
    const PartSpec *p = slot_spec(s, si);
    double eff = slot_effect(s, si);
    if (eff < 0.85) {
        if (sn->online) sim_log(s, "%s: lost power, contacts will block", s->slot[si].dev);
        sn->online = false; sn->has_reading = false;
        if (sn->warmup <= 0) sn->warmup = (int)p->spec2;
        return;
    }
    if (!sn->online) {
        if (--sn->warmup > 0) return;
        sn->online = true;
        sim_log(s, "%s: online", s->slot[si].dev);
    }
    /* With nowhere to go, the bench is a station instrument: it reports how
     * far its own alignment has drifted, which is still temperature-driven. */
    double err_scale;
    if (strcmp(p->id, "sen-cryo") == 0)
        err_scale = clampd(s->cpu.bay_temp / 40.0, 0.0, 1.0);
    else
        err_scale = clampd((SENSOR_CLEAR_TEMP - s->cpu.bay_temp) / SENSOR_ERROR_SPAN, 0.0, 1.0);
    sn->last_bearing = nom_fabs(sn->bias_full * err_scale);
    sn->last_range = 0.0;
    sn->last_sample_tick = s->tick;
    sn->has_reading = true;
}

/* Hardware breaks. A card degrades first — it still works, badly, and a reseat
 * or a purge fixes it — and only fails outright if you leave it alone. */
static void step_hardware(Sim *s)
{
    if (s->tick < s->next_fault_tick) return;
    s->next_fault_tick = s->tick + (uint64_t)rng_range(&s->rng, FAULT_MIN_GAP, FAULT_MAX_GAP);

    int live[SLOT_COUNT], n = 0;
    for (int i = 0; i < SLOT_COUNT; i++)
        if (s->slot[i].part >= 0 && s->slot[i].state != SLOT_FAILED) live[n++] = i;
    if (!n) return;

    int pick = live[rng_range(&s->rng, 0, n - 1)];
    Slot *sl = &s->slot[pick];
    const PartSpec *p = slot_spec(s, pick);
    bool stale = sl->firmware < p->firmware;

    if (sl->state == SLOT_OK) {
        sl->state = SLOT_DEGRADED;
        sim_log(s, "HW: %s degraded (%s%s)", sl->dev, p->name, stale ? ", firmware is stale" : "");
    } else {
        sl->state = SLOT_FAILED;
        sim_log(s, "HW: %s FAILED", sl->dev);
    }
    sl->health = clampd(sl->health - 0.10, 0.05, 1.0);
}

/* Life support. The ship reads /dev/scrubber every tick and looks for a line
 * `rate <percent per tick>`. It does not care what is behind that path: the
 * ship's own plant, a plain file a shim writes, or an alien unit bound in from
 * a wreck. That indifference is the whole design. */
static void step_life(Sim *s)
{
    /* An installed scrubber card publishes to the same path a shim would.
     * The ship cannot tell the difference, which is the point. */
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (!p || p->kind != K_SCRUBBER) continue;
        /* A dead or unpowered card must not squat on the path: that is exactly
         * when a shim wants to take it over. */
        if (slot_effect(s, i) <= 0.0) continue;
        Buf sb; buf_init(&sb);
        buf_puts(&sb, "rate ");
        buf_putnum(&sb, p->spec * slot_effect(s, i), 4);
        buf_putc(&sb, '\n');
        VNode *n = vfs_lookup(&s->fs, "/dev/scrubber");
        if (n && n->kind == VN_FILE) { buf_clear(&n->data); buf_put(&n->data, sb.p, sb.len); }
        buf_free(&sb);
        break;
    }

    double rate = 0.0;
    Buf b; buf_init(&b);
    if (vfs_read(&s->fs, "/dev/scrubber", &b) == IO_OK) {
        const char *p = b.p ? b.p : "";
        while (*p) {
            const char *e = p; while (*e && *e != '\n') e++;
            if (!strncmp(p, "rate ", 5)) {
                Value v;
                if (nom_parse_number(p + 5, (size_t)(e - p) - 5, &v)) rate = val_num(v);
            }
            p = *e ? e + 1 : e;
        }
    }
    buf_free(&b);
    s->o2_rate = clampd(rate, 0.0, 0.20);
    s->o2 = clampd(s->o2 + s->o2_rate - O2_CONSUME, 0.0, 100.0);
}

static void step_symptoms(Sim *s)
{
    int si = first_of_kind(s, K_SENSOR);
    bool bad_bearing = false;
    if (si >= 0) {
        if (s->sensor.online && (s->cpu.bay_temp < SENSOR_CLEAR_TEMP || s->slot[si].state != SLOT_OK))
            bad_bearing = true;
        if (!s->sensor.online && s->slot[si].request > 0 && slot_effect(s, si) < 0.85)
            bad_bearing = true;
    }
    symptom_set(s, SYM_BEARING_UNSTABLE, bad_bearing);
    symptom_set(s, SYM_BAY_OVERHEATING, s->cpu.bay_temp > BAY_THROTTLE_TEMP);

    bool shortfall = false;
    for (int i = 0; i < SLOT_COUNT; i++)
        if (s->slot[i].supplied < s->slot[i].request - 0.01) shortfall = true;
    symptom_set(s, SYM_POWER_SHORTFALL, shortfall);

    bool hw = false;
    for (int i = 0; i < SLOT_COUNT; i++)
        if (s->slot[i].part >= 0 && s->slot[i].state != SLOT_OK) hw = true;
    symptom_set(s, SYM_HARDWARE_FAULT, hw);
    symptom_set(s, SYM_AIR_FALLING, s->o2_rate < O2_CONSUME);

    /* Your own scripts eating the pool is a fault like any other, and it must
     * be as diagnosable as a dead card. Name the worst offender. */
    int want = 0;
    for (int i = 0; i < s->nsegs; i++) if (s->seg[i].docked) want += s->seg[i].cpu_need;
    bool starved = (s->tick > 60 && want > 0 && s->cpu.spare < want / 2 && s->cpu.pool > 0);
    if (starved && !s->symptom[SYM_COMPUTE_STARVED]) {
        int worst = -1;
        uint64_t most = 0;
        for (int i = 0; i < s->nscripts; i++)
            if (s->script[i].alive && s->script[i].steps_last > most) {
                most = s->script[i].steps_last;
                worst = i;
            }
        if (worst >= 0)
            sim_message(s, "ops-advisory",
                        "the tenants are short of compute and %s (pid %d) is using %llu "
                        "instructions a tick. `ps`, then give it a sleep().",
                        s->script[worst].name, worst + 1, (unsigned long long)most);
    }
    symptom_set(s, SYM_COMPUTE_STARVED, starved);
}

static const char *script_state(const Script *sc);

/* ------------------------------------------------------------------- /proc */
void sim_publish_proc(Sim *s)
{
    for (int i = 0; i < s->nscripts; i++) {
        Script *sc = &s->script[i];
        char path[NOM_PATH_MAX];
        snprintf(path, sizeof path, "/proc/%d/status", i + 1);
        Buf b; buf_init(&b);
        const char *st = script_state(sc);
        buf_printf(&b, "pid %d\n", i + 1);
        buf_printf(&b, "name %s\n", sc->name);
        buf_printf(&b, "path %s\n", sc->path);
        buf_printf(&b, "state %s\n", st);
        buf_printf(&b, "steps %llu\n", sc->vm ? (unsigned long long)vm_steps(sc->vm) : 0ULL);
        buf_printf(&b, "steps_last %llu\n", (unsigned long long)sc->steps_last);
        buf_printf(&b, "budget %d\n", s->cpu.per_script);
        if (sc->vm) buf_printf(&b, "line %d\n", vm_line(sc->vm));
        if (sc->vm && sc->status == VM_BLOCKED) buf_printf(&b, "wchan %s\n", vm_blocked_on(sc->vm));
        if (sc->err[0]) buf_printf(&b, "error %s\n", sc->err);
        vfs_mkfile(&s->fs, path, b.p ? b.p : "");
        buf_free(&b);
    }
}

/* -------------------------------------------------------------------- tick */
static void hash_str(uint64_t *h, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) { *h ^= (unsigned char)s[i]; *h *= 1099511628211ULL; }
}

static void record_frame(Sim *s)
{
    Buf f; buf_init(&f);
    buf_printf(&f, "t %llu", (unsigned long long)s->tick);
    buf_puts(&f, " bay "); buf_putnum(&f, s->cpu.bay_temp, 4);
    buf_printf(&f, " pool %d", s->cpu.pool);
    buf_printf(&f, " exec %llu", (unsigned long long)s->cpu.executed);
    buf_puts(&f, " batt "); buf_putnum(&f, s->bus.charge, 4);
    buf_puts(&f, " out ");  buf_putnum(&f, s->reactor.output, 4);
    buf_puts(&f, " o2 ");   buf_putnum(&f, s->o2, 4);
    buf_puts(&f, " cr ");   buf_putnum(&f, s->credits, 3);
    buf_puts(&f, " inc ");  buf_putnum(&f, s->income_last, 3);
    buf_puts(&f, " wear "); buf_putnum(&f, s->wear, 4);
    buf_printf(&f, " rs %d sen %d seg %d", (int)s->reactor.state, s->sensor.online ? 1 : 0, s->nsegs);
    for (int i = 0; i < SLOT_COUNT; i++) buf_printf(&f, " s%d %d", i, (int)s->slot[i].state);
    buf_putc(&f, '\n');
    hash_str(&s->digest, f.p, f.len);
    if (s->recording) buf_put(&s->replay, f.p, f.len);
    buf_free(&f);
}

uint64_t sim_state_digest(Sim *s) { return s->digest; }

void sim_tick(Sim *s)
{
    if (s->run != RUN_ACTIVE) return;

    for (int i = 0; i < SLOT_COUNT; i++) read_slot_conf(s, i);

    uint64_t before = 0, after = 0;
    for (int i = 0; i < s->nscripts; i++) if (s->script[i].vm) before += vm_steps(s->script[i].vm);

    int pool_left = s->cpu.pool;
    for (int i = 0; i < s->nscripts; i++) {
        Script *sc = &s->script[i];
        if (!sc->alive || !sc->vm) continue;
        if (sc->status == VM_SLEEP && s->tick < vm_wake_tick(sc->vm)) continue;
        uint64_t b0 = vm_steps(sc->vm);
        /* it may take as much of what is left as it wants */
        int give = pool_left > 0 ? pool_left : 1;
        VmStatus st = vm_run(sc->vm, give);
        pool_left -= (int)(vm_steps(sc->vm) - b0);
        if (pool_left < 0) pool_left = 0;
        sc->steps_last = vm_steps(sc->vm) - b0;
        sc->status = st;
        if (st == VM_ERROR) {
            snprintf(sc->err, sizeof sc->err, "%s", vm_err(sc->vm));
            sim_log(s, "%s: runtime error [line %d]: %s", sc->name, vm_line(sc->vm), sc->err);
            sc->alive = false;
        } else if (st == VM_OK) {
            sim_log(s, "%s: exited at tick %llu", sc->name, (unsigned long long)s->tick);
            sc->alive = false;
        }
    }
    for (int i = 0; i < s->nscripts; i++) if (s->script[i].vm) after += vm_steps(s->script[i].vm);
    s->cpu.executed = after - before;
    s->cpu.spare = pool_left;      /* tenants are served out of what is left */

    /* Segments arrive on contract. You do not choose when. */
    if (s->tick >= s->next_dock_tick && s->nsegs < SEG_MAX) {
        static const char *KINDS[] = { "habitat", "lab", "foundry", "dock" };
        char e[NOM_ERR_MAX];
        sim_dock_segment(s, KINDS[rng_range(&s->rng, 0, 3)], e, sizeof e);
        s->next_dock_tick = s->tick + (uint64_t)rng_range(&s->rng, 700, 1200);
    }

    step_hardware(s);
    step_reactor(s);
    step_power(s);
    step_thermal(s);
    step_compute(s);
    step_sensor(s);
    step_life(s);
    sim_step_orders(s);
    sim_step_segments(s);
    sim_step_bill(s);
    step_symptoms(s);
    sim_publish_proc(s);

    s->tick++;

    if (s->credits <= 0.0 && s->income_last <= 0.0 && s->tick > 200) {
        s->run = RUN_LOST;
        snprintf(s->outcome, sizeof s->outcome,
                 "broke at tick %llu with nothing earning — nobody was running ops",
                 (unsigned long long)s->tick);
        sim_log(s, "%s", s->outcome);
    } else if (s->o2 <= 0.0) {
        s->run = RUN_LOST;
        snprintf(s->outcome, sizeof s->outcome,
                 "you asphyxiated at tick %llu", (unsigned long long)s->tick);
        sim_log(s, "%s", s->outcome);
    } else if (s->cpu.bay_temp >= BAY_FAIL_TEMP) {
        s->run = RUN_LOST;
        snprintf(s->outcome, sizeof s->outcome, "flight computer cooked at tick %llu with %d segment(s) aboard",
                 (unsigned long long)s->tick, s->nsegs);
        sim_log(s, "%s", s->outcome);
    } else if (s->hull_integrity <= 0.0) {
        s->run = RUN_LOST;
        snprintf(s->outcome, sizeof s->outcome, "hull failed from cold at tick %llu with %d segment(s) aboard",
                 (unsigned long long)s->tick, s->nsegs);
        sim_log(s, "%s", s->outcome);
    } else if (s->tick >= s->max_ticks) {
        s->run = s->credits > 0 ? RUN_WON : RUN_LOST;
        snprintf(s->outcome, sizeof s->outcome, "shift over at tick %llu: %d segment(s), %.0f credits",
                 (unsigned long long)s->tick, s->nsegs, s->credits);
        sim_log(s, "%s", s->outcome);
    } else {
        bool any = false;
        for (int i = 0; i < s->nscripts; i++) if (s->script[i].alive) any = true;
        if (!any) {
            s->run = RUN_LOST;
            snprintf(s->outcome, sizeof s->outcome,
                     "every script stopped at tick %llu", (unsigned long long)s->tick);
            sim_log(s, "%s", s->outcome);
        }
    }
    record_frame(s);
}

uint64_t sim_run_to_end(Sim *s, uint64_t max)
{
    uint64_t n = 0;
    while (s->run == RUN_ACTIVE && n < max) { sim_tick(s); n++; }
    return n;
}

/* ------------------------------------------------------------------ output */
static const char *run_name(RunState r)
{
    switch (r) {
    case RUN_SETUP: return "setup"; case RUN_ACTIVE: return "active";
    case RUN_WON: return "won";     case RUN_LOST: return "lost";
    }
    return "?";
}

static const char *script_state(const Script *sc)
{
    if (sc->killed)    return "killed";
    if (!sc->launched) return "attached";
    if (sc->err[0])    return "faulted";
    if (!sc->alive)    return "exited";
    switch (sc->status) {
    case VM_BLOCKED: return "blocked";
    case VM_SLEEP:   return "sleeping";
    default:         return "running";
    }
}

void sim_status(Sim *s, Buf *out)
{
    buf_printf(out, "run       %s  tick %llu  seed %llu\n", run_name(s->run),
               (unsigned long long)s->tick, (unsigned long long)s->seed);
    if (s->outcome[0]) buf_printf(out, "outcome   %s\n", s->outcome);
    buf_printf(out, "station   %d segment(s), wear ", s->nsegs);
    buf_putnum(out, s->wear * 100.0, 0); buf_puts(out, "%\n");

    buf_printf(out, "reactor   %s  output ", reactor_state_name(s->reactor.state));
    buf_putnum(out, s->reactor.output, 2);
    buf_puts(out, " / "); buf_putnum(out, s->reactor.rated, 2);
    buf_puts(out, " MW   battery "); buf_putnum(out, s->bus.charge, 1);
    if (s->bus.brownout) buf_puts(out, "   *** BROWNOUT ***");
    buf_putc(out, '\n');

    buf_printf(out, "compute   pool %d, scripts used %llu, %d spare for tenants",
               s->cpu.pool, (unsigned long long)s->cpu.executed, s->cpu.spare);
    if (s->cpu.throttled) buf_puts(out, "   *** THERMAL THROTTLE ***");
    buf_puts(out, "\nbay       "); buf_putnum(out, s->cpu.bay_temp, 1);
    buf_puts(out, "C   in "); buf_putnum(out, s->cpu.heat_in, 2);
    buf_puts(out, " out ");   buf_putnum(out, s->cpu.heat_out, 2);
    if (s->cpu.bay_temp < SENSOR_CLEAR_TEMP) buf_puts(out, "   optics COLD");
    buf_putc(out, '\n');

    buf_puts(out, "air       O2 "); buf_putnum(out, s->o2, 1);
    buf_puts(out, "%   rate "); buf_putnum(out, s->o2_rate, 3);
    buf_puts(out, "   credits "); buf_putnum(out, s->credits, 1);
    buf_puts(out, "   net "); buf_putnum(out, s->income_last - s->power_bill_last, 2);
    buf_puts(out, "/tick\n");

    buf_puts(out, "\nslot dev       part           state     hp  duty  draw  got  eff\n");
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (!p) { buf_printf(out, " %-3d (empty)\n", i); continue; }
        Slot *sl = &s->slot[i];
        buf_printf(out, " %-3d %-9s %-14s %-9s ", i, sl->dev, p->id, slot_state_name(sl->state));
        buf_putnum(out, sl->health * 100, 0); buf_puts(out, "  ");
        buf_putnum(out, sl->duty, 2); buf_puts(out, "  ");
        buf_putnum(out, p->draw, 2);  buf_puts(out, "  ");
        buf_putnum(out, sl->supplied, 2); buf_puts(out, "  ");
        buf_putnum(out, slot_effect(s, i) * 100, 0); buf_puts(out, "%");
        if (!sl->enabled) buf_puts(out, "  [disabled]");
        buf_putc(out, '\n');
    }

    if (s->nscripts) buf_puts(out, "\npid script           state     steps   wchan\n");
    for (int i = 0; i < s->nscripts; i++) {
        Script *sc = &s->script[i];
        buf_printf(out, " %-3d %-16s %-9s %-7llu %s\n", i + 1, sc->name, script_state(sc),
                   sc->vm ? (unsigned long long)vm_steps(sc->vm) : 0ULL,
                   (sc->vm && sc->status == VM_BLOCKED) ? vm_blocked_on(sc->vm)
                                                        : (sc->err[0] ? sc->err : ""));
    }
}

void sim_result_json(Sim *s, Buf *out)
{
    buf_printf(out, "{\"result\":\"%s\"", run_name(s->run));
    buf_printf(out, ",\"seed\":%llu", (unsigned long long)s->seed);
    buf_printf(out, ",\"ticks\":%llu", (unsigned long long)s->tick);
    buf_printf(out, ",\"digest\":\"%016llx\"", (unsigned long long)s->digest);
    buf_printf(out, ",\"segments\":%d", s->nsegs);
    buf_puts(out, ",\"credits\":");  buf_putnum(out, s->credits, 2);
    buf_puts(out, ",\"income\":");   buf_putnum(out, s->income_last, 3);
    buf_puts(out, ",\"o2\":");       buf_putnum(out, s->o2, 3);
    buf_puts(out, ",\"o2_rate\":");  buf_putnum(out, s->o2_rate, 4);
    buf_puts(out, ",\"wear\":");     buf_putnum(out, s->wear, 3);
    buf_puts(out, ",\"bay_temp\":"); buf_putnum(out, s->cpu.bay_temp, 4);
    buf_printf(out, ",\"pool\":%d,\"rated\":%d,\"spare\":%d", s->cpu.pool, s->cpu.rated, s->cpu.spare);
    buf_puts(out, ",\"supply\":");   buf_putnum(out, s->reactor.output, 3);

    buf_printf(out, ",\"outcome\":\"%s\"", s->outcome);
    buf_puts(out, ",\"scripts\":[");
    for (int i = 0; i < s->nscripts; i++) {
        Script *sc = &s->script[i];
        if (i) buf_putc(out, ',');
        buf_printf(out, "{\"name\":\"%s\",\"state\":\"%s\",\"steps\":%llu", sc->name, script_state(sc),
                   sc->vm ? (unsigned long long)vm_steps(sc->vm) : 0ULL);
        if (sc->err[0]) buf_printf(out, ",\"error\":\"%s\"", sc->err);
        if (sc->vm && sc->status == VM_BLOCKED) buf_printf(out, ",\"blocked_on\":\"%s\"", vm_blocked_on(sc->vm));
        buf_putc(out, '}');
    }
    buf_puts(out, "]}");
}

void sim_replay_json(Sim *s, Buf *out)
{
    buf_puts(out, "{\"format\":\"nominal-replay-2\",\"scenario\":\"shift\"");
    buf_printf(out, ",\"seed\":%llu", (unsigned long long)s->seed);
    buf_puts(out, ",\"loadout\":[");
    bool first = true;
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = slot_spec(s, i);
        if (!p) continue;
        if (!first) buf_putc(out, ',');
        first = false;
        buf_printf(out, "{\"slot\":%d,\"part\":\"%s\",\"dev\":\"%s\"}", i, p->id, s->slot[i].dev);
    }
    buf_puts(out, "],\"scripts\":[");
    for (int i = 0; i < s->nscripts; i++) { if (i) buf_putc(out, ','); buf_printf(out, "\"%s\"", s->script[i].path); }
    buf_puts(out, "],\n\"frames\":[\n");
    const char *p = s->replay.p ? s->replay.p : "";
    bool f1 = true;
    while (*p) {
        const char *e = p; while (*e && *e != '\n') e++;
        if (!f1) buf_puts(out, ",\n");
        f1 = false;
        buf_putc(out, '"'); buf_put(out, p, (size_t)(e - p)); buf_putc(out, '"');
        p = *e ? e + 1 : e;
    }
    buf_puts(out, "\n],\n\"result\":");
    sim_result_json(s, out);
    buf_puts(out, "}\n");
}
