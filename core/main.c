/* main.c — the standalone driver.
 *
 *   nominal --headless --seed 7 --home home/ --out runs/r.json
 *   nominal --serve --port 7777 --home home/
 *
 * Headless exists so that iterating takes seconds instead of minutes, and so
 * that the determinism gate can run on every change. It prints one line of
 * machine-readable JSON to stdout and nothing else. See D1/D3.
 */
#include "nom.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern bool nom_write_file(const char *path, const char *data, size_t len);

static void usage(void)
{
    fprintf(stderr,
        "usage: nominal [--headless|--serve] [options]\n"
        "\n"
        "  --headless            run one encounter and print result JSON (default)\n"
        "  --serve               listen for terminal sessions\n"
        "  --port N              listen port (default 7777)\n"
        "  --seed N              match seed (default 1)\n"
        "  --home PATH           player home directory to load (default ./home)\n"
        "  --script NAME         script under /home/scripts to attach\n"
        "                        (default: every *.nom file, in name order)\n"
        "  --ticks N             tick ceiling (default 1500)\n"
        "  --budget N            instruction budget at full power (default %d)\n"
        "  --out PATH            write the replay JSON here\n"
        "  --log                 print the event log to stderr after the run\n"
        "  --quiet               suppress the banner on --serve\n",
        BUDGET_MAX_DEFAULT);
}

/* Attach every *.nom under /home/scripts, in name order. The vfs keeps
 * insertion order and hostfs loaded it sorted, so this is deterministic. */
static int attach_all(Sim *s)
{
    VNode *dir = vfs_lookup(&s->fs, "/home/scripts");
    if (!dir) return 0;
    int n = 0;
    for (VNode *c = dir->child; c; c = c->next) {
        if (c->kind != VN_FILE) continue;
        size_t l = strlen(c->name);
        if (l < 4 || strcmp(c->name + l - 4, ".nom") != 0) continue;
        char path[NOM_PATH_MAX];
        snprintf(path, sizeof path, "/home/scripts/%s", c->name);
        char err[NOM_ERR_MAX];
        if (sim_attach(s, path, err, sizeof err)) n++;
        else fprintf(stderr, "nominal: %s\n", err);
    }
    return n;
}

int main(int argc, char **argv)
{
    bool serve = false, want_log = false, quiet = false;
    int port = 7777, budget = 0;
    uint64_t seed = 1, ticks = 0;
    const char *home = "home";
    const char *out = NULL;
    const char *one_script = NULL;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if      (strcmp(a, "--serve") == 0)    serve = true;
        else if (strcmp(a, "--headless") == 0) serve = false;
        else if (strcmp(a, "--log") == 0)      want_log = true;
        else if (strcmp(a, "--quiet") == 0)    quiet = true;
        else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) { usage(); return 0; }
        else if (i + 1 < argc && strcmp(a, "--port") == 0)   port = atoi(argv[++i]);
        else if (i + 1 < argc && strcmp(a, "--seed") == 0)   seed = strtoull(argv[++i], NULL, 10);
        else if (i + 1 < argc && strcmp(a, "--ticks") == 0)  ticks = strtoull(argv[++i], NULL, 10);
        else if (i + 1 < argc && strcmp(a, "--budget") == 0) budget = atoi(argv[++i]);
        else if (i + 1 < argc && strcmp(a, "--home") == 0)   home = argv[++i];
        else if (i + 1 < argc && strcmp(a, "--out") == 0)    out = argv[++i];
        else if (i + 1 < argc && strcmp(a, "--script") == 0) one_script = argv[++i];
        else { fprintf(stderr, "nominal: unknown option '%s'\n", a); usage(); return 2; }
    }

    Sim *s = sim_new(seed);
    if (budget > 0) s->budget_max = budget;
    if (ticks > 0)  s->max_ticks = ticks;

    char err[NOM_ERR_MAX];
    if (!sim_load_home(s, home, err, sizeof err)) {
        fprintf(stderr, "nominal: %s\n", err);
        sim_free(s);
        return 2;
    }

    if (serve) {
        if (!quiet) {
            fprintf(stderr, "nominal: home %s, seed %llu\n", home, (unsigned long long)seed);
        }
        int rc = net_serve(s, port, !quiet);
        sim_free(s);
        return rc;
    }

    /* ---- headless ---- */
    int n;
    if (one_script) {
        char path[NOM_PATH_MAX];
        if (one_script[0] == '/') snprintf(path, sizeof path, "%s", one_script);
        else snprintf(path, sizeof path, "/home/scripts/%s", one_script);
        n = sim_attach(s, path, err, sizeof err) ? 1 : 0;
        if (!n) fprintf(stderr, "nominal: %s\n", err);
    } else {
        n = attach_all(s);
    }
    if (n == 0) {
        fprintf(stderr, "nominal: no scripts found under %s/scripts\n", home);
        sim_free(s);
        return 2;
    }

    if (!sim_launch(s, err, sizeof err)) {
        /* A compile error is a normal outcome of playing, not a crash. Report
         * it in the same JSON shape so tooling has one thing to parse. */
        printf("{\"result\":\"error\",\"seed\":%llu,\"error\":\"%s\"}\n",
               (unsigned long long)seed, err);
        sim_free(s);
        return 1;
    }

    sim_run_to_end(s, s->max_ticks + 1);

    if (want_log) {
        for (int i = 0; i < s->nevents; i++)
            fprintf(stderr, "%6llu  %s\n", (unsigned long long)s->event[i].tick, s->event[i].text);
    }

    if (out) {
        Buf r;
        buf_init(&r);
        sim_replay_json(s, &r);
        if (!nom_write_file(out, r.p, r.len))
            fprintf(stderr, "nominal: cannot write %s\n", out);
        buf_free(&r);
    }

    Buf j;
    buf_init(&j);
    sim_result_json(s, &j);
    printf("%s\n", j.p);
    buf_free(&j);

    int rc = (s->run == RUN_WON) ? 0 : 1;
    sim_free(s);
    return rc;
}
