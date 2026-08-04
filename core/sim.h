/* sim.h — the ship as a machine you administer.
 *
 * There are no hardcoded subsystems any more. There is a chassis with slots,
 * a catalog of parts, and physics that reads whatever is installed. See D13.
 *
 * Everything here is deterministic: a tick counter instead of a clock, an
 * explicit Rng, arrays instead of hash maps, and our own math. See D3.
 */
#ifndef NOM_SIM_H
#define NOM_SIM_H

#include "parts.h"
#include "station.h"

#define SIM_MAX_SCRIPTS    12
#define SIM_MAX_EVENTS     8192
#define BUDGET_MAX_DEFAULT 2000

/* ------------------------------------------------------------- powerplant */
typedef enum {
    REACTOR_COLD = 0,
    REACTOR_PRIMING,
    REACTOR_IDLE,
    REACTOR_SPINUP,
    REACTOR_ONLINE,
    REACTOR_SCRAMMED
} ReactorState;

typedef struct {
    ReactorState state;
    int    timer;
    double output;       /* MW, summed over installed reactor cards */
    double rated;        /* what they would make at full health     */
} Powerplant;

typedef struct {
    double charge, capacity, discharge_max;
    bool   brownout;
} PowerBus;

/* The flight computer pool. Instructions are shared between every running
 * script, so a new daemon costs the others and a second CPU card is a real
 * purchase rather than a number going up. */
typedef struct {
    double   bay_temp;
    int      pool;          /* instructions available this tick, all scripts */
    int      rated;         /* what the installed cards would give, healthy  */
    int      per_script;    /* what one script would get if they shared evenly */
    int      spare;         /* what the scripts did NOT use — the tenants' share */
    uint64_t executed;
    double   heat_in, heat_out;
    bool     throttled;
} Compute;

typedef struct {
    bool     online;
    int      warmup;
    double   bias_full;     /* systematic misalignment at full cold, degrees */
    double   last_bearing, last_range;
    uint64_t last_sample_tick;
    bool     has_reading;
} Sensor;

typedef enum { RUN_SETUP = 0, RUN_ACTIVE, RUN_WON, RUN_LOST } RunState;

/* What the ship can NOTICE, as opposed to what is actually wrong. */
typedef enum {
    SYM_BEARING_UNSTABLE = 0,
    SYM_BAY_OVERHEATING,
    SYM_POWER_SHORTFALL,
    SYM_HARDWARE_FAULT,
    SYM_AIR_FALLING,
    SYM_COMPUTE_STARVED,
    SYM_COUNT
} SymptomId;

typedef struct { uint64_t tick; char text[160]; } SimEvent;

typedef struct Script {
    char      name[NOM_NAME_MAX];
    char      path[NOM_PATH_MAX];
    Prog     *prog;
    VM       *vm;
    bool      alive, launched;
    VmStatus  status;
    char      err[NOM_ERR_MAX];
    uint64_t  steps_last;      /* for /proc accounting */
    bool      killed;
} Script;

struct Sim {
    uint64_t tick, seed;
    Rng      rng;
    RunState run;
    char     outcome[160];

    Slot        slot[SLOT_COUNT];
    Powerplant  reactor;
    PowerBus    bus;
    Compute     cpu;
    Sensor      sensor;

    double   hull_integrity;
    double   wear;
    double   o2;              /* percent. You are aboard. This is your clock. */
    double   o2_rate;         /* what /dev/scrubber claimed last tick        */

    /* --- the economy --- */
    double   credits;
    double   income_last;         /* credits earned last tick   */
    double   power_bill_last;     /* credits burned last tick   */
    double   fuel_rate;           /* credits per MW-tick        */
    double   cable_spent;         /* what the spool has cost you so far */
    bool     nudged_reactor;      /* only say it once */
    Segment  seg[SEG_MAX];
    int      nsegs;
    Order    order[ORDER_MAX];
    Message  msg[MSG_MAX];
    int      nmsgs;
    char     here[24];            /* which segment you are standing in */
    uint64_t next_dock_tick;
    uint64_t next_fault_tick;
    bool     symptom[SYM_COUNT];
    uint64_t symptom_since[SYM_COUNT];

    char     alarm_q[16][64];
    int      alarm_head, alarm_tail;

    int      budget_max;           /* a cap the player can set in /etc       */
    uint64_t max_ticks;

    Script   script[SIM_MAX_SCRIPTS];
    int      nscripts;

    SimEvent event[SIM_MAX_EVENTS];
    int      nevents;

