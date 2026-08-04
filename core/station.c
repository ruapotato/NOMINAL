/* station.c — segments, the economy, the replicator and the teleporter.
 *
 * The whole scoring system is the bank balance. A segment whose power, data
 * and compute needs are met pays; one that is starved pays less. Bad
 * administration shows up as a smaller number, not as an abstract score.
 *
 * Power is the universal currency: running hardware, printing a part and
 * teleporting across the station all meter through it, and power costs money.
 * Every decision lands on one bill. See D16.
 */
#include "nom.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* What a segment of each kind wants, and what it pays for being looked after.
 * Pay is roughly proportional to need, so a bigger tenant is not free money —
 * it is a bigger tenant. */
static const struct {
    const char *kind;
    double power, data;
    int    cpu;
    double pay;
    const char *blurb;
} SEG_KINDS[] = {
    { "habitat", 1.2, 0.8,  200, 3.0, "forty residents. wants air and light and not much else." },
    { "lab",     2.4, 2.2,  600, 7.0, "sample freezers and a lot of telemetry. fussy." },
    { "foundry", 4.0, 1.0,  300, 9.0, "hungry. mostly power, barely talks." },
    { "dock",    1.6, 3.0,  500, 6.0, "traffic control. light on power, heavy on the spine." },
};
#define SEG_KIND_COUNT ((int)(sizeof SEG_KINDS / sizeof SEG_KINDS[0]))

/* ------------------------------------------------------------------ pager */
void sim_message(Sim *s, const char *from, const char *fmt, ...)
{
    if (s->nmsgs >= MSG_MAX) {                /* keep the most recent */
        memmove(&s->msg[0], &s->msg[1], sizeof(Message) * (MSG_MAX - 1));
        s->nmsgs = MSG_MAX - 1;
    }
    Message *m = &s->msg[s->nmsgs++];
    snprintf(m->from, sizeof m->from, "%s", from);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m->text, sizeof m->text, fmt, ap);
    va_end(ap);
    m->tick = s->tick;
    m->unread = true;
}

int sim_unread(Sim *s)
{
    int n = 0;
    for (int i = 0; i < s->nmsgs; i++) if (s->msg[i].unread) n++;
    return n;
}

void sim_messages(Sim *s, Buf *out, bool unread_only)
{
    for (int i = 0; i < s->nmsgs; i++) {
        if (unread_only && !s->msg[i].unread) continue;
        buf_printf(out, "%6llu  %-10s %s\n", (unsigned long long)s->msg[i].tick,
                   s->msg[i].from, s->msg[i].text);
        s->msg[i].unread = false;
    }
}

/* What a tenant says when they are starved. They describe what they can see
 * from inside their own segment, which is never the cause. */
static const char *COMPLAINT[4][3] = {
    /* habitat */ { "lights keep browning out down here. is that us or you?",
                    "it's getting cold in the dorms. nobody's touched anything.",
                    "our terminals keep dropping. can't file anything." },
    /* lab     */ { "freezers are cycling. we are going to lose samples.",
                    "telemetry is dropping frames and the run is ruined.",
                    "instruments keep resetting mid-measurement. help?" },
    /* foundry */ { "we're running at half rate. can you give us more juice?",
                    "the furnace won't hold temperature.",
                    "line stopped twice this hour. losing the batch." },
    /* dock    */ { "traffic control is lagging. we've had two near misses.",
                    "we can't see inbound until they're right on top of us.",
                    "berth telemetry is garbage. holding all traffic." },
};

static int kind_index(const char *kind)
{
    if (!strcmp(kind, "habitat")) return 0;
    if (!strcmp(kind, "lab"))     return 1;
    if (!strcmp(kind, "foundry")) return 2;
    return 3;
}

/* How long a heartbeat is good for. Miss the window and the tenant starts
 * losing service, whatever the cabling says. */
#define HEARTBEAT_OK    40
#define HEARTBEAT_DEAD 140

/* A tenant who is badly served for long enough gives notice and then leaves.
 * Without this the station just declines gently while credits pile up, and
 * nothing ever forces you to spend them. Losing the rent is the consequence
 * that makes the whole economy bite. */
