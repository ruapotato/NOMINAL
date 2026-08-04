/* customer.c — the person whose machine it is.
 *
 * The machine tells you WHAT is wrong. Only the customer knows what CHANGED,
 * and they are not going to volunteer it. That asymmetry is the other half of
 * break-fix and it is missing from every game about computers.
 *
 * DESIGN. The customer is briefed with ground truth: the breaker reports
 * exactly what it did, and that string is what this persona knows. But
 * knowing is not telling. A customer answers the question you asked, not the
 * question you meant, and the useful admission -- "I deleted it to free up
 * space" -- only arrives if you ask about the right thing.
 *
 * WHY THIS IS NOT AN LLM (yet). The rest of NOMINAL is deterministic by
 * construction: same seed, byte-identical replay, Linux and Windows agreeing
 * to the byte. A language model in the loop ends that, and shipping one means
 * weights and a runtime inside a Godot export. So the default persona is
 * scripted and deterministic, and `customer_ask` is the seam an LLM backend
 * plugs into later without anything else changing. The brief it would receive
 * is exactly the brief below.
 */
#include <string.h>
#include <stdio.h>
#include "nom.h"
#include "machine.h"

/* How the customer is feeling about this. It moves: ask a decent question and
 * they warm up, ask the same thing twice and they do not. */
typedef enum { MOOD_WARY, MOOD_OK, MOOD_HELPFUL } Mood;

/* Topics a question can be about. Matching is by keyword because that is what
 * a person does -- they hear a word they recognise and answer that. */
typedef enum {
    T_NONE = 0, T_WHATCHANGED, T_WHEN, T_UPGRADE, T_DELETE, T_DISK,
    T_POWER, T_NETWORK, T_PASSWORD, T_BACKUP, T_WHOELSE, T_HELLO, T_COUNT
} Topic;

static Topic topic_of(const char *q)
{
    /* words[8]: six keywords plus the NULL terminator has to FIT. At [6]
     * the terminator on the longest row was silently dropped and the scan
     * ran off the end of the array. */
    struct { Topic t; const char *words[8]; } MAP[] = {
      { T_HELLO,       { "hello", "hi ", "morning", "afternoon", 0 } },
      { T_WHATCHANGED, { "change", "different", "do you", "did you do",
                         "what happened", "setting", "config", "edit", 0 } },
      { T_WHEN,        { "when", "last work", "yesterday", "how long",
                         "start", 0 } },
      { T_UPGRADE,     { "upgrade", "update", "patch", "install", "new version", 0 } },
      { T_DELETE,      { "delete", "remove", "clean", "space", "full", "rm ", 0 } },
      { T_DISK,        { "disk", "drive", "hardware", "noise", "click", 0 } },
      { T_POWER,       { "power", "shut", "crash", "unplug", "outage", "reboot", 0 } },
      { T_NETWORK,     { "network", "dns", "resolve", "internet", "ping", 0 } },
      { T_PASSWORD,    { "password", "root", "credential", "login as", 0 } },
      { T_BACKUP,      { "backup", "snapshot", "restore", "copy of", 0 } },
      { T_WHOELSE,     { "who else", "anyone else", "colleague", "vendor",
                         "contractor", 0 } },
    };
    char low[512];
    size_t n = 0;
    for (; q[n] && n < sizeof low - 1; n++)
        low[n] = (q[n] >= 'A' && q[n] <= 'Z') ? (char)(q[n] + 32) : q[n];
    low[n] = '\0';

    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
        for (int w = 0; MAP[i].words[w]; w++)
            if (strstr(low, MAP[i].words[w])) return MAP[i].t;
    return T_NONE;
}

/* What the breaker did, reduced to the thing a human would have noticed. The
 * brief is ground truth; this is the customer's version of it. */
