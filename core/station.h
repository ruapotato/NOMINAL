/* station.h — segments, the economy, the replicator and the teleporter.
 *
 * A segment is a paying tenant with needs. Meet them and it earns; starve it
 * and it earns less. That is the whole scoring system: there is no abstract
 * score, there is a bank balance. See D16.
 */
#ifndef NOM_STATION_H
#define NOM_STATION_H

#define SEG_MAX          12
#define ORDER_MAX         8
#define REPLICATOR_TICKS 40      /* how long a part takes to print */

typedef struct {
    char   name[24];        /* "hab-1", "lab-2" */
    char   kind[16];        /* habitat | lab | foundry | dock          */
    bool   docked;

    double power_need;      /* MW it wants                            */
    double data_need;       /* spine capacity it wants                */
    int    cpu_need;        /* instructions/tick its automation wants */

    int    rail;            /* slot index of the rail it is patched into, -1 = loose */
    int    spine;

    double pay;             /* credits per tick at full service       */
    double service;         /* 0..1 achieved last tick                */
    double served_ticks;    /* ticks at or above SLA                  */
    double docked_ticks;
    double sla;             /* what they were promised, 0..1          */
    int    starved_for;     /* consecutive ticks below SLA            */
    bool   complained;      /* so they nag once per episode, not per tick */
    uint64_t docked_at;
    double   x, y;          /* where its hatch is on the bay wall */
    /* A tenant is only served if something is actually LOOKING AFTER it: a
     * script has to write /srv/<name>/heartbeat regularly. That is what puts
     * your code at the centre of the game instead of the cabling. */
    uint64_t last_served;
    bool     ever_served;
} Segment;

#define MSG_MAX  64

/* A message from a tenant. People report symptoms, never causes — that is the
 * whole reason the pager is an IM client and not an error code. See D14. */
typedef struct {
    char     from[24];
    char     text[160];
    uint64_t tick;
    bool     unread;
} Message;

typedef struct {
    char     part[PART_ID_MAX];
    int      ready_at;      /* tick the replicator finishes it */
    bool     active;
} Order;

#endif /* NOM_STATION_H */
