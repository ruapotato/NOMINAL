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
#include "kernel.h"

/* WHO THEY ARE, as opposed to what they know.
 *
 * The cause tells you what happened to the machine; this tells you what it is
 * like to be on the phone with the person it happened to. Drawn per ticket
 * from the seed, so the same fault twice is not the same call twice -- which
 * matters more than it sounds, because a support call IS the person, and a
 * hundred faults answered by one voice is one conversation with a hundred
 * skins on it.
 *
 * None of these touch what the customer knows. A grumpy customer and a
 * cheerful one hold exactly the same facts and give them up on exactly the
 * same question; the difference is entirely in the telling. That is
 * deliberate: personality must not make a ticket harder or easier, only
 * different.
 */
/* A NAME PER PERSONA, and the pairing never moves.
 *
 * David: "The personas should all be tied to names so that if you get a
 * particular name, the persona's the same. That way you can kind of strike in
 * a feeling of I really don't like customer X or customer Y is really
 * pleasant."
 *
 * That only works if the binding is stable, so the arrays are parallel and
 * index i is always the same person: Marcus is always in a hurry, Eileen is
 * always apologetic. Over a few dozen tickets you start to recognise them
 * before they have finished their first sentence, which is exactly how it
 * feels to work a support queue.
 */
static const char *CUSTNAME[] = {
  "Dana",
  "Marcus",
  "Priya",
  "Eileen",
  "Tomasz",
  "Nadia",
  "Colin",
  "Beatriz",
  "Ravi",
  "Hilde",
  "Otto",
  "Saoirse",
  "Gareth",
  "Anouk",
  "Yusuf",
  "Marta",
  "Declan",
  "Ingrid",
  "Femi",
  "Lucia",
  "Bjorn",
  "Rosa",
  "Malik",
  "Greta",
  "Sanjay",
  "Elke",
  "Rory",
  "Chiara",
  "Idris",
  "Annika",
  "Pavel",
  "Noor",
  "Duncan",
  "Sofia",
  "Kwame",
  "Lena",
  "Angus",
  "Valentina",
  "Tariq",
  "Karin",
  "Emeka",
  "Milena",
  "Fergus",
  "Aiko",
  "Hassan",
  "Bridget",
  "Nikolai",
  "Camila",
  "Ade",
  "Solveig",
  "Rhys",
  "Zofia",
  "Kofi",
  "Marion",
  "Vikram",
  "Astrid",
  "Callum",
  "Renata",
  "Bilal",
  "Ilse",
  "Eamon",
  "Paloma",
  "Ismail",
  "Ulrike",
  "Niamh",
  "Dmitri",
  "Aisha",
  "Struan",
  "Carmen",
  "Levi",
  "Freya",
  "Osman",
  "Roisin",
  "Piotr",
  "Yara",
  "Hamish",
  "Ines",
  "Zaid",
  "Britta",
  "Tadhg",
  "Luz",
  "Arjun",
  "Signe",
  "Padraig",
  "Ewa",
  "Nabil",
  "Fiona",
  "Andrei",
  "Rania",
  "Iona",
  "Sami",
  "Verena",
  "Cormac",
  "Delphine",
  "Emre",
  "Maeve",
  "Janusz",
  "Adaeze",
  "Ruaridh",
  "Katja",
  "Faisal",
  "Lorna",
  "Bo",
};

static const char *PERSONA[] = {
  "grumpy, and you would rather be using Windows; you say so",
  "cheerful and chatty, and you wander off the subject",
  "in a hurry, with a meeting in ten minutes you keep mentioning",
  "apologetic about everything, including things that are not your fault",
  "convinced this has happened before and nobody fixed it properly",
  "suspicious that IT changed something without telling you",
  "very polite and a little formal, as though writing a letter",
  "blunt to the point of rudeness, but not unkind",
  "nervous that you are going to be blamed for this",
  "certain the machine is simply too old and should be replaced",
  "distracted; someone keeps talking to you while you are on the phone",
  "proud of being 'quite good with computers', and you are not",
  "worried about a deadline today that needs this machine",
  "resigned, as though nothing ever works and this is normal",
  "bewildered by all of it, and you say so often",
  "trying to be helpful by guessing at what might be wrong",
  "annoyed at having been on hold before you got through",
  "relieved that a person has finally answered",
  "frosty, because the last technician was rude to you",
  "delighted to have someone to talk to; it has been a quiet week",
  "impatient, and you interrupt with 'yes, but' a lot",
  "hard of hearing, so you ask them to repeat things",
  "on a bad line, and you keep saying you cannot hear well",
  "eating your lunch while you talk",
  "new here, and you keep saying you only started last month",
  "the longest-serving person in the building, and you mention it",
  "sceptical that anything can be done remotely",
  "fascinated, and you keep asking what the technician is doing",
  "terse; you answer in as few words as possible",
  "rambling; you give far more context than anyone needs",
  "convinced it is a virus, and you keep coming back to that",
  "sure it is the network, because it is always the network",
  "embarrassed that you had to call at all",
  "under the impression the technician is a supplier you are paying",
  "very tired, and you say you were up half the night",
  "cold, because the heating is off and you mention it twice",
  "cheerfully fatalistic: 'ah well, these things happen'",
  "protective of the machine, as though it were a pet",
  "keen to get off the phone and let you get on with it",
  "keen to stay on the phone and watch everything happen",
  "a smoker, and you keep stepping outside mid-sentence",
  "a manager, and you mention that you are a manager",
  "not a manager, and slightly bitter about your manager",
  "recently transferred from another site, and comparing everything to it",
  "worried about the cost, and you ask whether this will be charged",
  "worried about the data, and you ask about it repeatedly",
  "convinced somebody else has been using your machine",
  "sure you did nothing at all, in a way that invites doubt",
  "positive that you turned it off properly on Friday",
  "vague about dates; 'a while ago' is as precise as you get",
  "precise about dates to an unhelpful degree",
  "using a colleague's phone because yours is charging",
  "typing on another machine while you talk",
  "reading the screen out loud whether asked or not",
  "reluctant to touch anything in case you make it worse",
  "far too willing to touch things, and you keep doing so",
  "the sort who has written down every password on a card",
  "the sort who cannot remember any password at all",
  "in an open-plan office and self-conscious about being overheard",
  "alone in the building, and slightly spooked by it",
  "half-listening, because you are also watching the door",
  "cheerful but hopeless with words for things",
  "fond of calling everything 'the system'",
  "fond of calling everything 'the hard drive'",
  "convinced the screen and the computer are the same object",
  "insistent that you have 'done nothing different'",
  "insistent that you have 'tried everything'",
  "helpful in a way that is actively misleading",
  "quietly competent, and you answer well when asked well",
  "flustered, and you get the order of events wrong",
  "confident and wrong about what happened",
  "unwilling to admit you do not understand a question",
  "quick to say when you do not understand a question",
  "of the view that it was fine until the update",
  "of the view that updates are always the problem",
  "of the view that it is probably the cleaner unplugging things",
  "amused by the whole situation",
  "not remotely amused by the whole situation",
  "distracted by a dog that you mention several times",
  "distracted by a child in the background",
  "carefully writing down everything the technician says",
  "not writing anything down and asking twice",
  "a person who says 'obviously' about things that are not",
  "a person who apologises for the machine's behaviour",
  "convinced you are the only one having this problem",
  "convinced everyone is having this problem",
  "sure a colleague had exactly this last month",
  "wary of being talked down to, and quick to notice it",
  "grateful for anything explained plainly",
  "hostile to jargon and you say so",
  "keen on jargon you do not understand and use wrongly",
  "the sort who calls the machine 'she'",
  "brisk and businesslike, treating this as a transaction",
  "warm and personal, asking how the technician's day is",
  "prone to long pauses that the technician has to fill",
  "prone to answering before the question is finished",
  "someone who was about to go on holiday and now cannot",
  "someone who has just come back from holiday to this",
  "put out that this is not somebody else's job",
  "willing to do anything asked, competently and slowly",
  "willing to do anything asked, badly and fast",
  "convinced that turning it off and on again fixes everything",
  "convinced that turning it off and on again breaks everything",
};
#define NPERSONA ((int)(sizeof PERSONA / sizeof PERSONA[0]))

