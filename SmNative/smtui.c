/*
 * smtui.c - menuconfig-style configuration TUI for SoundManager, built on the
 * Universal-TUI engine (Apache-2.0), referenced as the git submodule utui/
 * (https://github.com/ayanami770/Universal-TUI).
 *
 * The C# side describes the whole configuration tree as a UTF-8 "spec" string
 * of KEY=VALUE lines (values must not contain line breaks), this module builds
 * the UtItem tree, runs the interactive editor, and serializes the edited
 * values back into an output buffer using the same line format.
 *
 * Spec keys:
 *   config=<path>                    scratch .config path for the engine
 *   backtitle= / title= / instructions=
 *   group.events= / group.meta= / group.settings=      menu labels
 *   label.file=                      label of the per-event sound file item
 *   meta.<m>.label= / meta.<m>.value=                  m: name author about image
 *   set.<k>.label= / set.<k>.help= / set.<k>.value=0|1 / set.<k>.show=0|1
 *                                    k: patch bgplayer assoc missing convert preferstartup
 *   event.count=N
 *   event.<i>.sym= / .name= / .help= / .enabled=0|1 / .file=
 *
 * Result lines: saved=0|1, meta.<m>=, set.<k>=0|1, event.<i>.enabled=, event.<i>.file=
 *
 * By ayanami770 - (c) 2026 - Available under the CDDL-1.0 license
 */
#ifndef SMNATIVE_BUILD
#define SMNATIVE_BUILD
#endif
#include "smnative.h"
#include "utui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 64
#define NUM_META 4
#define NUM_SET 6

static const char *const META_KEYS[NUM_META] = { "name", "author", "about", "image" };
static const char *const SET_KEYS[NUM_SET] =
    { "patch", "bgplayer", "assoc", "missing", "convert", "preferstartup" };

struct spec_event
{
    char *sym, *name, *help, *file;
    char *filesym;               /* "<sym>_FILE" */
    int enabled;
};

struct spec
{
    char *config, *backtitle, *title, *instructions;
    char *group_events, *group_meta, *group_settings, *label_file;
    char *meta_label[NUM_META], *meta_value[NUM_META];
    char *set_label[NUM_SET], *set_help[NUM_SET];
    int set_value[NUM_SET], set_show[NUM_SET];
    int nevents;
    struct spec_event events[MAX_EVENTS];
    char *storage;               /* mutated copy of the spec text */
};

static void spec_free(struct spec *sp)
{
    int i;
    for (i = 0; i < sp->nevents; i++)
        free(sp->events[i].filesym);
    free(sp->storage);
    memset(sp, 0, sizeof *sp);
}

/* Parse "prefix.<index>.rest" -> index, returns rest or NULL */
static const char *idx_key(const char *key, const char *prefix, int *index)
{
    size_t pl = strlen(prefix);
    char *end;
    long v;
    if (strncmp(key, prefix, pl) != 0 || key[pl] != '.')
        return NULL;
    v = strtol(key + pl + 1, &end, 10);
    if (end == key + pl + 1 || *end != '.' || v < 0 || v >= MAX_EVENTS)
        return NULL;
    *index = (int)v;
    return end + 1;
}

static int name_index(const char *const *names, int n, const char *key, size_t keylen)
{
    int i;
    for (i = 0; i < n; i++)
        if (strlen(names[i]) == keylen && strncmp(names[i], key, keylen) == 0)
            return i;
    return -1;
}