#define NOTICE_TICKS   520
#define QUIT_TICKS    1150

void sim_serve(Sim *s, int i)
{
    if (i < 0 || i >= s->nsegs) return;
    s->seg[i].last_served = s->tick;
    s->seg[i].ever_served = true;
}

double sim_freshness(Sim *s, int i)
{
    Segment *sg = &s->seg[i];
    if (!sg->ever_served) return 0.0;
    double age = (double)(s->tick - sg->last_served);
    if (age <= HEARTBEAT_OK) return 1.0;
    if (age >= HEARTBEAT_DEAD) return 0.0;
    return 1.0 - (age - HEARTBEAT_OK) / (double)(HEARTBEAT_DEAD - HEARTBEAT_OK);
}

int sim_seg_index(Sim *s, const char *name)
{
    for (int i = 0; i < s->nsegs; i++)
        if (s->seg[i].docked && strcmp(s->seg[i].name, name) == 0) return i;
    return -1;
}

/* Move a tenant up or down the shed order. This is the triage decision. */
bool sim_priority(Sim *s, const char *seg, int pos, char *err, size_t errsz)
{
    int i = sim_seg_index(s, seg);
    if (i < 0) { snprintf(err, errsz, "no tenant '%s'", seg); return false; }
    if (pos < 1) pos = 1;
    if (pos > s->nsegs) pos = s->nsegs;
    int to = pos - 1;
    Segment tmp = s->seg[i];
    if (to < i) for (int j = i; j > to; j--) s->seg[j] = s->seg[j - 1];
    else        for (int j = i; j < to; j++) s->seg[j] = s->seg[j + 1];
    s->seg[to] = tmp;
    sim_rebuild_srv(s);
    sim_log(s, "%s moved to priority %d", seg, pos);
    return true;
}

bool sim_dock_segment(Sim *s, const char *kind, char *err, size_t errsz)
{
    if (s->nsegs >= SEG_MAX) { snprintf(err, errsz, "no free docking rings"); return false; }
    int k = -1;
    for (int i = 0; i < SEG_KIND_COUNT; i++)
        if (strcmp(SEG_KINDS[i].kind, kind) == 0) k = i;
    if (k < 0) { snprintf(err, errsz, "no segment kind '%s'", kind); return false; }

    Segment *sg = &s->seg[s->nsegs];
    memset(sg, 0, sizeof *sg);
    int n = 1;
    for (int i = 0; i < s->nsegs; i++) if (strncmp(s->seg[i].name, kind, 3) == 0) n++;
    snprintf(sg->name, sizeof sg->name, "%.3s-%d", kind, n);
    snprintf(sg->kind, sizeof sg->kind, "%s", kind);
    sg->docked = true;
    sg->power_need = SEG_KINDS[k].power;
    sg->data_need  = SEG_KINDS[k].data;
    sg->cpu_need   = SEG_KINDS[k].cpu;
    sg->pay        = SEG_KINDS[k].pay;
    sg->rail = sg->spine = -1;
    sg->sla = 0.95;
    sg->x = 330 + s->nsegs * 208;
    sg->y = 514;
    sg->docked_at = s->tick;
    s->nsegs++;

    /* Auto-patch. The cabling is plumbing you CAN optimise, not a chore you
     * must do before anything works. */
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = sim_part_of_slot(s, i);
        if (!p) continue;
        if (p->kind == K_PWRBUS  && sg->rail  < 0) sg->rail  = i;
        if (p->kind == K_DATABUS && sg->spine < 0) sg->spine = i;
    }

    sim_rebuild_srv(s);
    sim_log(s, "%s docked: %s", sg->name, SEG_KINDS[k].blurb);
    sim_message(s, sg->name, "we're berthed at ring %d and the utilities are hooked up. "
                             "we need someone running ops for us — /srv/%s/heartbeat.",
                s->nsegs, sg->name);
    sim_log(s, "%s: wants %.1f MW, %.1f spine, %d instr — patch it in or it pays nothing",
            sg->name, sg->power_need, sg->data_need, sg->cpu_need);
    return true;
}