/* How the customer is feeling about this. It moves: ask a decent question and
 * they warm up, ask the same thing twice and they do not. */
typedef enum { MOOD_WARY, MOOD_OK, MOOD_HELPFUL } Mood;

/* Topics a question can be about. Matching is by keyword because that is what
 * a person does -- they hear a word they recognise and answer that. */
typedef enum {
    T_NONE = 0, T_WHATCHANGED, T_WHEN, T_UPGRADE, T_DELETE, T_DISK,
    T_POWER, T_NETWORK, T_PASSWORD, T_BACKUP, T_WHOELSE, T_HELLO,
    T_SCREEN, T_COUNT
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
      /* THE QUESTION THE HELP TEXT ITSELF SUGGESTS, and it had no topic of
       * its own -- so "what do you see on the screen" was captured by the
       * generic "do you" and answered as though it were "what changed". The
       * follow-up then got "I already told you about that", about something
       * they had never been asked. */
      { T_SCREEN,      { "on the screen", "screen say", "screen show",
                         "on the monitor", "on the display", "showing",
                         "screen", "monitor", "display", 0 } },
      /* "do you" was in here and it is not a topic, it is how half of all
       * questions begin. It captured "do you have a backup", "do you know the
       * root password" and "what do you see on the screen". */
      { T_WHATCHANGED, { "change", "different", "did you do",
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

    /* LONGEST match wins, not first-in-table. First-in-table made the answer
     * depend on the order I happened to write the rows in, and "do you" -- a
     * T_WHATCHANGED keyword sitting near the top -- swallowed "do you have a
     * backup", "do you know the root password", and anything else phrased as
     * a plain question. The customer then answered a question nobody asked,
     * which reads as a non-sequitur and was reported as exactly that.
     * Specificity is the right tie-break and it does not care about order. */
    Topic best = T_NONE;
    size_t bestlen = 0;
    for (size_t i = 0; i < sizeof MAP / sizeof MAP[0]; i++)
        for (int w = 0; w < 12 && MAP[i].words[w]; w++) {
            size_t wl = strlen(MAP[i].words[w]);
            if (wl > bestlen && strstr(low, MAP[i].words[w])) {
                best = MAP[i].t;
                bestlen = wl;
            }
        }
    return best;
}

/* What the breaker did, reduced to the thing a human would have noticed. The
 * brief is ground truth; this is the customer's version of it. */
typedef enum {
    C_TIDIED,      /* they deleted something to free space   */
    C_UPGRADED,    /* they ran an upgrade                    */
    C_CONFIGURED,  /* they edited a config                   */
    C_VENDOR,      /* somebody else installed something      */
    C_POWERCUT,    /* it lost power while it was running     */
    C_INNOCENT,    /* genuinely nothing: it just stopped     */
} Cause;

/* Every fault the breaker can produce maps to something a PERSON would have
 * noticed. This matters more than it looks: a playtester reported that the
 * customer "never once volunteered a fact that helped me find the fault", and
 * the reason was that most breaks fell through to C_INNOCENT -- a customer
 * who genuinely knows nothing is realistic exactly once and useless after
 * that. If a fault has no plausible human story, it should not be reaching
 * this function; add one here rather than shrugging. */
static Cause cause_of(const char *what)
{
    if (!what || !*what) return C_INNOCENT;

    /* somebody else was on the machine */
    if (strstr(what, "stray unit") || strstr(what, "vendor"))
        return C_VENDOR;

    /* an upgrade, in any of its forms */
    if (strstr(what, "upgraded") || strstr(what, "upgrade") ||
        strstr(what, "testing") || strstr(what, "architecture") ||
        strstr(what, "libc"))
        return C_UPGRADED;

    /* the power went, and something was mid-write */
    if (strstr(what, "unclean shutdown") || strstr(what, "dirty") ||
        strstr(what, "boot sector"))
        return C_POWERCUT;

    /* they were in a config file */
    if (strstr(what, "ld.so.conf") || strstr(what, "uuid") ||
        strstr(what, "fstab") || strstr(what, "noauto") ||
        strstr(what, "login shell") || strstr(what, "passwd") ||
        strstr(what, "typo") || strstr(what, "line") ||
        strstr(what, "repo") || strstr(what, "channel"))
        return C_CONFIGURED;

    /* they were tidying up */
    if (strstr(what, "deleted") || strstr(what, "removed") ||
        strstr(what, "wiped") || strstr(what, "truncated"))
        return C_TIDIED;

    /* corruption with no human cause: the honest answer is a power cut,
     * because that is what actually corrupts a file on a real machine */
    if (strstr(what, "corrupted") || strstr(what, "nulled") ||
        strstr(what, "smashed"))
        return C_POWERCUT;

    return C_INNOCENT;
}

void customer_brief(Machine *m, const char *what)
{
    snprintf(m->cust.truth, sizeof m->cust.truth, "%s", what ? what : "");
    m->cust.cause = (int)cause_of(what);
    /* Drawn from the truth string, so it varies per ticket without needing a
     * seed threaded down here. */
    {
        unsigned long h = 5381;
        for (const char *q = m->cust.truth; *q; q++) h = h * 33u + (unsigned char)*q;
        m->cust.persona = (int)(h % (unsigned long)NPERSONA);
    }
    m->cust.nturns = 0;
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
    case C_POWERCUT:
        return "...there was a power cut on Tuesday. It was on at the time.\n"
               "  It came back up fine though, or I thought it did.";
    default:
        return "...no. Honestly, nothing. It was working when I left.";
    }
}

/* Which topic unlocks the admission. */
/* Several questions can earn the admission, not one.
 *
 * A playtester asked "did you install anything?" on a ticket where a vendor
 * had installed something, and was told no -- because the only unlocking
 * topic was "has anyone else worked on this". That is not a customer being
 * coy, that is a keyword table being brittle, and it is why the customer was
 * reported as never once helping find a fault. */
static bool unlocks(Cause c, Topic t)
{
    switch (c) {
    /* "What has changed recently?" is THE question a technician opens with,
     * and C_TIDIED -- the commonest cause in the game -- was the single cause
     * that denied it. Every other cause unlocked on it. A playtester asked it
     * on ticket after ticket, got "Not that I remember" every time, and
     * concluded the customer was decoration. They were right about the
     * symptom and this was the cause. */
    case C_TIDIED:     return t == T_DELETE || t == T_DISK ||
                              t == T_WHATCHANGED;
    case C_UPGRADED:   return t == T_UPGRADE || t == T_WHATCHANGED;
    case C_CONFIGURED: return t == T_WHATCHANGED || t == T_UPGRADE;
    case C_VENDOR:     return t == T_WHOELSE || t == T_UPGRADE ||
                              t == T_WHATCHANGED;
    case C_POWERCUT:   return t == T_POWER || t == T_WHATCHANGED;
    default:           return false;
    }
}


/* The model backend, if this build has one. Weak symbols so a build without
 * llama.cpp links and behaves exactly as before. */
#ifdef NOM_LLM
bool llm_available(void);
bool llm_ask(const char *system_brief, const char *question,
             const char *forbidden, char *out, size_t outsz);
bool llm_ask_hist(const char *system_brief, const char **hist, int nhist,
                  const char *question, char *out, size_t outsz);
bool llm_ask_long(const char *system_brief, const char **hist, int nhist,
                  const char *question, char *out, size_t outsz);
bool llm_classify(const char *system_brief, const char *question,
                  char *out, size_t outsz);
#else
static bool llm_available(void) { return false; }
static bool llm_ask(const char *a, const char *b, const char *c,
                    char *d, size_t e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return false; }
static bool llm_ask_hist(const char *a, const char **b, int c,
                         const char *d, char *e, size_t f)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return false; }
