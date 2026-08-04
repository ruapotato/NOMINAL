/* hostfs.c — the only place in the core that touches the host filesystem.
 *
 * A home directory is a plain tree of text files: scripts plus a manifest, no
 * binary state and no absolute paths, so `zip -r me.zip home/` is the entire
 * sharing story. See D6. Keeping the host calls in one file also keeps the
 * Windows port contained (D2).
 */
#include "nom.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <direct.h>
#  define make_dir(p) _mkdir(p)
#else
#  define make_dir(p) mkdir((p), 0755)
#endif

static bool read_host_file(const char *path, Buf *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char tmp[4096];
    size_t n;
    while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) buf_put(out, tmp, n);
    fclose(f);
    return true;
}

static bool write_host_file(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    if (len) fwrite(data, 1, len, f);
    fclose(f);
    return true;
}

static void load_dir(Sim *s, const char *hostdir, const char *vfsdir, int depth)
{
    if (depth > 4) return;
    DIR *d = opendir(hostdir);
    if (!d) return;

    /* Directory order from readdir is not stable across filesystems, and a
     * script's attach order is observable. Collect, then sort by name. */
    char names[128][NOM_NAME_MAX];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 128) {
        if (e->d_name[0] == '.') continue;
        size_t nl = strlen(e->d_name);
        if (nl >= NOM_NAME_MAX) continue;      /* names this long are not ours */
        memcpy(names[n], e->d_name, nl + 1);
        n++;
    }
    closedir(d);

    for (int i = 1; i < n; i++) {
        char key[NOM_NAME_MAX];
        snprintf(key, sizeof key, "%s", names[i]);
        int j = i - 1;
        while (j >= 0 && strcmp(names[j], key) > 0) { memcpy(names[j+1], names[j], NOM_NAME_MAX); j--; }
        memcpy(names[j+1], key, NOM_NAME_MAX);
    }

    for (int i = 0; i < n; i++) {
        char hp[NOM_PATH_MAX * 3], vp[NOM_PATH_MAX * 3];
        snprintf(hp, sizeof hp, "%s/%s", hostdir, names[i]);
        snprintf(vp, sizeof vp, "%s/%s", vfsdir, names[i]);
        struct stat st;
        if (stat(hp, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            vfs_mkdir(&s->fs, vp);
            load_dir(s, hp, vp, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            Buf b;
            buf_init(&b);
            if (read_host_file(hp, &b)) vfs_mkfile(&s->fs, vp, b.p ? b.p : "");
            buf_free(&b);
        }
    }
}

bool sim_load_home(Sim *s, const char *path, char *err, size_t errsz)
{
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        snprintf(err, errsz, "%s: not a directory", path);
        return false;
    }
    snprintf(s->home, sizeof s->home, "%s", path);
    vfs_remove(&s->fs, "/home");
    vfs_mkdir(&s->fs, "/home");
    load_dir(s, path, "/home", 0);
    if (!vfs_lookup(&s->fs, "/home/scripts")) vfs_mkdir(&s->fs, "/home/scripts");
    return true;
}

static void save_dir(Sim *s, VNode *n, const char *hostdir)
{
    for (VNode *c = n->child; c; c = c->next) {
        char hp[NOM_PATH_MAX * 2];
        snprintf(hp, sizeof hp, "%s/%s", hostdir, c->name);
        if (c->kind == VN_DIR) {
            make_dir(hp);
            save_dir(s, c, hp);
        } else if (c->kind == VN_FILE) {
            write_host_file(hp, c->data.p ? c->data.p : "", c->data.len);
        }
    }
}

bool sim_save_home(Sim *s, const char *path)
{
    if (!path || !path[0]) return false;
    VNode *home = vfs_lookup(&s->fs, "/home");
    if (!home) return false;
    make_dir(path);
    save_dir(s, home, path);
    return true;
}

/* Used by the headless driver to drop a replay next to the run. */
bool nom_write_file(const char *path, const char *data, size_t len)
{
    return write_host_file(path, data, len);
}

bool nom_read_file(const char *path, Buf *out)
{
    return read_host_file(path, out);
}