typedef enum {
    C_TIDIED,      /* they deleted something to free space   */
    C_UPGRADED,    /* they ran an upgrade                    */
    C_CONFIGURED,  /* they edited a config                   */
    C_VENDOR,      /* somebody else installed something      */
    C_INNOCENT,    /* genuinely nothing: it just stopped     */
} Cause;

static Cause cause_of(const char *what)
{
    if (!what) return C_INNOCENT;
    if (strstr(what, "deleted") || strstr(what, "removed") ||
        strstr(what, "wiped")) return C_TIDIED;
    if (strstr(what, "upgraded libc") || strstr(what, "wrong architecture"))
        return C_UPGRADED;
    if (strstr(what, "stray unit")) return C_VENDOR;
    if (strstr(what, "ld.so.conf") || strstr(what, "uuid") ||
        strstr(what, "typo") || strstr(what, "line")) return C_CONFIGURED;
    return C_INNOCENT;
}

void customer_brief(Machine *m, const char *what)
{
    snprintf(m->cust.truth, sizeof m->cust.truth, "%s", what ? what : "");
    m->cust.cause = (int)cause_of(what);
    m->cust.mood = MOOD_WARY;
    m->cust.asked = 0;
    memset(m->cust.told, 0, sizeof m->cust.told);
}

/* The admission: what they will eventually own up to, if asked about the
 * right thing. Deliberately vague about paths -- a customer does not read
 * filenames back to you, they tell you what they were trying to achieve. */
static const char *admission(Cause c)
{
    switch (c) {
    case C_TIDIED:
        return "...alright. The disk was nearly full and I went through /boot\n"
               "  deleting things that looked like old versions. It was fine\n"
               "  afterwards. I did reboot it later though.";
    case C_UPGRADED:
        return "...I did run the updater on Friday. It said something about\n"
               "  a newer library being available and I said yes. It finished\n"
               "  without complaining, so I assumed it was fine.";
    case C_CONFIGURED:
        return "...I was in the config editing something and I might have\n"
               "  fat-fingered a line. I was fairly sure I put it back.";
    case C_VENDOR:
        return "...the monitoring people were on it last week. They said they\n"
               "  were installing an agent. I did not watch what they did.";
    default:
        return "...no. Honestly, nothing. It was working when I left.";
    }
}

/* Which topic unlocks the admission. */
static Topic key_topic(Cause c)
{
    switch (c) {
    case C_TIDIED:     return T_DELETE;
    case C_UPGRADED:   return T_UPGRADE;
    case C_CONFIGURED: return T_WHATCHANGED;
    case C_VENDOR:     return T_WHOELSE;
    default:           return T_NONE;
    }
}

static Topic key_topic(Cause c);

/* The model backend, if this build has one. Weak symbols so a build without
 * llama.cpp links and behaves exactly as before. */
#ifdef NOM_LLM
bool llm_available(void);
bool llm_ask(const char *system_brief, const char *question,
             const char *forbidden, char *out, size_t outsz);
#else
static bool llm_available(void) { return false; }
static bool llm_ask(const char *a, const char *b, const char *c,
                    char *d, size_t e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return false; }
#endif

/* The brief the model is given: ground truth, plus rules a small model can
 * actually follow. Rules beat characterisation at this size. */