bool sim_seg_patch(Sim *s, const char *seg, const char *link, char *err, size_t errsz)
{
    int si = sim_seg_index(s, seg);
    if (si < 0) { snprintf(err, errsz, "no segment '%s'", seg); return false; }
    int li = sim_slot_of_dev(s, link);
    if (li < 0) { snprintf(err, errsz, "no link '%s'", link); return false; }
    const PartSpec *lp = sim_part_of_slot(s, li);
    if (!lp) { snprintf(err, errsz, "slot is empty"); return false; }

    if (lp->kind == K_PWRBUS)       s->seg[si].rail = li;
    else if (lp->kind == K_DATABUS) s->seg[si].spine = li;
    else { snprintf(err, errsz, "%s is a %s, not a rail or a spine", link, part_kind_name(lp->kind)); return false; }
    sim_log(s, "%s patched into %s", seg, link);
    return true;
}

/* ------------------------------------------------------------ replicator */
bool sim_order(Sim *s, const char *part_id, char *err, size_t errsz)
{
    int ci = catalog_find(part_id);
    if (ci < 0) { snprintf(err, errsz, "the replicator has no pattern for '%s'", part_id); return false; }
    int nc; const PartSpec *cat = catalog(&nc);

    if ((double)cat[ci].price > s->credits) {
        snprintf(err, errsz, "%s costs %d and you have %.0f",
                 cat[ci].id, cat[ci].price, s->credits);
        return false;
    }
    int slot = -1;
    for (int i = 0; i < ORDER_MAX; i++) if (!s->order[i].active) { slot = i; break; }
    if (slot < 0) { snprintf(err, errsz, "the replicator queue is full"); return false; }

    s->credits -= cat[ci].price;
    s->order[slot].active = true;
    s->order[slot].ready_at = (int)s->tick + REPLICATOR_TICKS;
    snprintf(s->order[slot].part, PART_ID_MAX, "%s", cat[ci].id);
    sim_log(s, "replicator: printing %s, ready in %d ticks (-%d cr)",
            cat[ci].id, REPLICATOR_TICKS, cat[ci].price);
    return true;
}

/* The replicator draws hard while it is printing; that is the power spike
 * that makes ordering something you plan rather than something you spam. */
double sim_replicator_draw(Sim *s)
{
    double d = 0.0;
    for (int i = 0; i < ORDER_MAX; i++) if (s->order[i].active) d += 2.5;
    return d;
}

void sim_step_orders(Sim *s)
{
    for (int i = 0; i < ORDER_MAX; i++) {
        if (!s->order[i].active) continue;
        if ((int)s->tick < s->order[i].ready_at) continue;
        s->order[i].active = false;
        char path[NOM_PATH_MAX];
        snprintf(path, sizeof path, "/mnt/replicator/%s", s->order[i].part);
        vfs_mkfile(&s->fs, path, "printed. `install <part> <slot>` to fit it.\n");
        sim_log(s, "replicator: %s is ready in the receiving bay", s->order[i].part);
    }
}

/* ------------------------------------------------------------- teleporter */
bool sim_teleport(Sim *s, const char *where, char *err, size_t errsz)
{
    bool known = (strcmp(where, "core") == 0) || (strcmp(where, "plant") == 0);
    if (!known && sim_seg_index(s, where) < 0) {
        snprintf(err, errsz, "the teleporter has no pad in '%s'", where);
        return false;
    }
    double cost = 0.4;                 /* small on purpose: a trip should feel
                                        * like something, not deter maintenance */
    if (s->credits < cost) { snprintf(err, errsz, "not enough credits to cycle the pad"); return false; }
    s->credits -= cost;
    snprintf(s->here, sizeof s->here, "%s", where);
    sim_log(s, "teleported to %s (-%.1f cr)", where, cost);
    return true;
}

/* ---------------------------------------------------------------- economy */
/* A segment is served only as well as the worst of its three needs. Meeting
 * two out of three perfectly is not worth much, which is what stops the player
 * from over-provisioning one utility and ignoring another. */