static bool llm_ask_long(const char *a, const char **b, int c,
                         const char *d, char *e, size_t f)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return false; }
static bool llm_classify(const char *a, const char *b, char *c, size_t d)
{ (void)a; (void)b; (void)c; (void)d; return false; }
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
/* Would this customer bring it up on their own?
 *
 * People volunteer what was done TO them and go quiet about what they did
 * themselves. A power cut is the building's fault and a contractor is
 * somebody else's, so both come out in the first minute. Deleting files to
 * free space is theirs, and that waits until asked.
 *
 * This is the difference between a customer who is atmosphere and one who is
 * a source: the volunteered facts are real leads, offered early, for free. */
static bool volunteers(Cause c)
{
    return c == C_POWERCUT || c == C_VENDOR;
}


/* WHAT THEY ACTUALLY SEE, from the machine rather than from a table.
 *
 * Every variant of this used to say "it will not start" -- including on the
 * machines that boot to a login prompt with a service quietly dead. A
 * playtester had the customer insist the computer would not start while
 * `boot` returned [UP at target] with all eleven services running, and kept
 * insisting after the box was repaired. A customer who contradicts the
 * machine is worse than no customer: they are a source of false evidence.
 *
 * A person does not say "httpd is dead". They say the website is down, or
 * that it takes their password and then nothing happens. */
static const char *saw_of(Machine *m)
{
    if (!m) return "It was working yesterday. This morning it will not start.";
    if (!m->boot.running) {
        if (m->boot.failed_at <= BOOT_INITRD)
            return "It was working yesterday. This morning it will not start "
                   "at all -- it never gets as far as asking me to log in.";
        return "It starts to come up, there is some white writing on a black "
               "background, and then it stops. It never asks me to log in.";
    }
    Buf sick = {0};
    int dead = kernel_health(m, &sick);
    buf_free(&sick);
    if (dead > 0)
        return "It does start, and it asks me to log in, so I thought it was "
               "fine. But things are not working right -- some of what I use "
               "it for just is not there any more.";
    return "It seems to be working now, actually. It was not this morning.";
}

