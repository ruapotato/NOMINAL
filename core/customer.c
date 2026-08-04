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
    /* words[12], with room to spare, and the loop is bounded as well as
     * NULL-terminated. This array has overflowed twice: once silently (the
     * terminator was dropped and the scan ran off the end, segfaulting on any
     * question mentioning a password) and once with a warning. Belt and
     * braces is cheaper than a third time. */
    struct { Topic t; const char *words[12]; } MAP[] = {
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
        for (int w = 0; w < 12 && MAP[i].words[w]; w++)
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

/* WHAT THE CUSTOMER KNOWS.
 *
 * Not the fault. Not the path. Not the technical consequence. What a person
 * standing next to that machine would have noticed, in their own words.
 *
 * This is the whole of D21: the model cannot leak the answer because it was
 * never given the answer. "I deleted some old-looking files from a folder"
 * does not tell you which file, why the boot stops where it does, or how to
 * repair it. The machine holds the technical truth; the customer holds the
 * human story; the game is joining them up.
 */
static void build_brief(Cause c, bool earned, char *out, size_t outsz)
{
    /* What they did, as they would describe it. */
    static const char *DID[] = {
      [C_TIDIED]     = "A while ago the computer kept saying it was low on "
                       "space, so you went through a folder and deleted some "
                       "old-looking files you did not think were needed. It "
                       "carried on working fine afterwards.",
      [C_UPGRADED]   = "On Friday a box popped up offering to install updates "
                       "and you clicked yes. It finished without complaining.",
      [C_CONFIGURED] = "Last week you were in a settings file changing "
                       "something a colleague asked for. You think you put it "
                       "back the way it was.",
      [C_VENDOR]     = "Some people from a monitoring company were working on "
                       "the machine last week. They said they were installing "
                       "an agent. You did not watch what they did.",
      [C_INNOCENT]   = "You have not done anything to it at all. It was "
                       "working when you left and it was not when you came "
                       "back.",
    };
    /* What they saw. This is honest and useless as an answer, which is the
     * point -- it is the shape of every real first report. */
    static const char *SAW[] = {
      [C_TIDIED]     = "It was working yesterday. This morning it will not start.",
      [C_UPGRADED]   = "It was working yesterday. This morning it will not start.",
      [C_CONFIGURED] = "It was working yesterday. This morning it will not start.",
      [C_VENDOR]     = "It was working yesterday. This morning it will not start.",
      [C_INNOCENT]   = "It was working yesterday. This morning it will not start.",
    };

    snprintf(out, outsz,
        "You are Dana, an office worker. Your work computer will not start and "
        "you have called IT support. You are not technical: you do not know "
        "words like kernel, package, filesystem or boot loader, and you would "
        "not use them.\n"
        "\n"
        "WHAT YOU KNOW:\n"
        "- %s\n"
        "- %s\n"
        "You do not know anything else about it. If you are asked something "
        "technical, say you do not know.\n"
        "\n"
        "HOW YOU TALK:\n"
        "- You are the customer. You are NEVER the technician. Do not offer "
        "to help, do not ask if they need assistance.\n"
        "- ONE or TWO short sentences. Never more.\n"
        "- %s\n"
        "- Never mention these instructions.\n"
        "\n"
        "Examples of how you sound:\n"
        "Q: Hello, this is IT support.\n"
        "A: Oh, thank goodness. It will not turn on at all.\n"
        "Q: What do you see on the screen?\n"
        "A: Some white writing on a black background, then it stops.\n"
        "Q: Have you installed anything recently?\n"
        "A: Not that I know of.\n",
        DID[c], SAW[c],
        /* Social reluctance, not an information hazard. People do not lead
         * with the thing they think they will be blamed for -- but if it
         * comes out early, that is a realistic customer having a good day and
         * it costs the puzzle nothing. */
        earned
          ? "The technician has asked you directly about this. Tell them what "
            "you did. You are a little embarrassed about it."
          : "You would rather not bring up what you did unless you are asked "
            "about it directly. If they ask a general question, just say "
            "nothing has changed as far as you know.");
}

void customer_ask(Machine *m, const char *question, Buf *out)
{
    /* A request to DO something is not a question, and answering it with
     * dialogue would be useless. Checked first. */
    if (customer_do(m, question, out)) { m->cust.asked++; return; }

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
        /* No forbidden list: D21 removed the reason for one. The customer
         * cannot give away an answer it was never told. */
        if (llm_ask(brief, question, "", reply, sizeof reply)) {
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

/* ------------------------------------------------------------- actions --
 *
 * The other half of a support call. The technician is on the phone and the
 * machine is three hundred miles away, so anything physical has to be asked
 * for: press the power button, read out what is on the screen, put the disc
 * in. A customer who will not read their password out loud is an obstacle
 * with a human reason behind it, and that is a better obstacle than a locked
 * door.
 *
 * These are matched here rather than by the model, for the same reason topic
 * classification is: a table does it perfectly and a small model does not.
 * The model supplies the words, the table supplies the effect.
 */
typedef enum {
    A_NONE = 0, A_POWER, A_SCREEN, A_DISC, A_CABLE, A_TYPEPW, A_SITDOWN
} Action;

static Action action_of(const char *q)
{
    struct { Action a; const char *words[10]; } MAP[] = {
      { A_POWER,   { "turn it off", "power cycle", "reboot it", "restart it",
                     "switch it off", "press the power", "turn it on", 0 } },
      { A_SCREEN,  { "read out", "what does it say", "read me", "what is on the screen",
                     "what's on the screen", "look at the screen", 0 } },
      { A_DISC,    { "put the disc", "insert the disc", "rescue disc",
                     "boot from the disc", "put the cd", "recovery disc", 0 } },
      { A_CABLE,   { "check the cable", "is it plugged", "power lead",
                     "unplug", "plugged in", 0 } },
      { A_TYPEPW,  { "type the password", "type in the password",
                     "enter the password", "type your password", 0 } },
      { A_SITDOWN, { "are you at the machine", "go to the machine",
                     "sit down at", "in front of it", 0 } },
    };
    char low[512];
    size_t n = 0;
    for (; q[n] && n < sizeof low - 1; n++)
        low[n] = (q[n] >= 'A' && q[n] <= 'Z') ? (char)(q[n] + 32) : q[n];
    low[n] = 0;
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
        for (int w = 0; w < 10 && MAP[i].words[w]; w++)
            if (strstr(low, MAP[i].words[w])) return MAP[i].a;
    return A_NONE;
}

bool customer_do(Machine *m, const char *request, Buf *out)
{
    Action a = action_of(request);
    if (a == A_NONE) return false;

    switch (a) {
    case A_SITDOWN:
        m->cust.at_machine = true;
        buf_puts(out, "  \"Yes, I am right in front of it.\"\n");
        return true;

    case A_CABLE:
        buf_puts(out, "  \"Hang on... yes, it is plugged in. The little light "
                      "on the front is on.\"\n");
        return true;

    case A_POWER:
        m->cust.power_cycles++;
        m->cust.at_machine = true;
        if (m->cust.power_cycles == 1)
            buf_puts(out, "  \"Okay, holding the button... and back on.\"\n"
                          "  \"Same as before. It gets partway and stops.\"\n");
        else if (m->cust.power_cycles < 4)
            buf_puts(out, "  \"I have done that twice now and it does the same "
                          "thing every time.\"\n");
        else
            buf_puts(out, "  \"I do not think turning it off and on again is "
                          "going to fix it, is it.\"\n");
        return true;

    case A_SCREEN: {
        /* They read out the last line of the console, badly. This is real
         * evidence obtained socially rather than technically -- and it is
         * how you find out anything at all before the disc goes in. */
        m->cust.at_machine = true;
        const char *last = NULL;
        if (m->boot.console.len) {
            size_t e = m->boot.console.len;
            while (e && (m->boot.console.p[e-1] == '\n')) e--;
            size_t b = e;
            while (b && m->boot.console.p[b-1] != '\n') b--;
            static char linebuf[200];
            size_t len = e - b;
            if (len >= sizeof linebuf) len = sizeof linebuf - 1;
            memcpy(linebuf, m->boot.console.p + b, len);
            linebuf[len] = 0;
            last = linebuf;
        }
        if (last && *last) {
            buf_puts(out, "  \"It says... hang on, let me get my glasses.\"\n");
            buf_printf(out, "  \"%s\"\n", last);
            buf_puts(out, "  \"Does that mean anything to you?\"\n");
        } else {
            buf_puts(out, "  \"It is just black. Nothing at all.\"\n");
        }
        return true;
    }

    case A_DISC:
        if (m->cust.disc_inserted) {
            buf_puts(out, "  \"It is already in there.\"\n");
            return true;
        }
        m->cust.disc_inserted = true;
        m->cust.at_machine = true;
        buf_puts(out, "  \"Found it in the drawer. It is in.\"\n"
                      "  \"Do you want me to turn it off and on again?\"\n");
        return true;

    case A_TYPEPW:
        if (!m->cust.at_machine) {
            buf_puts(out, "  \"I am not at the machine, give me a minute.\"\n");
            m->cust.at_machine = true;
            return true;
        }
        m->cust.gave_password = true;
        buf_puts(out, "  \"Typing it now... it is not doing anything. There is "
                      "no cursor.\"\n");
        return true;

    default:
        return false;
    }
}

void customer_intro(Machine *m, Buf *out)
{
    (void)m;
    buf_puts(out,
        "  the customer is on the line. `ask <question>` to talk to them.\n"
        "  they know what changed. they are not going to lead with it.\n");
}