/* Utilities are SHED BY PRIORITY, not shared equally. When the station cannot
 * serve everyone, the tenants at the top of the list stay at full service and
 * the ones at the bottom get nothing — which is what a real plant does, and
 * which turns "everything is slightly broken" into "choose who to drop while
 * you buy capacity". Priority is the order they are listed; `priority` moves
 * a tenant up or down. */
void sim_step_segments(Sim *s)
{
    double income = 0.0;

    /* what is left for tenants after the station's own hardware has drawn */
    double p_left = s->reactor.output;
    double d_left = 0.0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = sim_part_of_slot(s, i);
        if (!p) continue;
        if (p->kind == K_DATABUS) d_left += p->spec;
        else p_left -= s->slot[i].supplied;
    }
    if (p_left < 0) p_left = 0;
    /* tenants are served from the instructions your scripts left behind */
    double c_left = (double)s->cpu.spare;

    for (int i = 0; i < s->nsegs; i++) {
        Segment *sg = &s->seg[i];
        if (!sg->docked) continue;
        sg->docked_ticks += 1.0;

        /* each tenant takes its full need if the station can still cover it */
        double pf = (sg->rail  >= 0 && p_left >= sg->power_need) ? 1.0
                  : (sg->rail  >= 0 && sg->power_need > 0 ? p_left / sg->power_need : 0.0);
        double df = (sg->spine >= 0 && d_left >= sg->data_need) ? 1.0
                  : (sg->spine >= 0 && sg->data_need > 0 ? d_left / sg->data_need : 0.0);
        double cf = (c_left >= (double)sg->cpu_need) ? 1.0
                  : (sg->cpu_need > 0 ? c_left / (double)sg->cpu_need : 1.0);
        if (pf > 1.0) pf = 1.0;
        if (df > 1.0) df = 1.0;
        if (cf > 1.0) cf = 1.0;
        if (pf < 0) pf = 0;
        if (df < 0) df = 0;
        if (cf < 0) cf = 0;
        p_left -= sg->power_need * pf;
        d_left -= sg->data_need * df;
        c_left -= (double)sg->cpu_need * cf;
        double svc = pf < df ? pf : df;
        if (cf < svc) svc = cf;
        double fr = sim_freshness(s, i);
        if (fr < svc) svc = fr;               /* nobody looking after it = no pay */
        sg->service = svc;
        if (svc >= sg->sla) {
            sg->served_ticks += 1.0;
            sg->starved_for = 0;
            sg->complained = false;
        } else {
            sg->starved_for++;
            if (sg->starved_for == NOTICE_TICKS)
                sim_message(s, sg->name, "this is not what we signed for. sort it out or we are "
                                         "taking the module elsewhere.");
            if (sg->starved_for >= QUIT_TICKS) {
                sim_message(s, sg->name, "we are gone. good luck.");
                sim_log(s, "%s TERMINATED the contract after %d ticks below SLA",
                        sg->name, sg->starved_for);
                sg->docked = false;
                sim_rebuild_srv(s);
                continue;
            }
            /* Nag once per episode, not once per tick. A pager that spams is
             * a pager you learn to ignore. */
            if (sg->starved_for == 25 && !sg->complained && s->tick > 90) {
                sg->complained = true;
                int k = kind_index(sg->kind);
                /* which of the three needs is worst decides what they notice */
                if (fr <= pf && fr <= df && fr <= cf) {
                    sim_message(s, sg->name, "is anyone actually running anything for us? "
                                             "we have had nothing from ops for a while.");
                } else {
                    int worst = (pf <= df && pf <= cf) ? 0 : (df <= cf ? 2 : 1);
                    sim_message(s, sg->name, "%s", COMPLAINT[k][worst]);
                }
            }
        }
        income += sg->pay * svc;
    }
    s->income_last = income;
    s->credits += income;

    /* If the station is short of power and you can afford a better reactor,
     * say so once. A game that lets you sit on twenty thousand credits while
     * everything degrades is not being honest with you. */
    if (!s->nudged_reactor && s->tick > 120) {
        int starving = 0;
        for (int i = 0; i < s->nsegs; i++)
            if (s->seg[i].docked && s->seg[i].service < 0.75) starving++;
        if (starving >= 2) {
            s->nudged_reactor = true;
            if (s->credits >= 1500.0)
                sim_message(s, "ops-advisory",
                            "you are power-limited. an reactor-a2 uprate is 1500 and you have %.0f. "
                            "`order reactor-a2`, then `install reactor-a2 <slot>`.", s->credits);
            else
                sim_message(s, "ops-advisory",
                            "you are power-limited. the cheapest fix is a reactor-a2 uprate at 1500; "
                            "you have %.0f. bank it before the tenants give notice.", s->credits);
        }
    }
}

