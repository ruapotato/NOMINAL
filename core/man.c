/* man.c — the documentation lives inside the machine.
 *
 * If a player has to leave the game to find out how the game works, the OS is
 * not doing its job. Every page here is reachable from `man` at the prompt and
 * from `cat /man/<topic>` in a script.
 */
#include "nom.h"
#include <string.h>

static const struct { const char *topic; const char *text; } PAGES[] = {
{ "", 
"man <topic>\n"
"\n"
"  intro      what this machine is and what your job is\n"
"  fs         the filesystem: /dev /srv /proc /etc /sys /var /n\n"
"  shell      commands, pipelines, filters\n"
"  scripts    writing and running them\n"
"  lang       the scripting language\n"
"  srv        tenants and heartbeats — read this one\n"
"  power      rails, spines, shedding and priority\n"
"  hardware   slots, the catalog, the replicator, cable\n"
"  faults     what breaks and how to tell what it was\n"
"  remote     ssh, sshfs, bind, mounting other machines\n"
},
{ "intro",
"NOMINAL — you are the sysadmin of a station.\n"
"\n"
"Tenants rent segments. They pay you every tick, but ONLY while something is\n"
"actually looking after them, and that something is your scripts. If nothing\n"
"writes /srv/<tenant>/heartbeat, they pay nothing, and after a while they give\n"
"notice and leave.\n"
"\n"
"Everything else follows from that. You need power to run the computer that\n"
"runs the scripts. Power costs money. More tenants need more power and more\n"
"instructions than you have, so at some point you must choose who gets served.\n"
"\n"
"Start with:   station     ls /srv     ps     man srv\n"
},
{ "fs",
"The filesystem IS the machine. There is no other API.\n"
"\n"
"  /dev      hardware. GENERATED from what is installed — pull a card and its\n"
"            files disappear. /dev/cpu, /dev/bus, /dev/reactor, /dev/alarm...\n"
"  /srv      one directory per tenant. heartbeat, status, and its fields.\n"
"  /proc     one directory per running script: state, steps, wchan, error.\n"
"  /etc      configuration READ EVERY TICK. Edit /etc/cpu0.conf and it takes\n"
"            effect immediately. Also hosts and fstab.\n"
"  /sys      the physical slots, 0..9.\n"
"  /var/log  messages, appended forever.\n"
"  /mnt      the parts catalog and the replicator's receiving bay.\n"
"  /n        where other machines get mounted.\n"
"\n"
"Most files are one value, so `cat` and `get()` are usually all you need.\n"
"`ls /dev/cpu` shows you what a device offers.\n"
},
{ "shell",
"Commands take arguments; responses end with a lone '.'\n"
"\n"
"Pipelines work:\n"
"  cat /var/log/messages | grep SYMPTOM | tail 5\n"
"  station | grep starved\n"
"  ls /srv | wc\n"
"\n"
"Filters: grep <pat>, grep-v <pat>, head [n], tail [n], sort, wc\n"
"\n"
"`help` lists commands. `man <topic>` explains them.\n"
},
{ "scripts",
"A script is a file. Running it is a process.\n"
"\n"
"  put /home/scripts/x.nom    type it in, end with a lone '.'\n"
"  start /home/scripts/x.nom  attach and run it now, as a new pid\n"
"  ps                         what is running and what it waits on\n"
"  cat /proc/2/status         detail for one process\n"
"  restart 2                  recompile from disk and run again — deploy\n"
"  kill 2                     stop it\n"
"\n"
"Every script shares one instruction pool (`cat /dev/cpu/pool`), divided\n"
"between the ones that are awake. A script that never sleeps takes a share\n"
"from every other script AND heats the bay it runs in.\n"
},
{ "lang",
"Python-shaped. Blocks by indentation or braces, both fine.\n"
"\n"
"  get(path)              read one file as a number or string\n"
"  write(path, value)     write one\n"
"  read(path)             raw text\n"
"  ls(path)               list a directory\n"
"  parse(text)            'key value' lines -> dict\n"
"\n"
"Four ways to wait — a trigger is a blocking read:\n"
"  sleep(n)               wake in n ticks\n"
"  waitfor(path, value)   wake when the file reads as that\n"
"  watch(path)            wake when it CHANGES, return the new value\n"
"  read(\"/dev/alarm\")     wake when something breaks\n"
"  read(\"/dev/msg\")       wake when somebody messages you\n"
"\n"
"All four cost one instruction per tick while suspended. A poll loop costs\n"
"hundreds and makes heat.\n"
"\n"
"Also: if/elif/else, while, for..in, def, break, continue, lists, dicts,\n"
"len str int num abs min max round sqrt sin cos atan2 range split strip join\n"
"append keys lookup has print bind mount tick\n"
},
{ "srv",
"THIS IS THE JOB.\n"
"\n"
"Every tenant has /srv/<name>/ :\n"
"  heartbeat   write anything to it to say ops is looking after them\n"
"  status      service, freshness, age, what it needs, what it pays\n"
"\n"
"Miss the heartbeat for 40 ticks and their service starts sliding. Miss it for\n"
"140 and they pay nothing. Stay below their SLA for 520 ticks and they give\n"
"notice; 1150 and they leave and take the rent with them.\n"
"\n"
"The shipped ops daemon is /home/scripts/serve.nom. It walks /srv every ten\n"
"ticks. That is fine with two tenants. It is your problem when there are ten.\n"
},
{ "power",
"Rails carry power, spines carry data. Both have finite capacity and finite\n"
"ports, and everything on one shares it.\n"
"\n"
"When the station cannot serve everyone, it SHEDS BY PRIORITY: the tenants at\n"
"the top of the list are served in full and the ones at the bottom get nothing.\n"
"That is deliberate — a station where everyone is slightly broken is worse than\n"
"one where you chose who to drop.\n"
"\n"
"  station              the list, in priority order\n"
"  priority <seg> <n>   move one up or down. 1 is served first.\n"
"  trace <device>       what it depends on and where the chain breaks\n"
},
{ "hardware",
"Ten slots. `slots` shows what is in them.\n"
"\n"
"  catalog                    what the replicator can print\n"
"  order <part>               print one — costs credits, takes 40 ticks\n"
"  install <part> <slot>      fit it from the receiving bay\n"
"  wire <thing> <switch>      measure, replicate and run a cable\n"
"  measure <a> <b>            price a run first\n"
"  place <thing> <x> <y>      move it — shorter runs cost less cable\n"
"  pull <slot>                remove a card, half the value back\n"
"\n"
"Cards are configured in /etc/<dev>.conf, read every tick:\n"
"  enabled 1     duty 1.00   (duty downclocks: less work, less heat)\n"
},
{ "faults",
"/dev/alarm blocks until something is wrong, then names a SYMPTOM. It does not\n"
"name the cause, because the station does not know it either:\n"
"\n"
"  bearing_unstable   cold optics, a knocked bench, or a starved sensor\n"
"  bay_overheating    fouled radiator, tripped breaker, or your own hot code\n"
"  power_shortfall    a breaker, a worn reactor, or over-subscription\n"
"  hardware_fault     a card is degraded or failed\n"
"  air_falling        the scrubber is not keeping up\n"
"\n"
"Tell them apart by reading: /dev/cpu/bay_temp, /dev/<dev>/state, /dev/bus/*,\n"
"/dev/mission/wear, and `trace <device>`.\n"
"\n"
"A card degrades before it fails. `write /dev/<dev>/ctl reseat` clears a\n"
"degraded one; a failed one needs replacing.\n"
},
{ "remote",
"Other machines are real machines.\n"
"\n"
"  hosts                     what is reachable (also /etc/hosts)\n"
"  ssh <host>                log in — your whole view moves there\n"
"  logout                    come back\n"
"  sshfs <host> <path>       mount it instead, for when ssh stops scaling\n"
"  bind <host|path> <path>   graft it wherever you want it\n"
"  mount-all                 bring up everything in /etc/fstab\n"
"\n"
"After binding, a script that only knows /dev/scrubber never learns that it is\n"
"really a unit on somebody else's wreck. That is the whole point of a\n"
"namespace, and it is why a shim is worth writing.\n"
},
};

const char *nom_man(const char *topic)
{
    int n = (int)(sizeof PAGES / sizeof PAGES[0]);
    for (int i = 0; i < n; i++)
        if (strcmp(PAGES[i].topic, topic ? topic : "") == 0) return PAGES[i].text;
    return "no such page. `man` on its own lists them.\n";
}