static int spec_parse(const char *text, struct spec *sp)
{
    char *line, *next;
    memset(sp, 0, sizeof *sp);
    sp->storage = _strdup(text);
    if (!sp->storage)
        return SMN_E_MEM;

    for (line = sp->storage; line && *line; line = next)
    {
        char *eq;
        next = strchr(line, '\n');
        if (next) { *next = 0; next++; }
        if (!*line)
            continue;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        {
            const char *key = line;
            char *val = eq + 1;
            int ei;
            const char *rest;

            if (strcmp(key, "config") == 0) sp->config = val;
            else if (strcmp(key, "backtitle") == 0) sp->backtitle = val;
            else if (strcmp(key, "title") == 0) sp->title = val;
            else if (strcmp(key, "instructions") == 0) sp->instructions = val;
            else if (strcmp(key, "group.events") == 0) sp->group_events = val;
            else if (strcmp(key, "group.meta") == 0) sp->group_meta = val;
            else if (strcmp(key, "group.settings") == 0) sp->group_settings = val;
            else if (strcmp(key, "label.file") == 0) sp->label_file = val;
            else if (strcmp(key, "event.count") == 0)
            {
                sp->nevents = atoi(val);
                if (sp->nevents < 0 || sp->nevents > MAX_EVENTS)
                    return SMN_E_TUI_SPEC;
            }
            else if (strncmp(key, "meta.", 5) == 0)
            {
                const char *dot = strchr(key + 5, '.');
                if (dot)
                {
                    int mi = name_index(META_KEYS, NUM_META, key + 5, (size_t)(dot - key - 5));
                    if (mi >= 0 && strcmp(dot + 1, "label") == 0) sp->meta_label[mi] = val;
                    else if (mi >= 0 && strcmp(dot + 1, "value") == 0) sp->meta_value[mi] = val;
                }
            }
            else if (strncmp(key, "set.", 4) == 0)
            {
                const char *dot = strchr(key + 4, '.');
                if (dot)
                {
                    int si = name_index(SET_KEYS, NUM_SET, key + 4, (size_t)(dot - key - 4));
                    if (si >= 0 && strcmp(dot + 1, "label") == 0) sp->set_label[si] = val;
                    else if (si >= 0 && strcmp(dot + 1, "help") == 0) sp->set_help[si] = val;
                    else if (si >= 0 && strcmp(dot + 1, "value") == 0) sp->set_value[si] = atoi(val) ? 1 : 0;
                    else if (si >= 0 && strcmp(dot + 1, "show") == 0) sp->set_show[si] = atoi(val) ? 1 : 0;
                }
            }
            else if ((rest = idx_key(key, "event", &ei)) != NULL)
            {
                struct spec_event *ev = &sp->events[ei];
                if (strcmp(rest, "sym") == 0) ev->sym = val;
                else if (strcmp(rest, "name") == 0) ev->name = val;
                else if (strcmp(rest, "help") == 0) ev->help = val;
                else if (strcmp(rest, "file") == 0) ev->file = val;
                else if (strcmp(rest, "enabled") == 0) ev->enabled = atoi(val) ? 1 : 0;
            }
        }
    }

    if (!sp->config || !sp->title || !sp->backtitle || sp->nevents <= 0)
        return SMN_E_TUI_SPEC;
    {
        int i;
        for (i = 0; i < sp->nevents; i++)
        {
            struct spec_event *ev = &sp->events[i];
            size_t n;
            if (!ev->sym || !ev->name)
                return SMN_E_TUI_SPEC;
            if (!ev->help) ev->help = (char *)"";
            if (!ev->file) ev->file = (char *)"";
            n = strlen(ev->sym) + 6;
            ev->filesym = (char *)malloc(n);
            if (!ev->filesym)
                return SMN_E_MEM;
            snprintf(ev->filesym, n, "%s_FILE", ev->sym);
        }
    }
    return SMN_OK;
}

static void set_str_item(UtItem *it, const char *name, const char *sym,
                         const char *value, const char *help)
{
    memset(it, 0, sizeof *it);
    it->type = UT_TYPE_STR;
    it->name = name;
    it->sym = sym;
    it->help = (help && *help) ? help : NULL;
    snprintf(it->strval, sizeof it->strval, "%s", value ? value : "");
}

static void set_bool_item(UtItem *it, int value, const char *name,
                          const char *sym, const char *help)
{
    memset(it, 0, sizeof *it);
    it->type = UT_TYPE_BOOL;
    it->value = value ? 2 : 0;
    it->name = name;
    it->sym = sym;
    it->help = (help && *help) ? help : NULL;
}

static void set_menu_item(UtItem *it, const char *name, UtItem *kids, int nkids)
{
    memset(it, 0, sizeof *it);
    it->type = UT_TYPE_MENU;
    it->name = name;
    it->children = kids;
    it->nchild = nkids;
}

struct tree
{
    UtItem root;
    UtItem top[3];
    UtItem *events;      /* nevents items */
    UtItem *evfiles;     /* nevents single-child arrays */
    UtItem meta[NUM_META];
    UtItem settings[NUM_SET];
    int nshown_settings;
    int set_index[NUM_SET];  /* settings[] position -> SET_KEYS index */
};