    Vfs      fs;
    char     home[NOM_PATH_MAX];
    Buf      replay;
    bool     recording;
    uint64_t digest;
};

Sim  *sim_new(uint64_t seed);
void  sim_free(Sim *s);
void  sim_reset(Sim *s, uint64_t seed);


/* Hardware. Installing rebuilds /dev, so a script that referenced a device
 * that is now gone starts failing on its next read — which is the point. */
bool  sim_install(Sim *s, int slotno, const char *part_id, char *err, size_t errsz);
bool  sim_remove(Sim *s, int slotno, char *err, size_t errsz);
void  sim_rebuild_devtree(Sim *s);
void  sim_rebuild_srv(Sim *s);
int   sim_slot_of_dev(Sim *s, const char *dev);
/* Patching. Which rail you use is the decision; ports are finite. */
/* Patching costs cable, and cable costs money. `metres` is what the measuring
 * tool read; pass 0 and it charges a nominal in-rack run. */
bool  sim_connect(Sim *s, const char *dev, const char *link, double metres, char *err, size_t errsz);
double sim_cable_rate(void);
/* The cable tool: measure the run between two things, replicate a cable that
 * long, and transport it into place. `wire` is the whole job in one verb, so
 * dragging is a convenience rather than the only way to do it. */
double sim_measure(Sim *s, const char *from, const char *to);
bool   sim_wire(Sim *s, const char *from, const char *to, char *err, size_t errsz);
bool   sim_unwire(Sim *s, const char *from, const char *to, char *err, size_t errsz);
bool   sim_place(Sim *s, const char *what, double x, double y, char *err, size_t errsz);
double sim_cable_spent(Sim *s);
bool  sim_disconnect(Sim *s, const char *dev, const char *link, char *err, size_t errsz);
/* traceroute for infrastructure: follow a device back through what it needs. */
void  sim_trace(Sim *s, const char *dev, Buf *out);

bool  sim_load_home(Sim *s, const char *path, char *err, size_t errsz);
bool  sim_save_home(Sim *s, const char *path);

bool  sim_attach(Sim *s, const char *vfspath, char *err, size_t errsz);
void  sim_detach_all(Sim *s);
bool  sim_launch(Sim *s, char *err, size_t errsz);
/* Job control. A sysadmin who cannot stop a runaway daemon is not an admin. */
bool  sim_kill(Sim *s, int pid, char *err, size_t errsz);
bool  sim_restart(Sim *s, int pid, char *err, size_t errsz);
bool  sim_start_script(Sim *s, const char *path, char *err, size_t errsz);
void  sim_publish_proc(Sim *s);

void  sim_tick(Sim *s);
uint64_t sim_run_to_end(Sim *s, uint64_t max);

void  sim_log(Sim *s, const char *fmt, ...);
void  sim_status(Sim *s, Buf *out);
void  sim_result_json(Sim *s, Buf *out);
void  sim_replay_json(Sim *s, Buf *out);

/* Economy and station verbs. */
bool  sim_order(Sim *s, const char *part_id, char *err, size_t errsz);
bool  sim_teleport(Sim *s, const char *where, char *err, size_t errsz);
bool  sim_dock_segment(Sim *s, const char *kind, char *err, size_t errsz);
bool  sim_seg_patch(Sim *s, const char *seg, const char *link, char *err, size_t errsz);
int   sim_seg_index(Sim *s, const char *name);
void  sim_serve(Sim *s, int segidx);
bool  sim_priority(Sim *s, const char *seg, int pos, char *err, size_t errsz);
double sim_freshness(Sim *s, int segidx);
void  sim_station(Sim *s, Buf *out);
void  sim_message(Sim *s, const char *from, const char *fmt, ...);
void  sim_messages(Sim *s, Buf *out, bool unread_only);
int   sim_unread(Sim *s);
/* Helpers station.c needs from sim.c. */
const PartSpec *sim_part_of_slot(Sim *s, int i);
const char     *sim_dev_of_slot(Sim *s, int i);
/* How much of `want` a consumer gets from a shared link, after everything else
 * on that link has asked for its share too. */
double sim_link_share(Sim *s, int linkslot, double want, bool power);
double sim_link_load_of(Sim *s, int linkslot, bool power);
double sim_replicator_draw(Sim *s);
void   sim_step_orders(Sim *s);
void   sim_step_segments(Sim *s);
void   sim_step_bill(Sim *s);
/* Everything the desktop draws, as one "key value" block. */
void   sim_telemetry(Sim *s, Buf *out);
uint64_t sim_state_digest(Sim *s);

#endif /* NOM_SIM_H */