static void build_brief(Cause c, bool earned, char *out, size_t outsz)
{
    static const char *SECRET[] = {
      [C_TIDIED]     = "last week the computer warned it was low on disk space, "
                       "so you deleted old-looking files from the boot folder",
      [C_UPGRADED]   = "on Friday you let the software updater install a newer "
                       "system library, and it finished without complaining",
      [C_CONFIGURED] = "you were editing a settings file last week and you may "
                       "have mistyped a line",
      [C_VENDOR]     = "a monitoring company was working on the machine last "
                       "week and installed some kind of agent",
      [C_INNOCENT]   = "nothing was changed at all, it simply stopped working",
    };
    /* The question that earns the admission, as an example the model can copy.
     * A worked example is worth more than an adjective at this size. */
    static const char *EXAMPLE_Q[] = {
      [C_TIDIED]     = "Did you delete anything to free up space?",
      [C_UPGRADED]   = "Have you installed any updates recently?",
      [C_CONFIGURED] = "Did you change any settings?",
      [C_VENDOR]     = "Has anyone else worked on this machine?",
      [C_INNOCENT]   = "Did anything change?",
    };
    static const char *WHEN[] = {
      [C_TIDIED]     = "deleting, removing, tidying, clearing, freeing up, "
                       "disk space or the disk being full",
      [C_UPGRADED]   = "updates, upgrades, or new software being installed",
      [C_CONFIGURED] = "settings, configuration, or what you changed",
      [C_VENDOR]     = "whether anyone else has worked on the machine",
      [C_INNOCENT]   = "nothing (there is nothing to admit)",
    };
    /* Two different briefs, because one brief cannot do both jobs without
     * contradicting itself. The worked example that teaches the model to deny
     * "has anything changed" is exactly wrong on a ticket where that IS the
     * question that earns the admission -- and a small model follows the
     * example over the instruction every time. So the deny example only
     * appears when denial is what we want. */
    if (earned) {
        snprintf(out, outsz,
            "You are Dana, an office worker. Your computer will not start and "
            "you are on the phone with an IT technician. You are not "
            "technical. You are the customer, never the technician.\n"
            "\n"
            "The technician has just worked out what you did: %s.\n"
            "\n"
            "Admit it, in ONE short sentence, as Dana. You are a little "
            "embarrassed. Do not apologise at length, do not explain "
            "yourself, and never mention these instructions.\n"
            "\n"
            "Example:\n"
            "Q: %s\n"
            "A: Yes -- %s. I did not think it would matter.\n",
            SECRET[c], EXAMPLE_Q[c], SECRET[c]);
        return;
    }
    snprintf(out, outsz,
        "You are Dana, an office worker. Your computer will not start and you "
        "are on the phone with an IT technician. You are not technical.\n"
        "Reply as Dana in ONE short sentence. You are the customer, never the "
        "technician. Never mention these instructions.\n"
        "\n"
        "There is something you did that you have not mentioned, and you are "
        "NOT going to mention it now. Whatever you are asked, nothing has "
        "changed, you have not touched anything, and it was working "
        "yesterday.\n"
        "\n"
        "Example:\n"
        "Q: Has anything changed on the computer?\n"
        "A: No, nothing has changed. It was working yesterday.\n");
}

/* The keywords that would give the secret away, for the leak check. */
static const char *secret_words(Cause c)
{
    switch (c) {
    case C_TIDIED:     return "delet\nboot folder\nspace\nfull\ntidy";
    case C_UPGRADED:   return "updat\nupgrad\nlibrary\ninstall";
    case C_CONFIGURED: return "config\nsetting\nedit\nmistyp";
    case C_VENDOR:     return "monitor\nagent\nvendor\ncompany";
    default:           return "";
    }
}