void sim_step_bill(Sim *s)
{
    double mw = s->reactor.output;
    double bill = mw * s->fuel_rate;
    s->power_bill_last = bill;
    s->credits -= bill;
    if (s->credits < 0) s->credits = 0;
}

/* Everything the desktop draws. Same "key value" shape as every device file,
 * so the UI has exactly one parser. */
void sim_telemetry(Sim *s, Buf *out)
{
    buf_printf(out, "tick %llu\n", (unsigned long long)s->tick);
    buf_printf(out, "run %d\n", (int)s->run);
    buf_printf(out, "here %s\n", s->here[0] ? s->here : "core");
    buf_printf(out, "unread %d\n", sim_unread(s));
    buf_puts(out, "credits ");    buf_putnum(out, s->credits, 1);        buf_putc(out, '\n');
    buf_puts(out, "income ");     buf_putnum(out, s->income_last, 2);    buf_putc(out, '\n');
    buf_puts(out, "bill ");       buf_putnum(out, s->power_bill_last, 2); buf_putc(out, '\n');
    buf_puts(out, "o2 ");         buf_putnum(out, s->o2, 2);             buf_putc(out, '\n');
    buf_puts(out, "o2_rate ");    buf_putnum(out, s->o2_rate, 3);        buf_putc(out, '\n');
    buf_puts(out, "bay ");        buf_putnum(out, s->cpu.bay_temp, 1);   buf_putc(out, '\n');
    buf_printf(out, "pool %d\n", s->cpu.pool);
    buf_printf(out, "rated %d\n", s->cpu.rated);
    buf_printf(out, "throttled %d\n", s->cpu.throttled ? 1 : 0);
    buf_puts(out, "supply ");     buf_putnum(out, s->reactor.output, 2); buf_putc(out, '\n');
    buf_puts(out, "supply_rated "); buf_putnum(out, s->reactor.rated, 2); buf_putc(out, '\n');
    buf_puts(out, "battery ");    buf_putnum(out, s->bus.charge, 1);     buf_putc(out, '\n');
    buf_puts(out, "wear ");       buf_putnum(out, s->wear * 100, 1);     buf_putc(out, '\n');
    buf_puts(out, "cable_rate ");  buf_putnum(out, sim_cable_rate(), 1);  buf_putc(out, '\n');
    buf_puts(out, "cable_spent "); buf_putnum(out, s->cable_spent, 1);    buf_putc(out, '\n');
    buf_printf(out, "brownout %d\n", s->bus.brownout ? 1 : 0);
    buf_printf(out, "reactor_state %d\n", (int)s->reactor.state);

    /* slot<i> dev part kind state health rail spine draw data */
    int nc; const PartSpec *cat = catalog(&nc);
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (s->slot[i].part < 0) { buf_printf(out, "slot%d empty\n", i); continue; }
        const PartSpec *p = &cat[s->slot[i].part];
        buf_printf(out, "slot%d %s %s %s %d %d %s %s ", i, s->slot[i].dev, p->id,
                   part_kind_name(p->kind), (int)s->slot[i].state,
                   (int)(s->slot[i].health * 100),
                   sim_dev_of_slot(s, s->slot[i].link[0]),
                   sim_dev_of_slot(s, s->slot[i].link[1]));
        buf_putnum(out, p->draw, 2); buf_putc(out, ' ');
        buf_putnum(out, p->data, 2);
        buf_printf(out, " %d ", (int)p->needs);
        buf_putnum(out, s->slot[i].x, 1); buf_putc(out, ' ');
        buf_putnum(out, s->slot[i].y, 1); buf_putc(out, '\n');
    }
    /* seg<i> name kind service pay rail spine power data */
    for (int i = 0; i < s->nsegs; i++) {
        Segment *sg = &s->seg[i];
        buf_printf(out, "seg%d %s %s ", i, sg->name, sg->kind);
        buf_putnum(out, sg->service * 100, 0); buf_putc(out, ' ');
        buf_putnum(out, sg->pay * sg->service, 2);
        buf_printf(out, " %s %s ", sim_dev_of_slot(s, sg->rail), sim_dev_of_slot(s, sg->spine));
        buf_putnum(out, sg->power_need, 2); buf_putc(out, ' ');
        buf_putnum(out, sg->data_need, 2);  buf_putc(out, ' ');
        buf_putnum(out, sg->x, 1); buf_putc(out, ' ');
        buf_putnum(out, sg->y, 1); buf_putc(out, '\n');
    }
    /* link<i> dev kind capacity load ports */
    for (int i = 0; i < SLOT_COUNT; i++) {
        const PartSpec *p = sim_part_of_slot(s, i);
        if (!p || (p->kind != K_PWRBUS && p->kind != K_DATABUS)) continue;
        bool power = (p->kind == K_PWRBUS);
        buf_printf(out, "link%d %s %s ", i, s->slot[i].dev, power ? "power" : "data");
        buf_putnum(out, p->spec, 2); buf_putc(out, ' ');
        buf_putnum(out, sim_link_load_of(s, i, power), 2);
        buf_printf(out, " %d\n", (int)p->spec2);
    }
    buf_printf(out, "outcome %s\n", s->outcome);
}