static int tree_build(struct spec *sp, struct tree *tr)
{
    int i;
    memset(tr, 0, sizeof *tr);
    tr->events = (UtItem *)calloc((size_t)sp->nevents, sizeof(UtItem));
    tr->evfiles = (UtItem *)calloc((size_t)sp->nevents, sizeof(UtItem));
    if (!tr->events || !tr->evfiles)
        return SMN_E_MEM;

    for (i = 0; i < sp->nevents; i++)
    {
        struct spec_event *ev = &sp->events[i];
        set_str_item(&tr->evfiles[i], sp->label_file ? sp->label_file : "Sound file",
                     ev->filesym, ev->file, ev->help);
        set_bool_item(&tr->events[i], ev->enabled, ev->name, ev->sym, ev->help);
        tr->events[i].children = &tr->evfiles[i];
        tr->events[i].nchild = 1;
    }

    for (i = 0; i < NUM_META; i++)
        set_str_item(&tr->meta[i], sp->meta_label[i] ? sp->meta_label[i] : META_KEYS[i],
                     NULL, sp->meta_value[i], NULL);

    tr->nshown_settings = 0;
    for (i = 0; i < NUM_SET; i++)
    {
        if (!sp->set_show[i])
            continue;
        set_bool_item(&tr->settings[tr->nshown_settings], sp->set_value[i],
                      sp->set_label[i] ? sp->set_label[i] : SET_KEYS[i],
                      NULL, sp->set_help[i]);
        tr->set_index[tr->nshown_settings] = i;
        tr->nshown_settings++;
    }

    set_menu_item(&tr->top[0], sp->group_events ? sp->group_events : "Sound events",
                  tr->events, sp->nevents);
    set_menu_item(&tr->top[1], sp->group_meta ? sp->group_meta : "Scheme metadata",
                  tr->meta, NUM_META);
    set_menu_item(&tr->top[2], sp->group_settings ? sp->group_settings : "Settings",
                  tr->settings, tr->nshown_settings);
    set_menu_item(&tr->root, sp->title, tr->top, 3);
    return SMN_OK;
}

static void tree_free(struct tree *tr)
{
    free(tr->events);
    free(tr->evfiles);
    memset(tr, 0, sizeof *tr);
}

/* append "key=value\n" to the output buffer */
static int out_kv(char *out, int cap, int *pos, const char *key, const char *value)
{
    int n = snprintf(out + *pos, (size_t)(cap - *pos), "%s=%s\n", key, value ? value : "");
    if (n < 0 || n >= cap - *pos)
        return SMN_E_ARG;
    *pos += n;
    return SMN_OK;
}

static int out_kvi(char *out, int cap, int *pos, const char *key, int value)
{
    char tmp[16];
    snprintf(tmp, sizeof tmp, "%d", value);
    return out_kv(out, cap, pos, key, tmp);
}

static int collect(struct spec *sp, struct tree *tr, int saved, char *out, int cap)
{
    int pos = 0, i, err;
    char key[96];

    if ((err = out_kvi(out, cap, &pos, "saved", saved)) != SMN_OK) return err;
    for (i = 0; i < NUM_META; i++)
    {
        snprintf(key, sizeof key, "meta.%s", META_KEYS[i]);
        if ((err = out_kv(out, cap, &pos, key, tr->meta[i].strval)) != SMN_OK) return err;
    }
    for (i = 0; i < tr->nshown_settings; i++)
    {
        snprintf(key, sizeof key, "set.%s", SET_KEYS[tr->set_index[i]]);
        if ((err = out_kvi(out, cap, &pos, key, tr->settings[i].value ? 1 : 0)) != SMN_OK) return err;
    }
    for (i = 0; i < sp->nevents; i++)
    {
        snprintf(key, sizeof key, "event.%d.enabled", i);
        if ((err = out_kvi(out, cap, &pos, key, tr->events[i].value ? 1 : 0)) != SMN_OK) return err;
        snprintf(key, sizeof key, "event.%d.file", i);
        if ((err = out_kv(out, cap, &pos, key, tr->evfiles[i].strval)) != SMN_OK) return err;
    }
    return SMN_OK;
}