static void build_brief(Machine *m, Cause c, bool earned, char *out, size_t outsz)
{
    /* What they did, as they would describe it. */
    static const char *DID[] = {
      [C_POWERCUT]   = "There was a power cut in the building on Tuesday "
                       "afternoon. The machine was on at the time and it went "
                       "off with everything else. It seemed fine when it came "
                       "back, or you thought it was.",
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

    snprintf(out, outsz,
        "You are %s, an office worker. Your work computer is not working "
        "properly and you have called IT support. You are not technical: you "
        "do not know words like kernel, package, filesystem or boot loader, "
        "and you would not use them.\n"
        "\n"
        "YOUR MANNER: you are %s. Let that colour how you say things. It does "
        "NOT change what you know or what you are willing to say.\n"
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
        /* The examples show TONE, never content. They used to answer "what
         * do you see on the screen" with a specific screen, and a small model
         * copies an example ahead of following an instruction -- so the
         * customer described a dead boot on a machine that was sitting at a
         * login prompt, and went on doing it after the box was repaired.
         * WHAT YOU SEE above is the truth; the examples must not compete
         * with it. */
        "Examples of how you SOUND (never copy their content -- describe what\n"
        "you actually see, from WHAT YOU SEE above):\n"
        "Q: Hello, this is IT support.\n"
        "A: Oh, thank goodness. Thanks for calling me back.\n"
        "Q: Have you moved the machine recently?\n"
        "A: No, it has been under the same desk for years.\n"
        "%s",
        CUSTNAME[m ? m->cust.persona % NPERSONA : 0],
        PERSONA[m ? m->cust.persona % NPERSONA : 0], DID[c], saw_of(m),
        /* Social reluctance, not an information hazard. People do not lead
         * with the thing they think they will be blamed for -- but if it
         * comes out early, that is a realistic customer having a good day and
         * it costs the puzzle nothing. */
        /* An example of volunteering, for the customers who would. Showing it
         * works and telling it does not -- the instruction alone produced
         * "Yesterday." every time. */
        volunteers(c)
          ? "Q: When did it last work?\n"
            "A: It was fine yesterday. We did have that power cut on Tuesday, "
            "mind you.\n"
          : "Q: Have you installed anything recently?\n"
            "A: Not that I know of.\n",
        earned
          ? "The technician has asked you directly about this. Tell them what "
            "happened. You are a little embarrassed about it."
          : volunteers(c)
            ? "This was not your fault and you will happily mention it if the "
              "conversation gives you any excuse -- especially if you are "
              "asked when it last worked or what has been going on."
            : "You would rather not bring up what you did unless you are "
              "asked about it directly. If they ask a general question, just "
              "say nothing has changed as far as you know.");
}

/* Who is on the phone. Stable for a given persona, so the same name is
 * always the same person. */
const char *customer_name(const Machine *m)
{
    return CUSTNAME[m ? m->cust.persona % NPERSONA : 0];
}


/* ------------------------------------------------------- the other two --
 *
 * David wants a chat app with three contacts, not one: the customer, a
 * COWORKER who knows only what you tell them, and a MANAGER who has the whole
 * architecture and is the most knowledgeable thing you can talk to.
 *
 * The three differ in exactly one thing, and it is the interesting thing:
 * WHAT THEY ARE TOLD.
 *
 *   customer  what a non-technical person at the machine can see. Never the
 *             fault, never a path -- D21, and it is why nothing can leak.
 *   coworker  nothing at all about this machine. They are a competent
 *             technician on the next desk who has not seen it, so they can
 *             only reason about what YOU describe. Useful for exactly the
 *             thing a colleague is useful for: you explain it out loud and
 *             they ask the obvious question you skipped.
 *   manager   how the SYSTEM works -- the boot chain, the tools, where things
 *             live. Not what is wrong with this machine. They are the person
 *             who wrote the runbook, not the person looking at the box.
 *
 * The manager knowing the architecture and not the answer is the whole
 * design. Give them the fault and the game is over; give them the shape of
 * the system and they are a senior colleague worth asking.
 */
static const char *COWORKER_BRIEF =
"You are Ben, a support technician at the next desk. You are competent, "
"friendly, and slightly overworked.\n"
"\n"
"CRITICAL: you have NOT seen this machine. You have no access to it, no logs "
"from it, and no idea what the fault is. You know ONLY what your colleague "
"has just told you in this conversation.\n"
"\n"
"So: reason out loud about what they describe, ask the obvious question they "
"might have skipped, suggest what you would check next. If they have not told "
"you enough, say so and ask for the specific thing you would need -- what the "
"boot log said, what `pkg verify` printed, which service is down.\n"
"\n"
"Never invent a detail about their machine. If you catch yourself about to "
"say what the fault is, stop and ask them for evidence instead.\n"
"\n"
"Two or three sentences. Talk like a colleague, not a manual.\n";

static const char *MANAGER_BRIEF =
"You are Json, the senior engineer who wrote this system's runbook. Your "
"colleague is working a break-fix ticket and has come to you for advice.\n"
"\n"
"YOU KNOW HOW THE SYSTEM WORKS. You do NOT know what is wrong with their "
"particular machine -- you have not seen it. Explain the architecture, tell "
"them where to look, tell them which tool answers which question.\n"
"\n"
"THE MACHINE, as you know it:\n"
"- It is NomnixOS on a small rv64 box. Boot order: firmware (zbios) -> boot "
"loader (zbl, config /boot/zbl/zbl.cfg) -> kernel /boot/vmnomuz (a SYMLINK to "
"the versioned image) -> initrd, which finds the root by UUID and mounts it "
"-> /sbin/init -> /bin/rc /etc/rc.boot -> /sbin/mountall reads /etc/fstab -> "
"/etc/rc.d/rc.3 -> /sbin/svcinit starts the units in /etc/services.d -> "
"/sbin/getty and login.\n"
"- Services are .svc unit files: name, exec, enabled, runlevel, after, "
"critical, restart. `svc` lists them, `svc status <name>` says why one is "
"unhappy, `svc disable <name>` turns one off.\n"
"- Everything is packaged. `pkg verify` compares files to what the package "
"shipped, `pkg owns <path>` says who owns a file, `pkg diff <path>` shows "
"what changed, `pkg reinstall` restores but LEAVES edited config alone unless "
"you add --force (which writes a .pkgsave first). `pkg --root /mnt` works on "
"a mounted disk without chrooting -- essential when the disk's own libc is "
"broken.\n"
"- Binaries declare the libraries and versions they need; `ldd` shows them, "
"resolved through /etc/ld.so.conf. A newer library satisfies an older "
"requirement; an older one does not.\n"
"- The boot log is /var/log/boot.log, previous boot /var/log/boot.log.1, read "
"with `dmesg` (and `dmesg -r /mnt` from the rescue medium). READ IT FIRST: it "
"tells you which layer failed.\n"
"- A filesystem runs out of bytes and inodes independently: `df` and `df -i`.\n"
"- The rescue medium is /dev/sr0 and is never damaged. `rescue`, then "
"`mount /dev/sda1 /mnt`.\n"
"\n"
"THE COMPLETE COMMAND VOCABULARY. There is nothing else. If a command you\n"
"are about to name is not on this list, it does not exist:\n"
"  dmesg [-1] [-f text] [-r root]   svc | svc status|enable|disable <name>\n"
"  pkg list|owns|verify|diff|reinstall [--force] [--root DIR]\n"
"  ldd [-r root] <prog>   df [-i]   blkid   mount   umount   fsck <dev>\n"
"  ls  cat  stat  chmod  cp  mv  rm [-r]  touch  grep  sed  head  wc  echo\n"
"  find <dir> [-name pat] [-type f|d]     netstat\n"
"  ps  ns  kill  chroot  man  links  mkinitrd  zbl-mkconfig  zbl-install\n"
"  rcon connect|status|console|power|media|boot\n"
"The shell has pipes, quoting, && || and `for i in a b; do ... done`, and\n"
"globbing with * and ?.\n"
"\n"
"WHAT A BROKEN UNIT FILE MEANS. A .svc file with no `exec` line is not\n"
"disabled -- it is INVALID, and svcinit stops the boot on it. `enabled: no`\n"
"is what disabled means. A unit whose file is corrupt behaves as though its\n"
"fields are missing, so the first thing to check is whether it is readable\n"
"text at all.\n"
"\n"
"THINGS THIS MACHINE DOES NOT HAVE, which you must never suggest:\n"
"  no locate, no which -- `find <dir> -name <pattern>` is there though\n"
"  no editor at all: no vi, no nano, no ed. Files are changed with\n"
"     `sed -i s/old/new/ <file>`, `sed -i /text/d <file>` and `echo x >> f`\n"
"  no ss, ifconfig or ip -- but `netstat` is there, and the network is\n"
"     /etc/net/interfaces and `svc status net`\n"
"  no systemctl or journalctl -- services are `svc`, logs are `dmesg`\n"
"  no apt, dpkg, rpm or yum -- packages are `pkg`\n"
"  no tail, less, more, du, top, lsof, awk, curl or tar\n"
"\n"
"NEVER INVENT ANYTHING. Do not name a command that is not on the list above.\n"
"Do not describe behaviour you are unsure of. You wrote the runbook, so being\n"
"wrong is worse for them than being unhelpful: if you do not know, say you do\n"
"not know and tell them which file or which command would settle it. A guess\n"
"in your voice sounds like documentation.\n"
"\n"
"Answer the question they actually asked, concretely, naming the command you "
"would run. Three or four sentences at most. If they have not said enough for "
"you to be useful, tell them what to go and look at.\n";

/* DOES THIS REPLY NAME A COMMAND THAT EXISTS?
 *
 * A playtester: "the runbook author hallucinating commands is the single
 * worst thing here -- it is the one character whose job is to be
 * authoritative." Told to check open connections he reached for netstat;
 * told to search the disk, find; told to edit a file, vi. None of them exist.
 *
 * The brief tells him so, and prompting alone got 5/12 to 10/12 -- better,
 * and still not something to rely on, because netstat and find are burned
 * into a model far more deeply than any brief. So the rule is enforced in
 * code, the same way D21 stopped relying on the model to keep a secret: a
 * reply that names a command this machine does not have is thrown away.
 *
 * Only backticked words are checked. "the service is down" is English; the
 * model writes commands as `cmd`. */
static bool names_only_real_commands(const char *txt)
{
    static const char *REAL[] = {
        "dmesg","svc","pkg","ldd","df","blkid","mount","umount","fsck","ls",
        "cat","stat","chmod","cp","mv","rm","touch","grep","sed","head","wc",
        "echo","ps","ns","kill","chroot","man","links","mkinitrd",
        "zbl-mkconfig","zbl-install","rcon","sh","for","boot","rescue","ask",
        "find","netstat",
        "ben","json","exit","cd","pwd","bind","unbind","help","true","false",
        NULL
    };
    for (const char *q = strchr(txt, '`'); q; ) {
        const char *e = strchr(q + 1, '`');
        if (!e) break;
        char prog[64];
        size_t k = 0;
        for (const char *w = q + 1; w < e && *w != ' ' && k < sizeof prog - 1; w++)
            prog[k++] = *w;
        prog[k] = 0;
        if (prog[0] && prog[0] != '/' && prog[0] != '.' && prog[0] != '-') {
            bool found = false;
            for (int i = 0; REAL[i] && !found; i++)
                if (strcmp(prog, REAL[i]) == 0) found = true;
            if (!found) return false;
        }
        q = strchr(e + 1, '`');
    }
    return true;
}

/* Ask somebody who is not the customer. `who` is "coworker" or "manager". */
void colleague_ask(Machine *m, const char *who, const char *question, Buf *out)
{
    bool boss = who && who[0] == 'm';
    const char *name = boss ? "Json" : "Ben";

    if (llm_available()) {
        char reply[600];
        int n = boss ? m->cust.nmgr : m->cust.ncow;
        const char *hist[CUST_TURNS * 2];
        int nh = n < CUST_TURNS ? n : CUST_TURNS;
        for (int i = 0; i < nh; i++) {
            hist[i * 2]     = boss ? m->cust.mq[i] : m->cust.cq[i];
            hist[i * 2 + 1] = boss ? m->cust.ma[i] : m->cust.ca[i];
        }
        /* The manager is allowed a paragraph; the colleague stays brief. */
        bool got = boss
            ? llm_ask_long(MANAGER_BRIEF, hist, nh, question, reply, sizeof reply)
            : llm_ask_hist(COWORKER_BRIEF, hist, nh, question, reply, sizeof reply);
        /* One retry, then honesty. A senior engineer who says "I would have
         * to look" is useful; one who invents a command is worse than
         * silence, because it is wrong in an authoritative voice. */
        if (got && reply[0] && !names_only_real_commands(reply)) {
            got = boss
                ? llm_ask_long(MANAGER_BRIEF, hist, nh, question, reply, sizeof reply)
                : llm_ask_hist(COWORKER_BRIEF, hist, nh, question, reply, sizeof reply);
            if (got && reply[0] && !names_only_real_commands(reply)) {
                buf_printf(out,
                    "  %s: \"I would have to look at that one -- I do not want "
                    "to send you after a command this box has not got. Start "
                    "with `dmesg` and tell me which layer it stops at.\"\n",
                    name);
                return;
            }
        }
        if (got && reply[0]) {
            if (n >= CUST_TURNS) {
                for (int i = 1; i < CUST_TURNS; i++) {
                    if (boss) {
                        snprintf(m->cust.mq[i-1], sizeof m->cust.mq[0], "%s", m->cust.mq[i]);
                        snprintf(m->cust.ma[i-1], sizeof m->cust.ma[0], "%s", m->cust.ma[i]);
                    } else {
                        snprintf(m->cust.cq[i-1], sizeof m->cust.cq[0], "%s", m->cust.cq[i]);
                        snprintf(m->cust.ca[i-1], sizeof m->cust.ca[0], "%s", m->cust.ca[i]);
                    }
                }
                n = CUST_TURNS - 1;
            }
            if (boss) {
                snprintf(m->cust.mq[n], sizeof m->cust.mq[0], "%s", question);
                snprintf(m->cust.ma[n], sizeof m->cust.ma[0], "%s", reply);
                m->cust.nmgr = n + 1;
            } else {
                snprintf(m->cust.cq[n], sizeof m->cust.cq[0], "%s", question);
                snprintf(m->cust.ca[n], sizeof m->cust.ca[0], "%s", reply);
                m->cust.ncow = n + 1;
            }
            buf_printf(out, "  %s: \"%s\"\n", name, reply);
            return;
        }
    }

    /* No model: say so plainly rather than inventing advice. */
    if (boss)
        buf_printf(out, "  %s: \"Read the boot log first -- `dmesg`, or "
                        "`dmesg -r /mnt` from the rescue medium. It tells you "
                        "which layer failed, and that decides which package to "
                        "suspect.\"\n", name);
    else
        buf_printf(out, "  %s: \"I have not seen it. What did the boot log "
                        "say?\"\n", name);
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
        bool earned = unlocks(c, t);
        build_brief(m, c, earned, brief, sizeof brief);
        /* No forbidden list: D21 removed the reason for one. The customer
         * cannot give away an answer it was never told. */
        /* The call so far, replayed. The BRIEF is rebuilt from scratch every
         * turn -- so once the machine boots, the customer stops insisting it
         * will not start -- while the transcript persists, so they remember
         * what they have already told you. State fresh, memory kept. */
        const char *hist[CUST_TURNS * 2];
        int nh = m->cust.nturns < CUST_TURNS ? m->cust.nturns : CUST_TURNS;
        for (int i = 0; i < nh; i++) {
            hist[i * 2]     = m->cust.hq[i];
            hist[i * 2 + 1] = m->cust.ha[i];
        }
        if (llm_ask_hist(brief, hist, nh, question, reply, sizeof reply)) {
            /* Remember it. When the call runs long the oldest exchange is
             * dropped, which is also roughly what a person does. */
            if (m->cust.nturns >= CUST_TURNS) {
                for (int i = 1; i < CUST_TURNS; i++) {
                    snprintf(m->cust.hq[i-1], sizeof m->cust.hq[0], "%s", m->cust.hq[i]);
                    snprintf(m->cust.ha[i-1], sizeof m->cust.ha[0], "%s", m->cust.ha[i]);
                }
                m->cust.nturns = CUST_TURNS - 1;
            }
            snprintf(m->cust.hq[m->cust.nturns], sizeof m->cust.hq[0], "%s", question);
            snprintf(m->cust.ha[m->cust.nturns], sizeof m->cust.ha[0], "%s", reply);
            m->cust.nturns++;
        }
        if (reply[0]) {
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
    case T_SCREEN:
        buf_puts(out, "  \"");
        buf_puts(out, saw_of(m));
        buf_puts(out, "\"\n");
        return;

    case T_HELLO:
        buf_puts(out, "  \"Hello. Look, I really need this back today.\"\n");
        return;
    case T_WHEN: {
        /* The timeline is the one thing every customer volunteers, and it is
         * where a real lead belongs: not the cause, but WHEN and WHAT ELSE
         * was going on. This is what makes talking to them worth the time. */
        static const char *LEAD[] = {
          [C_TIDIED]     = "  \"It had been complaining about space for weeks "
                           "before that, mind.\"\n",
          [C_UPGRADED]   = "  \"It did ask me about some updates on Friday, if "
                           "that is any use.\"\n",
          [C_CONFIGURED] = "  \"I was in and out of the settings last week for "
                           "something else.\"\n",
          [C_VENDOR]     = "  \"The only thing I can think of is those "
                           "monitoring people were in on Tuesday.\"\n",
          [C_POWERCUT]   = "  \"We did have that power cut on Tuesday, but it "
                           "came back fine afterwards.\"\n",
          [C_INNOCENT]   = "  \"Nothing happened at all. That is what is so "
                           "annoying about it.\"\n",
        };
        buf_puts(out, "  \"It was working yesterday. I shut it down normally\n"
                      "  last night and this morning it just... did not come\n"
                      "  back.\"\n");
        buf_puts(out, LEAD[c]);
        return;
    }
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
    if (unlocks(c, t)) {
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
    A_NONE = 0, A_POWER, A_SCREEN, A_DISC, A_CABLE, A_TYPEPW, A_SITDOWN,
    A_RUN
} Action;



/* ---------------------------------------------------------- the tool call --
 *
 * THE MODEL DECIDES WHAT THE TECHNICIAN ASKED FOR, not a keyword table.
 *
 * David, on the table I had here first: "Uhm... that seems brittle. Like what
 * if I say 'Can I have you enter: ls / and read back what you see.' the lm
 * should tool call 'ls /'. A lookup table is a mess." He is right, and it is
 * the same brittleness this project criticises everywhere else -- a table only
 * matches the phrasings I happened to imagine.
 *
 * So this is a genuine tool call. One model call whose ONLY job is to decide
 * which of a small set of actions the technician is asking for, and to extract
 * the command if there is one. It is deliberately not the same call that
 * writes the dialogue: classification wants a short, constrained answer and
 * conversation wants a long, free one, and asking a 3B model to do both at
 * once gets you neither.
 *
 * The reply is one line, from a closed set:
 *
 *   RUN <command>   type this at the keyboard and read back what appears
 *   POWER           turn it off and on again
 *   DISC            put the rescue medium in
 *   CABLE           check it is plugged in
 *   PASSWORD        type the root password
 *   SCREEN          read out what is on the screen
 *   NONE            it was a question, not an instruction
 *
 * The keyword table remains ONLY as the fallback when there is no model at
 * all, because a build without llama must still play.
 */
static const char *TOOL_BRIEF =
"You decide what a computer technician is asking you to DO. You are not "
"having a conversation: you output one line and nothing else.\n"
"\n"
"Answer with exactly one of:\n"
"  RUN <command>   they want you to type something at the keyboard\n"
"  POWER           they want the machine turned off and on again\n"
"  DISC            they want the rescue/recovery disc put in the drive\n"
"  CABLE           they want you to check a cable or plug\n"
"  PASSWORD        they want the password typed in\n"
"  SCREEN          they want to know what is on the screen right now\n"
"  NONE            they are asking a question, not asking you to do anything\n"
"\n"
"For RUN, give the command EXACTLY as they said it, with no quotes, no\n"
"backticks and no explanation.\n"
"\n"
"Examples:\n"
"Q: could you type ls /boot for me\n"
"A: RUN ls /boot\n"
"Q: Can I have you enter: 'ls /' and read back what you see.\n"
"A: RUN ls /\n"
"Q: at the prompt, put in df -h and tell me the numbers\n"
"A: RUN df -h\n"
"Q: reboot the computer\n"
"A: POWER\n"
"Q: turn it off and on again please\n"
"A: POWER\n"
"Q: can you power cycle the box\n"
"A: POWER\n"
"Q: pop the recovery disc in the drive\n"
"A: DISC\n"
"Q: is it plugged in at the wall?\n"
"A: CABLE\n"
"Q: what does the screen say\n"
"A: SCREEN\n"
"Q: when did it last work properly?\n"
"A: NONE\n"
"Q: have you deleted anything recently\n"
"A: NONE\n"
/* A question that CONTAINS an action word is still a question. "was there a
 * power cut" was being answered SCREEN, which is the one failure mode that
 * matters: mistaking a question for an instruction makes the customer do
 * something nobody asked for. */
"Q: was there a power cut on Tuesday\n"
"A: NONE\n"
"Q: did anyone reboot it before you called?\n"
"A: NONE\n"
"Q: is the screen broken?\n"
"A: NONE\n"
"\n"
"If they are asking ABOUT something rather than asking you to DO it, the\n"
"answer is NONE, however many action words the sentence contains.\n";

/* Ask the model what was being asked for. Returns the action, and copies the
 * command into `cmd` for RUN. */
static Action tool_call(const char *request, char *cmd, size_t cmdsz)
{
    cmd[0] = 0;
    if (!llm_available()) return A_NONE;

    char reply[256];
    if (!llm_classify(TOOL_BRIEF, request, reply, sizeof reply))
        return A_NONE;

    /* The model sometimes wraps the line in quotes or prefixes "A:". Strip
     * what a small model actually does rather than what it was told to do. */
    char *r = reply;
    while (*r == ' ' || *r == '"' || *r == '`') r++;
    if ((r[0] == 'A' || r[0] == 'a') && r[1] == ':') { r += 2; while (*r == ' ') r++; }

    if (strncmp(r, "RUN", 3) == 0 && (r[3] == ' ' || r[3] == ':')) {
        const char *q = r + 4;
        while (*q == ' ' || *q == '"' || *q == '`' || *q == ':') q++;
        size_t k = 0;
        while (*q && *q != '\n' && k < cmdsz - 1) {
            if (*q == '"' || *q == '`') { q++; continue; }
            cmd[k++] = *q++;
        }
        while (k && (cmd[k-1] == ' ' || cmd[k-1] == '.')) k--;
        cmd[k] = 0;
        return cmd[0] ? A_RUN : A_NONE;
    }
    if (strncmp(r, "POWER", 5) == 0)    return A_POWER;
    if (strncmp(r, "DISC", 4) == 0)     return A_DISC;
    if (strncmp(r, "CABLE", 5) == 0)    return A_CABLE;
    if (strncmp(r, "PASSWORD", 8) == 0) return A_TYPEPW;
    if (strncmp(r, "SCREEN", 6) == 0)   return A_SCREEN;
    return A_NONE;
}

/* For the measurement harness: what did the model decide, as a string. */
void customer_tool_probe(const char *request, char *out, size_t outsz)
{
    char cmd[256];
    Action a = tool_call(request, cmd, sizeof cmd);
    switch (a) {
    case A_RUN:    snprintf(out, outsz, "RUN %s", cmd); break;
    case A_POWER:  snprintf(out, outsz, "POWER");    break;
    case A_DISC:   snprintf(out, outsz, "DISC");     break;
    case A_CABLE:  snprintf(out, outsz, "CABLE");    break;
    case A_TYPEPW: snprintf(out, outsz, "PASSWORD"); break;
    case A_SCREEN: snprintf(out, outsz, "SCREEN");   break;
    default:       snprintf(out, outsz, "NONE");     break;
    }
}

bool customer_do(Machine *m, const char *request, Buf *out)
{
    /* THE MODEL, AND ONLY THE MODEL.
     *
     * A keyword table sat behind this as a fallback and David wanted it gone:
     * "No lookup table, not model, not chat at all." He is right that keeping
     * it invited the failure it was meant to prevent -- two code paths that
     * disagree about what a sentence means, with the worse one silently
     * winning whenever the better one hesitated. One mechanism, measured at
     * 22/22, or none. */
    static char toolcmd[256];
    Action a = tool_call(request, toolcmd, sizeof toolcmd);
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
        /* THEY ACTUALLY DO IT.
         *
         * This used to print a line of dialogue and touch nothing, so the
         * customer was describing a reboot that had not happened -- and a
         * technician watching the console over the service processor saw
         * nothing change, which is the worst kind of lie a simulation can
         * tell. The customer is the pair of hands in the room: asking them to
         * power cycle the box power cycles the box, and if they have put the
         * rescue disc in, the box comes up on the disc.
         *
         * That also makes the two routes agree. `rcon power cycle` and "could
         * you turn it off and on again" do the same thing to the same machine,
         * because they are the same act performed by different people. */
        m->cust.power_cycles++;
        m->cust.at_machine = true;
        if (m->cust.disc_inserted) machine_boot_rescue(m);
        else                       machine_boot(m);
        /* The boot itself replaced the console, so the note goes after. */
        buf_puts(&m->boot.console,
                 "\n[power button pressed at the machine]\n");

        if (m->cust.disc_inserted)
            buf_puts(out, "  \"Right, holding the button... it is coming back "
                          "up.\"\n  \"It looks different this time -- lots of "
                          "writing, and it has stopped with a hash.\"\n");
        else if (m->cust.power_cycles == 1)
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

    case A_RUN: {
        /* THEY TYPE WHAT YOU DICTATE, AND READ BACK WHAT THEY SEE.
         *
         * The command really runs on their machine -- this is not a canned
         * response, it is a shell round trip through a person. What comes
         * back is degraded the way a person degrades it: they read the last
         * few lines, they do not know which parts matter, and they say so.
         *
         * It stays FAIR because the output is real. Every character they read
         * back is a character the machine printed. They are a slow, narrow
         * pipe, not an unreliable one. */
        const char *cmd = toolcmd[0] ? toolcmd : NULL;
        if (!cmd || !*cmd) {
            buf_puts(out, "  \"Type what, sorry? Tell me exactly what to put "
                          "in and I will read out whatever it says.\"\n");
            return true;
        }

        /* Strip the quotes and backticks a technician naturally puts round a
         * command when dictating it. */
        char clean[256];
        size_t k = 0;
        for (const char *q = cmd; *q && k < sizeof clean - 1; q++) {
            if (*q == '`' || *q == '"' || *q == '\'') continue;
            clean[k++] = *q;
        }
        while (k && (clean[k-1] == ' ' || clean[k-1] == '.' ||
                     clean[k-1] == '?' || clean[k-1] == '\n')) k--;
        clean[k] = 0;
        if (!clean[0]) {
            buf_puts(out, "  \"I did not catch the command.\"\n");
            return true;
        }

        m->cust.at_machine = true;
        if (!m->boot.running) {
            buf_puts(out, "  \"There is nowhere to type it. It has not "
                          "finished starting up -- there is no prompt, just "
                          "the writing that stopped.\"\n");
            return true;
        }

        Buf o = {0};
        kernel_run(m, clean, &o);

        /* IT GOES ON THE CONSOLE, because that is what a console is.
         *
         * David: "if you are remoted into the box and ask the client to run
         * ls, you should see that in your connect terminal." Of course you
         * should -- a service processor shows the machine's screen, and the
         * machine's screen is where the person standing at it is typing.
         * Anything else would mean the console lies by omission whenever a
         * human touches the keyboard.
         *
         * It is appended whether or not anyone is attached, for the same
         * reason a real screen keeps displaying with nobody watching. */
        buf_printf(&m->boot.console, "%s@%s:~# %s\n",
                   "user", m->id, clean);
        if (o.len) buf_put(&m->boot.console, o.p, o.len);

        buf_printf(out, "  \"Alright... I have typed %s.\"\n", clean);
        if (!o.len) {
            buf_puts(out, "  \"It did not say anything back. Just the prompt "
                          "again.\"\n");
            buf_free(&o);
            return true;
        }

        /* They read out the tail. A person reads what is in front of them,
         * and what is in front of them is the end of the output. */
        int lines = 0;
        size_t e = o.len;
        while (e && o.p[e-1] == '\n') e--;
        size_t b = e;
        while (b && lines < 6) {
            if (o.p[b-1] == '\n') { lines++; if (lines >= 6) break; }
            b--;
        }
        buf_puts(out, "  \"It says:\"\n");
        for (size_t i = b; i < e; ) {
            size_t le = i;
            while (le < e && o.p[le] != '\n') le++;
            buf_puts(out, "    | ");
            buf_put(out, o.p + i, le - i);
            buf_puts(out, "\n");
            i = le < e ? le + 1 : e;
        }
        if (b > 0)
            buf_puts(out, "  \"There is more above that but it has scrolled "
                          "off. Do you want me to do it again?\"\n");
        buf_free(&o);
        return true;
    }

    case A_DISC:
        if (m->cust.disc_inserted) {
            buf_puts(out, "  \"It is already in there.\"\n");
            return true;
        }
        m->cust.disc_inserted = true;
        /* The disc going in is the SAME event as the virtual drive being
         * loaded over the service processor -- one drive, one piece of state
         * -- so blkid, mount and `rcon status` all see it happen. */
        m->sp_media = true;
        m->sp_bootdev = 1;
        m->cust.at_machine = true;
        buf_puts(&m->boot.console, "[rescue medium inserted at the machine]\n");
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
        "  they know what changed. they are not going to lead with it.\n"
        /* A playtester burned five tickets before working out that hanging up
         * loses the machine: they would read the ticket, disconnect to think,
         * reconnect to act, and be handed a different fault without being
         * told. The rule is fine -- one call, one machine, like a real call --
         * but it was nowhere in the help, and an undocumented rule that costs
         * you your work is just a trap. */
        "  this call IS the ticket: hang up and this machine is gone, and the\n"
        "  next connection is a different fault. do the whole job in one go.\n");
}
