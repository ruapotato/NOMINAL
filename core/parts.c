/* parts.c — the catalog.
 *
 * Every line here is a design decision about what problems have more than one
 * answer. Two CPUs that differ only in price/heat/instructions are not filler:
 * they are the difference between "buy your way out" and "write better code".
 */
#include "nom.h"
#include <string.h>

static const PartSpec CATALOG[] = {
/*  id             name                          kind        price  draw  spec   spec2  heat  mtbf   fw  desc */
{ "reactor-a1", "Ampere A1 fission stack",     K_REACTOR,   0,   0.0, 10.00,  0.0,  0.65,  9000, 3, 0,0, "Salvage. Underpowered and you cannot afford better yet." },
{ "reactor-a2", "Ampere A2 uprate kit",        K_REACTOR, 1500,  0.0,  4.00,  0.0,  0.40, 10000, 1, 0,0, "Not a reactor: a second stack that runs alongside the A1. Cheap, cool, modest." },
{ "reactor-b2", "Bellweather B2 stack",        K_REACTOR, 4200,  0.0,  9.00,  0.0,  0.90,  9000, 2, 0,0, "Half again the output. Half again the waste heat." },
{ "reactor-c1", "Corvid C1 compact",           K_REACTOR, 6800,  0.0,  8.00,  0.0,  0.45, 14000, 1, 0,0, "Less output than a B2, far cooler, and it never breaks." },

{ "batt-s",     "24-cell buffer",              K_BATTERY,   0,   0.0, 40.00,  3.0,  0.00, 20000, 1, 0,0, "Salvage. Enough to prime the reactor once." },
{ "batt-l",     "80-cell buffer",              K_BATTERY, 1500,  0.0, 80.00,  5.0,  0.00, 20000, 1, 0,0, "Ride out a brownout instead of dying in one." },

{ "cpu-mk1",    "Tessel Mk1 flight computer",  K_CPU,       0,   2.0, 2000.0, 1.0,  1.44,  6000, 4, NEEDS_PWR|NEEDS_DATA,2.0, "Salvage. 2000 instructions a tick and it runs hot." },
{ "cpu-mk2",    "Tessel Mk2",                  K_CPU,    2600,   2.0, 3200.0, 0.8,  1.30,  7000, 2, NEEDS_PWR|NEEDS_DATA,2.4, "More instructions per tick for the same power." },
{ "cpu-lp",     "Tessel LP low-power",         K_CPU,    1900,   1.1, 1400.0, 0.5,  0.50, 11000, 1, NEEDS_PWR|NEEDS_DATA,1.2, "Slow, cold, cheap to run. Two of these beat one Mk1." },
{ "cpu-xr",     "Kessler XR",                  K_CPU,    7400,   3.2, 6000.0, 1.2,  3.10,  4000, 1, NEEDS_PWR|NEEDS_DATA,4.0, "Enormous. Will cook the bay if you do not plan for it." },

{ "rad-a",      "Panel radiator",              K_RADIATOR,  0,   1.5,  0.90,  0.0,  0.00,  5000, 2, NEEDS_PWR,0, "Salvage. Fouls often. Purge takes 15 ticks." },
{ "rad-b",      "Folded-fin radiator",         K_RADIATOR,2200,   1.5,  1.60,  0.0,  0.00,  8000, 1, NEEDS_PWR,0, "Same power draw, nearly twice the heat rejected." },
{ "rad-loop",   "Pumped coolant loop",         K_RADIATOR,5100,   2.2,  2.80,  0.0,  0.00,  6000, 1, NEEDS_PWR,0, "Hungry, but it will cool anything you can afford to run." },

{ "sen-a",      "Bench optical sensor",        K_SENSOR,    0,   1.5, 30.00, 12.0,  0.10,  4500, 3, NEEDS_PWR|NEEDS_DATA,1.5, "Salvage. Drifts badly when cold. Needs a warm bay." },
{ "sen-b",      "Gyro-stabilised bench",       K_SENSOR, 3100,   1.5,  8.00,  8.0,  0.10,  7000, 1, NEEDS_PWR|NEEDS_DATA,1.5, "Far less cold drift, and it warms up faster." },
{ "sen-cryo",   "Cryogenic interferometer",    K_SENSOR, 8800,   2.4,  1.00, 20.0,  0.05,  5000, 1, NEEDS_PWR|NEEDS_DATA,2.5, "Wants a COLD bay. Inverts everything you learned." },

{ "thr-a",      "Salvaged ion cluster",        K_THRUSTER,  0,   4.0,  0.12,  0.0,  0.20,  7000, 2, NEEDS_PWR|NEEDS_DATA,0.8, "Salvage. Adequate." },
{ "thr-b",      "Hall-effect cluster",         K_THRUSTER,3400,   4.0,  0.19,  0.0,  0.25,  7000, 1, NEEDS_PWR|NEEDS_DATA,0.8, "Half again the acceleration for the same power." },
{ "thr-eff",    "High-efficiency drive",       K_THRUSTER,4900,   2.4,  0.13,  0.0,  0.12,  9000, 1, NEEDS_PWR|NEEDS_DATA,0.8, "Nearly A1 thrust on little over half the power." },

{ "scrub-a",    "Salvaged CO2 scrubber",       K_SCRUBBER,  0,   1.4,  0.070, 0.0,  0.15,  6000, 2, NEEDS_PWR,0, "Salvage. Just keeps up with one person breathing." },
{ "scrub-b",    "Molecular sieve plant",       K_SCRUBBER,2400,   1.8,  0.160, 0.0,  0.20,  9000, 1, NEEDS_PWR,0, "Comfortable margin, and it does not foul." },

{ "pwr-a",      "Salvaged power rail",         K_PWRBUS,    0,   0.0, 10.00,  6.0,  0.00, 12000, 2, 0,0, "Salvage. 7 MW across four ports. Overload it and everything on it suffers." },
{ "pwr-b",      "Heavy power rail",            K_PWRBUS, 1800,   0.0, 14.00,  6.0,  0.00, 15000, 1, 0,0, "Twice the throughput, six ports." },
{ "data-a",     "Salvaged data spine",         K_DATABUS,   0,   0.0,  8.00,  3.0,  0.00, 10000, 2, 0,0, "Salvage. Three ports. Saturate it and every device on it goes intermittent." },
{ "data-b",     "Wide data spine",             K_DATABUS,1400,   0.0, 14.00,  5.0,  0.00, 13000, 1, 0,0, "Five ports and room to grow." },
};

const PartSpec *catalog(int *count)
{
    *count = (int)(sizeof CATALOG / sizeof CATALOG[0]);
    return CATALOG;
}

int catalog_find(const char *id)
{
    int n = (int)(sizeof CATALOG / sizeof CATALOG[0]);
    for (int i = 0; i < n; i++)
        if (strcmp(CATALOG[i].id, id) == 0) return i;
    return -1;
}

const char *part_kind_name(PartKind k)
{
    switch (k) {
    case K_REACTOR:  return "reactor";
    case K_BATTERY:  return "battery";
    case K_CPU:      return "cpu";
    case K_RADIATOR: return "radiator";
    case K_SENSOR:   return "sensor";
    case K_THRUSTER: return "thruster";
    case K_SCRUBBER: return "scrubber";
    case K_PWRBUS:   return "pwrbus";
    case K_DATABUS:  return "databus";
    default:         return "empty";
    }
}