int smtui_run(const char *specUtf8, char *outUtf8, int outCap)
{
    struct spec sp;
    struct tree tr;
    UtApp app;
    int err, saved;

    if (!specUtf8 || !outUtf8 || outCap < 16)
        return SMN_E_ARG;
    outUtf8[0] = 0;

    err = spec_parse(specUtf8, &sp);
    if (err != SMN_OK)
    {
        spec_free(&sp);
        return err;
    }
    err = tree_build(&sp, &tr);
    if (err != SMN_OK)
    {
        tree_free(&tr);
        spec_free(&sp);
        return err;
    }

    memset(&app, 0, sizeof app);
    app.backtitle = sp.backtitle;
    app.title = sp.title;
    app.config_file = sp.config;
    app.sym_prefix = "SM";
    app.instructions = (sp.instructions && *sp.instructions) ? sp.instructions : NULL;
    app.app_name = "soundmanager";
    app.saved_epilogue = NULL;
    app.ascii = -1;

    saved = ut_run(&tr.root, &app);
    if (saved < 0)
    {
        tree_free(&tr);
        spec_free(&sp);
        return SMN_E_TUI_NOTTY;
    }

    err = collect(&sp, &tr, saved, outUtf8, outCap);
    tree_free(&tr);
    spec_free(&sp);
    return err == SMN_OK ? saved : err;
}

/* Headless self test: parse + build + collect roundtrip without running the
 * interactive editor. Returns 0 on success. */
int smtui_selftest(void)
{
    static const char SPEC[] =
        "config=smtui-selftest.config\n"
        "backtitle=SoundManager test\n"
        "title=Test tree\n"
        "group.events=Events\n"
        "group.meta=Meta\n"
        "group.settings=Options\n"
        "label.file=Sound file\n"
        "meta.name.label=Name\n"
        "meta.name.value=\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88\n"   /* テスト */
        "meta.author.label=Author\n"
        "meta.author.value=someone\n"
        "meta.about.label=About\n"
        "meta.about.value=\n"
        "meta.image.label=Image\n"
        "meta.image.value=C:\\img.png\n"
        "set.patch.label=Patch startup sound\n"
        "set.patch.value=1\n"
        "set.patch.show=1\n"
        "set.missing.label=Reset missing sounds\n"
        "set.missing.value=0\n"
        "set.missing.show=1\n"
        "set.bgplayer.show=0\n"
        "event.count=2\n"
        "event.0.sym=STARTUP\n"
        "event.0.name=\xE8\xB5\xB7\xE5\x8B\x95\n"                   /* 起動 */
        "event.0.help=Played at startup.\n"
        "event.0.enabled=1\n"
        "event.0.file=C:\\Users\\\xE9\x9F\xB3\\Startup.wav\n"
        "event.1.sym=SHUTDOWN\n"
        "event.1.name=Shutdown\n"
        "event.1.enabled=0\n"
        "event.1.file=\n";
    struct spec sp;
    struct tree tr;
    char out[4096];
    int pos_unused = 0;
    (void)pos_unused;

    if (spec_parse(SPEC, &sp) != SMN_OK) return 1;
    if (sp.nevents != 2) return 2;
    if (strcmp(sp.events[0].filesym, "STARTUP_FILE") != 0) return 3;
    if (tree_build(&sp, &tr) != SMN_OK) return 4;
    if (tr.root.nchild != 3 || tr.top[0].nchild != 2) return 5;
    if (tr.nshown_settings != 2) return 6;
    if (tr.events[0].value != 2 || tr.events[1].value != 0) return 7;
    if (strcmp(tr.evfiles[0].strval, "C:\\Users\\\xE9\x9F\xB3\\Startup.wav") != 0) return 8;
    if (collect(&sp, &tr, 1, out, sizeof out) != SMN_OK) return 9;
    if (!strstr(out, "saved=1\n")) return 10;
    if (!strstr(out, "meta.name=\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88\n")) return 11;
    if (!strstr(out, "set.patch=1\n")) return 12;
    if (!strstr(out, "set.missing=0\n")) return 13;
    if (strstr(out, "set.bgplayer=") != NULL) return 14;
    if (!strstr(out, "event.0.enabled=1\n")) return 15;
    if (!strstr(out, "event.0.file=C:\\Users\\\xE9\x9F\xB3\\Startup.wav\n")) return 16;
    if (!strstr(out, "event.1.enabled=0\n")) return 17;
    tree_free(&tr);
    spec_free(&sp);
    return 0;
}
