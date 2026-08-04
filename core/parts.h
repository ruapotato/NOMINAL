/* parts.h — hardware as data.
 *
 * The ship is a chassis with slots. A slot holds a part bought from a catalog.
 * /dev is generated from what is actually installed, so pulling a card removes
 * its device files and every script that touched them starts failing. See D13.
 */
#ifndef NOM_PARTS_H
#define NOM_PARTS_H

#define SLOT_COUNT      10
#define CATALOG_MAX     48
#define PART_ID_MAX     24
#define PART_NAME_MAX   40

/* What a card has to be patched into before it does anything. A device that is
 * installed but not wired shows up in the inventory and reports nothing —
 * which is a different fault from a misconfigured one, and looks different. */
#define NEEDS_PWR   0x1
#define NEEDS_DATA  0x2

#define LINK_COUNT  6

typedef enum {
    K_NONE = 0,
    K_REACTOR,     /* spec = MW output                                   */
    K_BATTERY,     /* spec = capacity, spec2 = max discharge rate        */
    K_CPU,         /* spec = instructions/tick, spec2 = heat per kinstr  */
    K_RADIATOR,    /* spec = heat rejected per MW supplied               */
    K_SENSOR,      /* spec = bearing accuracy, spec2 = warmup ticks      */
    K_THRUSTER,    /* spec = acceleration at full power                  */
    K_SCRUBBER,    /* spec = O2 percent per tick at full duty            */
    K_PWRBUS,      /* spec = MW it can carry, spec2 = ports               */
    K_DATABUS,     /* spec = data units it can carry, spec2 = ports       */
    K_KIND_COUNT
} PartKind;

/* A catalog entry. Immutable; what you buy is an instance in a slot. */
typedef struct {
    char     id[PART_ID_MAX];        /* "cpu-mk2"                       */
    char     name[PART_NAME_MAX];
    PartKind kind;
    int      price;
    double   draw;                   /* MW it wants when enabled        */
    double   spec, spec2;
    double   heat;                   /* MW-equivalent of waste heat at full duty */
    int      mtbf;                   /* mean ticks between failures; 0 = very reliable */
    int      firmware;               /* revision it ships with          */
    uint8_t  needs;                  /* NEEDS_PWR | NEEDS_DATA          */
    double   data;                   /* load it puts on a data bus      */
    char     desc[96];
} PartSpec;

typedef enum {
    SLOT_EMPTY = 0,
    SLOT_OK,
    SLOT_DEGRADED,     /* works, badly — the interesting state          */
    SLOT_FAILED        /* does nothing until repaired or replaced       */
} SlotState;

typedef struct {
    int        part;                 /* catalog index, -1 when empty    */
    char       dev[PART_ID_MAX];     /* instance name: "cpu0", "rad1"   */
    SlotState  state;
    double     health;               /* 1.0 new, 0.0 dead               */
    bool       enabled;              /* /etc/<dev>.conf: enabled        */
    double     duty;                 /* /etc/<dev>.conf: 0..1 clock/throttle */
    int        firmware;             /* installed revision              */
    int        link[2];              /* which bus each need is patched into, -1 = loose */
    double     cable_m[2];           /* metres of cable run to each, for the bill */
    double     x, y;                 /* where it physically sits in the bay      */
    double     request;              /* MW asked of the bus             */
    double     supplied;             /* MW it actually got              */
    uint64_t   installed_tick;
} Slot;

const PartSpec *catalog(int *count);
int  catalog_find(const char *id);
const char *part_kind_name(PartKind k);

#endif /* NOM_PARTS_H */