/* ---------------------------------------------------------- the cable tool */
/* Anything that can be an end of a cable: a card, a switch, or a segment. */
static bool endpoint_pos(Sim *s, const char *name, double *x, double *y, bool *is_seg)
{
    int si = sim_seg_index(s, name);
    if (si >= 0) { *x = s->seg[si].x + 158; *y = s->seg[si].y + 24; *is_seg = true; return true; }
    int d = sim_slot_of_dev(s, name);
    if (d >= 0)  { *x = s->slot[d].x + 100; *y = s->slot[d].y + 30; *is_seg = false; return true; }
    return false;
}

/* What the tool reads: the straight run plus 18% for slack and dressing, the
 * same figure the room view shows while you drag. */
double sim_measure(Sim *s, const char *from, const char *to)
{
    double ax, ay, bx, by;
    bool sa, sb;
    if (!endpoint_pos(s, from, &ax, &ay, &sa)) return 0.0;
    if (!endpoint_pos(s, to, &bx, &by, &sb)) return 0.0;
    double dx = ax - bx, dy = ay - by;
    return nom_sqrt(dx * dx + dy * dy) * 1.18 / 46.0;   /* 46 px to the metre */
}

bool sim_wire(Sim *s, const char *from, const char *to, char *err, size_t errsz)
{
    double m = sim_measure(s, from, to);
    int si = sim_seg_index(s, from);
    if (si >= 0) {
        /* a segment's drop: the length is billed the same way */
        double cost = (m < 1.5 ? 1.5 : m) * sim_cable_rate();
        if (cost > s->credits) {
            snprintf(err, errsz, "a %.1f m drop costs %.0f cr and you have %.0f",
                     m, cost, s->credits);
            return false;
        }
        if (!sim_seg_patch(s, from, to, err, errsz)) return false;
        s->credits -= cost;
        s->cable_spent += cost;
        sim_log(s, "cable tool: %.1f m drop replicated and run, %s -> %s (-%.0f cr)",
                m, from, to, cost);
        return true;
    }
    return sim_connect(s, from, to, m, err, errsz);
}