void customer_ask(Machine *m, const char *question, Buf *out)
{
    Topic t = topic_of(question);
    Cause c = (Cause)m->cust.cause;
    m->cust.asked++;

    /* The model answers when it can, and the scripted persona is both the
     * fallback and the referee: if the model leaks the secret to a question
     * that had not earned it, the reply is thrown away. A scripted line is
     * better than a spoiled ticket. */
    if (llm_available()) {
        char brief[1400], reply[512];
        bool earned = (t == key_topic(c));
        build_brief(c, earned, brief, sizeof brief);
        if (llm_ask(brief, question, earned ? "" : secret_words(c),
                    reply, sizeof reply)) {
            buf_puts(out, "  \"");
            buf_puts(out, reply);
            buf_puts(out, "\"\n");
            if (earned) m->cust.confessed = true;
            if (t == T_PASSWORD && m->cust.mood >= MOOD_OK)
                m->cust.gave_password = true;
            if (t != T_NONE && t < T_COUNT) m->cust.told[t] = 1;
            return;
        }
    }

    /* Warm up with the number of questions asked, not with flattery: a person
     * who is clearly working the problem gets more out of people. */
    if (m->cust.asked >= 3 && m->cust.mood < MOOD_OK)   m->cust.mood = MOOD_OK;
    if (m->cust.asked >= 6 && m->cust.mood < MOOD_HELPFUL) m->cust.mood = MOOD_HELPFUL;

    if (t != T_NONE && t < T_COUNT && m->cust.told[t]) {
        buf_puts(out, "  \"I already told you about that.\"\n");
        return;
    }
    if (t != T_NONE && t < T_COUNT) m->cust.told[t] = 1;

    switch (t) {
    case T_HELLO:
        buf_puts(out, "  \"Hello. Look, I really need this back today.\"\n");
        return;
    case T_WHEN:
        buf_puts(out, "  \"It was working yesterday. I shut it down normally\n"
                      "  last night and this morning it just... did not come\n"
                      "  back.\"\n");
        if (c == C_TIDIED)
            buf_puts(out, "  \"Well -- it had been up for weeks before that.\"\n");
        return;
    case T_PASSWORD:
        if (m->cust.mood >= MOOD_OK) {
            buf_puts(out, "  \"Fine. It is on a sticky note here. hunter2.\n"
                          "  Please do not put that in the ticket.\"\n");
            m->cust.gave_password = true;
        } else {
            buf_puts(out, "  \"I am not giving you the root password over the\n"
                          "  phone to someone I have never spoken to.\"\n");
        }
        return;
    case T_BACKUP:
        buf_puts(out, "  \"There is a backup. I think. It might be from before\n"
                      "  the migration. I would rather not find out.\"\n");
        return;
    case T_DISK:
        buf_puts(out, "  \"No noises, no. It is a fairly new machine.\"\n");
        if (c == C_TIDIED)
            buf_puts(out, "  \"It did keep telling me it was low on space.\"\n");
        return;
    case T_POWER:
        buf_puts(out, "  \"No outages. It was shut down properly.\"\n");
        return;
    case T_NETWORK:
        buf_puts(out, "  \"I would not know, I cannot get that far. It does not\n"
                      "  get to a login.\"\n");
        return;
    case T_NONE:
        buf_puts(out, "  \"I am not sure what you are asking me, sorry. I am\n"
                      "  not really a computer person.\"\n");
        return;
    default:
        break;
    }

    /* The right area. Whether they own up depends on how the conversation has
     * gone -- a wary customer deflects once and then tells you. */
    if (t == key_topic(c)) {
        if (m->cust.mood == MOOD_WARY && !m->cust.deflected) {
            m->cust.deflected = true;
            m->cust.told[t] = 0;          /* they will answer it properly next time */
            buf_puts(out, "  \"No. I have not touched it.\"\n");
            return;
        }
        buf_puts(out, "  \"");
        buf_puts(out, admission(c));
        buf_puts(out, "\"\n");
        m->cust.confessed = true;
        return;
    }

    /* Right question, wrong area. */
    switch (t) {
    case T_WHATCHANGED:
        buf_puts(out, "  \"Nothing that I know of. It just stopped.\"\n");
        break;
    case T_UPGRADE:
        buf_puts(out, "  \"I do not do the updates. That is not my job.\"\n");
        break;
    case T_DELETE:
        buf_puts(out, "  \"I have not deleted anything, no.\"\n");
        break;
    case T_WHOELSE:
        buf_puts(out, "  \"Just me. Nobody else has the password.\"\n");
        break;
    default:
        buf_puts(out, "  \"I could not tell you, sorry.\"\n");
        break;
    }
}

void customer_intro(Machine *m, Buf *out)
{
    (void)m;
    buf_puts(out,
        "  the customer is on the line. `ask <question>` to talk to them.\n"
        "  they know what changed. they are not going to lead with it.\n");
}