bool sim_unwire(Sim *s, const char *from, const char *to, char *err, size_t errsz)
{
    int si = sim_seg_index(s, from);
    if (si >= 0) {
        if (s->seg[si].rail >= 0 && strcmp(sim_dev_of_slot(s, s->seg[si].rail), to) == 0)
            s->seg[si].rail = -1;
        else if (s->seg[si].spine >= 0 && strcmp(sim_dev_of_slot(s, s->seg[si].spine), to) == 0)
            s->seg[si].spine = -1;
        else { snprintf(err, errsz, "%s is not wired to %s", from, to); return false; }
        sim_log(s, "%s unwired from %s", from, to);
        return true;
    }
    return sim_disconnect(s, from, to, err, errsz);
}

/* Move something in the bay. The room view calls this when you drag, so the
 * measured length and the drawn cable never disagree. */
bool sim_place(Sim *s, const char *what, double x, double y, char *err, size_t errsz)
{
    int si = sim_seg_index(s, what);
    if (si >= 0) { s->seg[si].x = x; s->seg[si].y = y; return true; }
    int d = sim_slot_of_dev(s, what);
    if (d >= 0)  { s->slot[d].x = x; s->slot[d].y = y; return true; }
    snprintf(err, errsz, "nothing called '%s' in the bay", what);
    return false;
}

/* ------------------------------------------------------------------- view */
void sim_station(Sim *s, Buf *out)
{
    buf_puts(out, "credits   ");
    buf_putnum(out, s->credits, 1);
    buf_puts(out, "    income +");
    buf_putnum(out, s->income_last, 2);
    buf_puts(out, "/tick    power bill -");
    buf_putnum(out, s->power_bill_last, 2);
    buf_puts(out, "/tick    net ");
    buf_putnum(out, s->income_last - s->power_bill_last, 2);
    buf_putc(out, '\n');
    buf_printf(out, "you are in %s\n\n", s->here[0] ? s->here : "core");

    buf_puts(out, "pri segment   kind      rail     spine    power  data  cpu   service  uptime  pays\n");
    for (int i = 0; i < s->nsegs; i++) {
        Segment *sg = &s->seg[i];
        if (!sg->docked) continue;
        buf_printf(out, "%-3d %-9s %-9s %-8s %-8s ", i + 1, sg->name, sg->kind,
                   sg->rail  >= 0 ? sim_dev_of_slot(s, sg->rail)  : "-",
                   sg->spine >= 0 ? sim_dev_of_slot(s, sg->spine) : "-");
        buf_putnum(out, sg->power_need, 1); buf_puts(out, "    ");
        buf_putnum(out, sg->data_need, 1);  buf_puts(out, "   ");
        buf_printf(out, "%-5d ", sg->cpu_need);
        buf_putnum(out, sg->service * 100, 0);
        buf_puts(out, "%     ");
        buf_putnum(out, sg->docked_ticks > 0 ? sg->served_ticks / sg->docked_ticks * 100 : 100, 0);
        buf_puts(out, "%     ");
        buf_putnum(out, sg->pay * sg->service, 2);
        if (sg->service < sg->sla) {
            double fr = sim_freshness(s, i);
            double pf = sg->rail  >= 0 ? sim_link_share(s, sg->rail,  sg->power_need, true)  : 0.0;
            double df = sg->spine >= 0 ? sim_link_share(s, sg->spine, sg->data_need, false) : 0.0;
            const char *why = "power";
            if (fr <= pf && fr <= df) why = sg->ever_served ? "no heartbeat" : "NOT SERVED";
            else if (df < pf) why = "spine";
            buf_printf(out, "   <-- %s", why);
        }
        buf_putc(out, '\n');
    }

    int pending = 0;
    for (int i = 0; i < ORDER_MAX; i++) if (s->order[i].active) pending++;
    if (pending) {
        buf_puts(out, "\nreplicator\n");
        for (int i = 0; i < ORDER_MAX; i++)
            if (s->order[i].active)
                buf_printf(out, "  %-14s ready in %d ticks\n", s->order[i].part,
                           s->order[i].ready_at - (int)s->tick);
    }
}
