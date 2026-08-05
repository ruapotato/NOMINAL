/* customer.c — the person whose machine it is, and the only pair of hands
 * in the room.
 *
 * The machine tells you WHAT is wrong. Only the customer knows what CHANGED,
 * and they are not going to volunteer it. That asymmetry is the other half of
 * break-fix and it is missing from every game about computers.
 *
 * WHY THIS IS NOT A LANGUAGE MODEL ANY MORE.
 *
 * It was one for a while: a 3B model answered every question in her voice.
 * Four blind playtests agreed on the verdict. Sixty to a hundred and twenty
 * seconds a reply, one of them nine minutes, and worst on exactly the tickets
 * this character exists for; 1.8GB of weights plus a vendored runtime plus two
 * gates whose whole job was to catch it inventing commands, which it still did
 * after two rounds of fixes.
 *
 * The thing worth keeping was never the prose. What playtesters quoted back
 * was "There is more above that but it has scrolled off. Do you want me to do
 * it again?" and being forced to break a long sed into four short ones. Both
 * of those are RULES -- a terminal is a fixed number of lines high, and there
 * is a limit to how much anyone will type off a phone call. A rule is always
 * true, costs microseconds, and cannot invent a command.
 *
 * So she is a menu now, and the menu is not a downgrade: every line she says
 * is derived from the state of that machine, she can only see the bottom of
 * the screen, she misreads characters, and she never once says a word she was
 * not read. The round trip through a person is intact. The improvisation is
 * gone, and with it the only part that could lie.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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
    m->cust.mood = MOOD_WARY;
    m->cust.asked = 0;
    m->cust.remarks = 0;
    m->cust.deflected = false;
    m->cust.confessed = false;
    memset(m->cust.told, 0, sizeof m->cust.told);
    buf_clear(&m->cust.screen);
    m->cust.nlines = 0;
    m->cust.scroll = 0;
}

/* Who is on the phone. Stable for a given persona, so the same name is
 * always the same person. */
const char *customer_name(const Machine *m)
{
    return CUSTNAME[m ? m->cust.persona % NPERSONA : 0];
}

/* ------------------------------------------------------- what she is like --
 *
 * Everything below is a pure function of the persona index, which is a pure
 * function of the ticket. Two playtesters independently complained that "hang
 * on, let me get my glasses" was word for word identical across three
 * different customers; the fix is not more lines, it is that WHICH line you
 * get is a property of the person.
 */

/* HOW MUCH OF THE SCREEN SHE CAN SEE.
 *
 * A terminal is a fixed number of lines high and a person reads what is on it,
 * not what scrolled past twenty seconds ago. This is the rule behind the
 * favourite line in the game. Some people read the whole screen out, some read
 * you the last four lines and think that is the whole screen; both are the
 * same person every time you ring them. */
static int screen_lines(int p) { return 4 + (p % 6) * 2; }   /* 4 .. 14 */

/* HOW MUCH SHE WILL TYPE IN ONE GO.
 *
 * Long enough for the canonical air-gapped repair, which is a sed with two
 * uuids and a path in it and comes to about eighty characters, because a limit
 * that makes the ticket unsolvable is not a constraint, it is a wall. Past it
 * she SAYS it is too long and types nothing -- never a shortened version of
 * what was dictated. That bug has happened once already: `sed -i s/old/new/
 * /mnt/boot/zbl/zbl.cfg` arrived cut to `sed -i s/old/new/ /` and was run, and
 * she reported having typed it. The shell has variables, so a line past the
 * limit can always be split into two that are not. */
static int dictate_max(int p) { return 88 + (p % 5) * 12; }  /* 88 .. 136 */

/* THE SAME THREE LINES EVERY TIME, ON EVERY CUSTOMER, was the complaint.
 * Bound to the persona, so a given customer always squints the same way and
 * different customers do not. */
static const char *LEANS[] = {
  "It says... hang on, let me get my glasses.",
  "Right, hold on, I will read it to you.",
  "Let me lean in, the writing is tiny.",
  "Give me a second, I am squinting at it.",
  "There is a load of it. The bottom line says:",
  "Okay. The last thing on it is:",
  "It is all white writing on black. The bit at the bottom says:",
  "Hang on, I will move the lamp... right.",
  "I will read it exactly as it is written, shall I?",
  "It has stopped with this on it:",
  "Let me put my coffee down. It says:",
  "I am looking at it now.",
  "Bear with me, I am not good at reading these.",
  "It has been sitting like this all morning. It says:",
  "Hold on, I will read you the last bit.",
  "Right. Word for word, the end of it says:",
  "I have been staring at this since eight o'clock. It says:",
  "There is white writing all down it. The last line is:",
  "Let me get closer, the desk is a bit far back.",
  "Do you want all of it, or the bottom? The bottom says:",
  "One second... okay, here we go.",
  "I will spell it out if you need. It reads:",
  "It has not moved since I rang. The bottom of it says:",
  "Give me a moment, I had the brightness down.",
};
static const char *AFTERS[] = {
  "Does that mean anything to you?",
  "Is that bad?",
  "I have no idea what any of that means.",
  "That is all there is, I am afraid.",
  "Should there be more than that?",
  "I did write it down earlier, if it helps.",
  "It has not changed since I rang you.",
  "Does that help at all?",
  "Is that the sort of thing you wanted?",
  "I can take a photograph of it if that is easier.",
  "That is word for word, that is.",
  "I have written it down as well, just in case.",
  "None of that was there yesterday, I do not think.",
};
/* What she does with her hands and her attention while you are talking. Not
 * information: texture, and the reason two customers who say the same true
 * thing do not sound like one customer. */
static const char *TICS[] = {
  "Sorry -- somebody is talking to me. Go on.",
  "Right. Yes.",
  "Sorry, say again?",
  "Hang on, I am writing this down.",
  "Mm. Yes, alright.",
  "Bear with me, I have the phone under my chin.",
  "Sorry, one moment... right, I am back.",
  "Okay. I am with you.",
  "I am not really a computer person, as you can tell.",
  "Take your time, I am not going anywhere.",
  "Sorry, I did not catch all of that.",
  "Yes -- yes, I see.",
};
#define PICK(arr, i) (arr[(i) % (int)(sizeof arr / sizeof arr[0])])

static void say(Buf *out, const char *s) { buf_printf(out, "  \"%s\"\n", s); }

/* A remark she throws in without being asked, every few exchanges. Every one
 * is TRUE OF THAT MACHINE and derived from it: the disc really is still in the
 * drive, it really was low on space, somebody really was on it on Friday. A
 * customer who volunteers a lead is a source; a customer who volunteers colour
 * is a liar. */
static void maybe_remark(Machine *m, Buf *out)
{
    if (!m || m->cust.remarks >= 3) return;
    if ((m->cust.asked % 4) != 3) return;

    const char *pool[10];
    int n = 0;
    if (m->cust.disc_inserted)
        pool[n++] = "That disc is still in the front of it, by the way.";
    if (m->fs_dirty)
        pool[n++] = "It did go off in the middle of things the other day, "
                    "if that is any use.";
    if (m->fs_capacity && machine_disk_used(m) * 10 > m->fs_capacity * 9)
        pool[n++] = "It has been telling me it is low on space for weeks, "
                    "mind you.";
    if (m->cust.power_cycles >= 3)
        pool[n++] = "I have turned this off and on more times this morning "
                    "than in the last five years.";
    if (m->cust.cause == C_VENDOR)
        pool[n++] = "Somebody was doing something with it on Friday, "
                    "actually. I did not ask what.";
    if (m->on_rescue)
        pool[n++] = "It still does not look like my computer, that screen.";
    if (m->boot.running && !m->on_rescue) {
        Buf sick = {0};
        int dead = kernel_health(m, &sick);
        buf_free(&sick);
        if (dead > 0)
            pool[n++] = "Some of what I use it for is still not there, "
                        "if that matters.";
        else
            pool[n++] = "It does look happier than it did, I will say that.";
    }
    if (!m->boot.running && m->powered)
        pool[n++] = "It has been sitting exactly like that since I rang you.";
    if (!n) return;
    say(out, pool[(m->cust.persona + m->cust.remarks) % n]);
    m->cust.remarks++;
}

/* -------------------------------------------------------- reading it back --
 *
 * She misreads. Transposed letters, a nought read as the letter O, a one read
 * as an I. Rarely, deterministically -- the same person misreads the same line
 * the same way every time, which is what makes "read that back to me again"
 * a real move rather than a dice roll.
 *
 * NEVER ON THE LINE THAT MATTERS MOST. The bottom line of what she is reading
 * is the one you asked for, and a ticket where the decisive line is scrambled
 * is not hard, it is unfair. Nor on anything with a path or an assignment in
 * it: mangling a uuid you are about to type back is the same unfairness with
 * more steps.
 */
static unsigned hash_of(const char *s, unsigned h)
{
    for (; *s; s++) h = h * 33u + (unsigned char)*s;
    return h;
}

static bool misread(int persona, const char *in, char *out, size_t osz)
{
    size_t n = strlen(in);
    if (n < 12 || n >= osz) return false;
    if (strchr(in, '/') || strchr(in, '=')) return false;

    unsigned h = hash_of(in, (unsigned)persona * 2654435761u + 17u);
    if (h % 7) return false;

    memcpy(out, in, n + 1);
    for (int kind = 0; kind < 3; kind++) {
        switch ((h / 7 + (unsigned)kind) % 3) {
        case 0:                        /* two letters the wrong way round */
            for (size_t i = 1; i + 2 < n; i++) {
                char a = out[i], b = out[i + 1];
                bool al = (a >= 'a' && a <= 'z'), bl = (b >= 'a' && b <= 'z');
                if (al && bl && a != b && ((h >> 3) % (n - 2)) == i - 1) {
                    out[i] = b; out[i + 1] = a;
                    return true;
                }
            }
            break;
        case 1:                        /* a nought read as the letter O */
            for (size_t i = 0; i < n; i++)
                if (out[i] == '0') { out[i] = 'O'; return true; }
            break;
        default:                       /* "I is" for "1 is" */
            for (size_t i = 0; i < n; i++)
                if (out[i] == '1') { out[i] = 'I'; return true; }
            break;
        }
    }
    return false;
}

/* Where line `i` of the screen buffer starts and ends. Lines are counted once
 * when the material arrives and the buffer never changes after that, so
 * walking it is cheap and there is no second copy to fall out of step. */
static void line_at(const Buf *b, int want, size_t *bp, size_t *ep)
{
    size_t i = 0;
    int idx = 0;
    *bp = *ep = 0;
    while (i < b->len) {
        size_t e = i;
        while (e < b->len && b->p[e] != '\n') e++;
        if (idx == want) { *bp = i; *ep = e; return; }
        idx++;
        i = e < b->len ? e + 1 : e;
    }
}

static int count_lines(const Buf *b)
{
    int n = 0;
    size_t i = 0;
    while (i < b->len) {
        size_t e = i;
        while (e < b->len && b->p[e] != '\n') e++;
        n++;
        i = e < b->len ? e + 1 : e;
    }
    return n;
}

/* WHAT SHE IS LOOKING AT STOPS EXISTING WHEN THE SCREEN DOES.
 *
 * Rebooting the machine clears the console, and the copy she was reading from
 * is then a photograph of a screen that is no longer there -- so "scroll back
 * up and read me what came before" would have read back lines the machine had
 * printed before it was restarted, presented as what is in front of her now.
 * That is the one thing she is never allowed to do. */
static void forget_screen(Machine *m)
{
    buf_clear(&m->cust.screen);
    m->cust.nlines = 0;
    m->cust.scroll = 0;
}

/* Hand her something to read: she looks at the bottom of it. */
static void put_screen(Machine *m, const char *text, size_t len)
{
    buf_clear(&m->cust.screen);
    /* Trailing blank lines are not on the screen, they are the cursor. */
    while (len && (text[len - 1] == '\n' || text[len - 1] == ' ')) len--;
    buf_put(&m->cust.screen, text, len);
    m->cust.nlines = count_lines(&m->cust.screen);
    m->cust.scroll = m->cust.nlines;      /* she has read none of it yet */
}

/* Read one screenful back, from `scroll` upwards. `second` is a second look:
 * she is being careful, so she does not misread. */
static void read_window(Machine *m, Buf *out, bool second)
{
    int win = screen_lines(m->cust.persona);
    int bot = second ? m->cust.scroll + win : m->cust.scroll;
    int top = second ? m->cust.scroll : m->cust.scroll - win;
    if (bot > m->cust.nlines) bot = m->cust.nlines;
    if (top < 0) top = 0;
    if (top >= bot) {
        say(out, "There is nothing above that. That is the top of it.");
        return;
    }

    say(out, PICK(LEANS, m->cust.persona));
    for (int i = top; i < bot; i++) {
        size_t b, e;
        line_at(&m->cust.screen, i, &b, &e);
        char raw[240], bad[240];
        size_t n = e - b;
        if (n >= sizeof raw) n = sizeof raw - 1;
        memcpy(raw, m->cust.screen.p + b, n);
        raw[n] = 0;
        const char *show = raw;
        /* Not the bottom line of what she is reading: that is the one you
         * asked for. */
        if (!second && i + 1 < bot && misread(m->cust.persona, raw, bad, sizeof bad))
            show = bad;
        buf_printf(out, "    | %s\n", show);
    }
    if (!second) m->cust.scroll = top;

    if (top > 0)
        say(out, "There is more above that but it has scrolled off. Do you "
                 "want me to do it again?");
    else
        say(out, PICK(AFTERS, m->cust.persona * 5 + 1));
}

/* ------------------------------------------------------------ what she saw --
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
    if (!m->powered)
        return "It is off. The screen is black and I cannot hear the fan.";
    if (!m->boot.running) {
        if (m->boot.failed_at <= BOOT_INITRD)
            return "It will not start at all -- it never gets as far as "
                   "asking me to log in.";
        return "It starts to come up, there is some white writing on a black "
               "background, and then it stops. It never asks me to log in.";
    }
    /* THE MEDIUM UNDERNEATH IS PART OF WHAT THEY CAN SEE, and it decides the
     * answer to the last question of every air-gapped call. "Is it working
     * now?" asked over a rescue shell was being answered from the disk's
     * health, which is a true statement about a system that is not the one in
     * front of them. They cannot name it, but nobody misses that their login
     * box has turned into a hash. */
    if (m->on_rescue)
        return "It did come back on, but it does not look like my computer. "
               "There is no login box -- just a lot of writing and a little "
               "square that blinks after a hash.";
    Buf sick = {0};
    int dead = kernel_health(m, &sick);
    buf_free(&sick);
    if (dead > 0)
        return "It does start, and it asks me to log in, so I thought it was "
               "fine. But things are not working right -- some of what I use "
               "it for just is not there any more.";
    return "It seems to be working now, actually. It was not this morning.";
}

/* The admission: what they will eventually own up to, if asked about the
 * right thing. Deliberately vague about paths -- a customer does not read
 * filenames back to you, they tell you what they were trying to achieve. */
static const char *admission(Cause c)
{
    switch (c) {
    case C_TIDIED:
        return "...alright. The disk was nearly full and I went through the "
               "folder deleting things that looked like old versions. It was "
               "fine afterwards. I did restart it later though.";
    case C_UPGRADED:
        return "...I did run the updater on Friday. It said something about a "
               "newer version being available and I said yes. It finished "
               "without complaining, so I assumed it was fine.";
    case C_CONFIGURED:
        return "...I was in the settings changing something and I might have "
               "fat-fingered a line. I was fairly sure I put it back.";
    case C_VENDOR:
        return "...the monitoring people were on it last week. They said they "
               "were installing something. I did not watch what they did.";
    case C_POWERCUT:
        return "...there was a power cut on Tuesday. It was on at the time. "
               "It came back up fine though, or I thought it did.";
    default:
        return "...no. Honestly, nothing. It was working when I left.";
    }
}

/* ------------------------------------------------------------ the options --
 *
 * Stable ids, not positions in a list. What she can do changes constantly --
 * the disc goes in, the machine comes up, she has already answered that -- and
 * a front end that renumbers under the player's fingers produces the one
 * mistake this menu exists to prevent: the wrong thing done confidently. So
 * option 2 is "can I have you run" on every ticket forever, and the list is
 * filtered rather than repacked.
 */
enum {
    O_SCREEN = 1,   /* read the whole screen back                          */
    O_RUN,          /* "can I have you run:" <command>                     */
    O_MORE,         /* scroll back up: the part that has scrolled off      */
    O_AGAIN,        /* read that last bit again, carefully                 */
    O_DOING,        /* what were you doing when it stopped working         */
    O_WHEN,         /* when did it last work properly                      */
    O_USEDFOR,      /* what do you use this machine for                    */
    O_DELETED,      /* have you deleted anything                           */
    O_UPDATES,      /* have you installed any updates                      */
    O_WHOELSE,      /* has anybody else been working on it                 */
    O_POWERCUT,     /* has it lost power recently                          */
    O_NOISES,       /* is it making any noises                             */
    O_PASSWORD,     /* what is the root password                           */
    O_CYCLE,        /* turn it off and on again                            */
    O_OFF,          /* turn it off                                         */
    O_ON,           /* turn it on                                          */
    O_DISCIN,       /* put the rescue disc in                              */
    O_DISCOUT,      /* take the rescue disc out                            */
    O_PLUGGED,      /* is it plugged in, is the screen on                  */
    O_STICKER,      /* read me the sticker on the front                    */
    O_WORKING,      /* is it working now                                   */
    O_MAX
};

static const char *label_of(int id)
{
    switch (id) {
    case O_SCREEN:   return "ask her to read the whole screen back";
    case O_RUN:      return "\"can I have you run:\"  <command>";
    case O_MORE:     return "yes -- scroll back up and read me what came before";
    case O_AGAIN:    return "ask her to read that last bit again, carefully";
    case O_DOING:    return "what were you doing when it stopped working?";
    case O_WHEN:     return "when did it last work properly?";
    case O_USEDFOR:  return "what do you use this machine for?";
    case O_DELETED:  return "have you deleted anything to make space?";
    case O_UPDATES:  return "have you installed any updates lately?";
    case O_WHOELSE:  return "has anybody else been working on it?";
    case O_POWERCUT: return "has it lost power, or gone off suddenly?";
    case O_NOISES:   return "is it making any noises?";
    case O_PASSWORD: return "ask her for the root password";
    case O_CYCLE:    return "ask her to turn it off and on again";
    case O_OFF:      return "ask her to turn it off";
    case O_ON:       return "ask her to turn it on";
    case O_DISCIN:   return "ask her to put the rescue disc in";
    case O_DISCOUT:  return "ask her to take the rescue disc out";
    case O_PLUGGED:  return "is it plugged in? is the screen on?";
    case O_STICKER:  return "ask her to read the sticker on the front";
    case O_WORKING:  return "is it working now, from where you are sitting?";
    default:         return "";
    }
}

/* CAN SHE DO IT, RIGHT NOW.
 *
 * The list must never offer something that cannot work, and must always leave
 * a way forward: the screen, the power button and "is it working" are on it
 * whatever state the machine is in. */
static bool offered(const Machine *m, int id)
{
    if (id <= 0 || id >= O_MAX) return false;
    switch (id) {
    case O_MORE:     return m->cust.screen.len > 0 && m->cust.scroll > 0;
    case O_AGAIN:    return m->cust.screen.len > 0 && m->cust.nlines > 0;
    case O_OFF:      return m->powered;
    case O_ON:       return !m->powered;
    case O_DISCIN:   return !m->cust.disc_inserted;
    case O_DISCOUT:  return m->cust.disc_inserted;
    case O_PASSWORD: return !m->cust.gave_password;
    /* DICTATING AT A MACHINE WITH NO PROMPT IS A THING A PLAYER WILL TRY, and
     * taking the option away does not stop them trying -- it just means the
     * game says no on her behalf. She is standing in front of it, so let her
     * answer: "there is nowhere to type it, it has not finished starting up".
     * That is the same refusal in her voice and it is also a diagnosis. */
    case O_RUN:
    case O_SCREEN: case O_CYCLE: case O_WORKING:
        return true;
    default:
        /* the questions: once she has answered one, she has answered it */
        return !m->cust.told[id];
    }
}

void customer_options(Machine *m, Buf *out)
{
    if (!m) return;
    for (int id = 1; id < O_MAX; id++)
        if (offered(m, id))
            buf_printf(out, "  [%d] %s\n", id, label_of(id));
}

/* Which question unlocks the admission.
 *
 * Several questions can earn it, not one. A playtester asked "did you install
 * anything?" on a ticket where a vendor had installed something, and was told
 * no -- because the only unlocking topic was "has anyone else worked on
 * this". That is not a customer being coy, that is a table being brittle. */
static bool unlocks(Cause c, int id)
{
    switch (c) {
    /* "What were you doing when it stopped" is THE question a technician opens
     * with, and C_TIDIED -- the commonest cause in the game -- was the one
     * cause that denied it. */
    case C_TIDIED:     return id == O_DELETED || id == O_NOISES || id == O_DOING;
    case C_UPGRADED:   return id == O_UPDATES || id == O_DOING;
    case C_CONFIGURED: return id == O_DOING   || id == O_UPDATES;
    case C_VENDOR:     return id == O_WHOELSE || id == O_UPDATES || id == O_DOING;
    case C_POWERCUT:   return id == O_POWERCUT || id == O_DOING;
    default:           return false;
    }
}

/* The lead she gives with the timeline. Not the cause: when, and what else was
 * going on. This is what makes talking to her worth the round trip. */
static const char *lead_of(Cause c)
{
    switch (c) {
    case C_TIDIED:     return "It had been complaining about space for weeks "
                              "before that, mind.";
    case C_UPGRADED:   return "It did ask me about some updates on Friday, if "
                              "that is any use.";
    case C_CONFIGURED: return "I was in and out of the settings last week for "
                              "something else.";
    case C_VENDOR:     return "The only thing I can think of is those "
                              "monitoring people were in on Tuesday.";
    case C_POWERCUT:   return "We did have that power cut on Tuesday, but it "
                              "came back fine afterwards.";
    default:           return "Nothing happened at all. That is what is so "
                              "annoying about it.";
    }
}

/* WHAT THIS MACHINE IS FOR, in her words, read off what is installed on it.
 *
 * She does not know the word for any of it, but she knows what stops working
 * when it stops working -- and every clause here is true of that disk because
 * it is asked of the package database. */
static void what_it_does(Machine *m, Buf *out)
{
    char line[320];
    size_t k = 0;
    k += (size_t)snprintf(line + k, sizeof line - k, "It is just my work "
                          "computer. ");
    if (pkg_find(m, "httpd"))
        k += (size_t)snprintf(line + k, sizeof line - k,
                              "The web page everyone looks at comes off it. ");
    if (pkg_find(m, "postfix"))
        k += (size_t)snprintf(line + k, sizeof line - k,
                              "It sends the emails out, I am told. ");
    if (pkg_find(m, "openssh"))
        k += (size_t)snprintf(line + k, sizeof line - k,
                              "People get onto it from their own desks. ");
    if (pkg_find(m, "cron"))
        snprintf(line + k, sizeof line - k,
                 "And it does something overnight on a timer that I have "
                 "never once seen.");
    say(out, line);
}

/* --------------------------------------------------------- the round trip --
 *
 * She types what you dictate and reads back what she sees. The command really
 * runs on her machine: this is not a canned response, it is a shell round trip
 * through a person. What comes back is degraded the way a person degrades it
 * -- she reads the bottom of the screen, she does not know which part matters,
 * and she says so. It stays FAIR because the output is real. Every character
 * she reads back is a character the machine printed. She is a slow, narrow
 * pipe, not an unreliable one.
 */
static void dictate(Machine *m, const char *arg, Buf *out)
{
    char clean[NOM_ARG_MAX];
    size_t k = 0;

    /* The quotes and backticks a technician puts round a command when
     * dictating it are not part of the command. */
    for (const char *q = arg ? arg : ""; *q && k < sizeof clean - 1; q++) {
        if (*q == '`') continue;
        clean[k++] = *q;
    }
    while (k && (clean[k - 1] == ' ' || clean[k - 1] == '\n')) k--;
    clean[k] = 0;
    while (clean[0] == ' ') memmove(clean, clean + 1, strlen(clean));

    if (!clean[0]) {
        say(out, "Run what, sorry? Tell me exactly what to type and I will "
                 "read out whatever it says.");
        return;
    }

    if (!m->powered) {
        say(out, "There is nowhere to type it -- it is off. The screen is "
                 "black. Do you want me to press the button?");
        return;
    }
    if (!m->boot.running) {
        say(out, "There is nowhere to type it. It has not finished starting "
                 "up -- there is no prompt, just the writing that stopped.");
        return;
    }

    /* A COMMAND THAT DID NOT ARRIVE WHOLE IS NOT TYPED. Never a truncated
     * version of it, whatever the buffer says. */
    if ((int)strlen(clean) > dictate_max(m->cust.persona)) {
        say(out, "Sorry -- that is more than I can type in one go. Can you "
                 "break it up for me?");
        say(out, "I will lose my place halfway through something that long.");
        return;
    }

    m->cust.at_machine = true;

    Buf o = {0};
    kernel_run(m, clean, &o);

    /* IT GOES ON THE CONSOLE, because that is what a console is. A service
     * processor shows the machine's screen, and the machine's screen is where
     * the person standing at it is typing. Appended whether or not anyone is
     * attached, for the same reason a real screen keeps displaying with nobody
     * watching. */
    buf_printf(&m->boot.console, "%s@%s:~# %s\n", "user", m->id, clean);
    if (o.len) buf_put(&m->boot.console, o.p, o.len);

    buf_printf(out, "  \"Alright... I have typed %s.\"\n", clean);
    if (!o.len) {
        say(out, "It did not say anything back. Just the prompt again.");
        forget_screen(m);
        buf_free(&o);
        return;
    }
    put_screen(m, o.p, o.len);
    /* Output that is nothing but blank lines is not something on a screen. */
    if (m->cust.nlines == 0)
        say(out, "It did not say anything back. Just the prompt again.");
    else
        read_window(m, out, false);
    buf_free(&o);
}

/* THEY ACTUALLY DO IT.
 *
 * The power button used to print a line of dialogue and touch nothing, so the
 * customer was describing a reboot that had not happened -- and a technician
 * watching the console over the service processor saw nothing change, which is
 * the worst kind of lie a simulation can tell. She is the pair of hands in the
 * room: asking her to power cycle the box power cycles the box, and if the
 * rescue disc is in, the box comes up on the disc. */
static void power_cycle(Machine *m, Buf *out)
{
    m->cust.power_cycles++;
    m->cust.at_machine = true;
    forget_screen(m);
    if (m->cust.disc_inserted) machine_boot_rescue(m);
    else                       machine_boot(m);
    buf_puts(&m->boot.console, "\n[power button pressed at the machine]\n");

    if (m->cust.disc_inserted)
        say(out, "Right, holding the button... it is coming back up. It looks "
                 "different this time -- lots of writing, and it has stopped "
                 "with a hash.");
    /* THE BOOT THAT WORKED, DESCRIBED AS ONE. This was chosen by how many
     * times she had pressed the button, so the reboot that ended a repair --
     * the machine coming up on its own disk, at last -- was answered "I have
     * done that twice now and it does the same thing every time." */
    else if (m->boot.running)
        say(out, "Holding the button... hang on. Oh! That is different -- it "
                 "has gone all the way through and it is asking me to log in.");
    else if (m->cust.power_cycles == 1)
        say(out, "Okay, holding the button... and back on. Same as before. It "
                 "gets partway and stops.");
    else if (m->cust.power_cycles < 4)
        say(out, "I have done that twice now and it does the same thing every "
                 "time.");
    else
        say(out, "I do not think turning it off and on again is going to fix "
                 "it, is it.");
}

/* WHEN SHE CANNOT, SHE SAYS WHY, AND SHE SAYS IT IN HER OWN WORDS.
 *
 * "She cannot do that right now" is the game talking. It reads as a menu
 * error, it tells the player nothing about the machine, and it is the exact
 * register this rewrite exists to get away from: she is an instrument, not a
 * form with a validation message. Every one of these describes what she can
 * SEE -- the tray is empty, it is already off, there is nothing left above
 * that -- which is information, and half of them are the answer to the
 * question that was really being asked. */
static void cannot(Machine *m, int idx, Buf *out)
{
    switch (idx) {
    case O_MORE:
        say(out, m->cust.screen.len
              ? "That is the top of it. There is nothing above that."
              : "There is nothing on the screen for me to read back.");
        return;
    case O_AGAIN:
        say(out, "There is nothing on the screen for me to read back.");
        return;
    case O_OFF:
        say(out, "It is already off. The screen is black and the fan has "
                 "stopped.");
        return;
    case O_ON:
        say(out, "It is already on. It has been on all morning.");
        return;
    case O_DISCIN:
        say(out, "It is already in there.");
        return;
    case O_DISCOUT:
        say(out, "There is nothing in the drive. The tray is empty.");
        return;
    case O_PASSWORD:
        say(out, "I have already given you that. hunter2, off the sticky "
                 "note.");
        return;
    default:
        break;
    }
    if (idx > 0 && idx < O_MAX && m->cust.told[idx]) {
        say(out, "I already told you about that.");
        return;
    }
    /* Not a thing she could be asked at all. This one IS an interface error
     * and says so plainly, rather than putting a nonsense sentence in her
     * mouth to cover for a front end that sent a number nobody offered. */
    buf_printf(out, "  there is no option %d.\n", idx);
}

void customer_choose(Machine *m, int idx, const char *arg, Buf *out)
{
    if (!m) return;
    if (!offered(m, idx)) { cannot(m, idx, out); return; }
    m->cust.asked++;
    if (m->cust.asked >= 3 && m->cust.mood < MOOD_OK)      m->cust.mood = MOOD_OK;
    if (m->cust.asked >= 6 && m->cust.mood < MOOD_HELPFUL) m->cust.mood = MOOD_HELPFUL;

    /* Texture, on the exchanges where a person would fill the silence. */
    if (idx != O_MORE && idx != O_AGAIN &&
        ((m->cust.asked + m->cust.persona) % 5) == 0)
        say(out, PICK(TICS, m->cust.persona * 3 + m->cust.asked));

    switch (idx) {
    case O_SCREEN: {
        m->cust.at_machine = true;
        if (!m->powered) {
            say(out, "There is nothing on it. It is black -- it is off.");
            break;
        }
        /* THE SCREEN, WHICH IS NOT THE WHOLE OF THE CONSOLE BUFFER. The
         * console also carries notes about the room -- "[power button pressed
         * at the machine]" -- which are there for a technician watching over
         * the service processor and were never printed by the machine. She
         * read one of those out word for word once, as though it were on her
         * screen. */
        Buf vis = {0};
        size_t i = 0;
        while (i < m->boot.console.len) {
            size_t e = i;
            while (e < m->boot.console.len && m->boot.console.p[e] != '\n') e++;
            size_t s = i;
            while (s < e && m->boot.console.p[s] == ' ') s++;
            if (s < e && m->boot.console.p[s] != '[') {
                buf_put(&vis, m->boot.console.p + i, e - i);
                buf_putc(&vis, '\n');
            }
            i = e < m->boot.console.len ? e + 1 : e;
        }
        if (!vis.len) {
            say(out, "It is just black. Nothing at all.");
            buf_free(&vis);
            break;
        }
        put_screen(m, vis.p, vis.len);
        buf_free(&vis);
        read_window(m, out, false);
        break;
    }

    case O_RUN:
        dictate(m, arg, out);
        break;

    case O_MORE:
        read_window(m, out, false);
        break;

    case O_AGAIN:
        say(out, "Alright, let me look properly this time.");
        read_window(m, out, true);
        break;

    case O_DOING:
    case O_DELETED:
    case O_UPDATES:
    case O_WHOELSE:
    case O_POWERCUT:
    case O_NOISES: {
        m->cust.told[idx] = 1;
        Cause c = (Cause)m->cust.cause;
        if (unlocks(c, idx)) {
            /* Whether she owns up depends on how the call has gone: a wary
             * customer deflects once and then tells you. */
            if (m->cust.mood == MOOD_WARY && !m->cust.deflected) {
                m->cust.deflected = true;
                m->cust.told[idx] = 0;     /* she will answer it properly next time */
                say(out, "No. I have not touched it.");
                break;
            }
            say(out, admission(c));
            m->cust.confessed = true;
            break;
        }
        switch (idx) {
        case O_DOING:
            say(out, "Nothing that I know of. I was doing what I always do and "
                     "then it just stopped.");
            break;
        case O_DELETED:
            say(out, "I have not deleted anything, no.");
            break;
        case O_UPDATES:
            say(out, "I do not do the updates. That is not my job.");
            break;
        case O_WHOELSE:
            say(out, "Just me. Nobody else has the password.");
            break;
        case O_POWERCUT:
            say(out, "No outages, no. It was shut down properly.");
            break;
        default:
            say(out, "No noises. It is a fairly new machine.");
            if (c == C_TIDIED)
                say(out, "It did keep telling me it was low on space, mind.");
            break;
        }
        break;
    }

    case O_WHEN:
        m->cust.told[idx] = 1;
        say(out, "It was working yesterday. I shut it down normally last night "
                 "and this morning it just... did not come back.");
        say(out, lead_of((Cause)m->cust.cause));
        break;

    case O_USEDFOR:
        m->cust.told[idx] = 1;
        what_it_does(m, out);
        break;

    case O_PASSWORD:
        /* A customer who will not read their password out to a stranger is an
         * obstacle with a human reason behind it, and that is a better
         * obstacle than a locked door. */
        if (m->cust.mood >= MOOD_OK) {
            say(out, "Fine. It is on a sticky note here. hunter2. Please do "
                     "not put that in the ticket.");
            m->cust.gave_password = true;
        } else {
            say(out, "I am not giving you the root password over the phone to "
                     "someone I have never spoken to.");
        }
        break;

    case O_CYCLE:
        power_cycle(m, out);
        break;

    case O_OFF:
        kernel_stop_daemons(m);
        m->powered = false;
        m->boot.running = false;
        buf_clear(&m->boot.console);
        buf_puts(&m->boot.console,
                 "[powered off -- the screen is black and the fans have "
                 "stopped]\n");
        forget_screen(m);
        m->cust.at_machine = true;
        say(out, "Held the button in... right, it has gone off. The screen is "
                 "black and the fan has stopped.");
        break;

    case O_ON:
        m->cust.at_machine = true;
        forget_screen(m);
        if (m->cust.disc_inserted) machine_boot_rescue(m);
        else                       machine_boot(m);
        buf_puts(&m->boot.console, "\n[power button pressed at the machine]\n");
        say(out, m->boot.running
              ? "Pressing it... there we are, it is coming up."
              : "Pressing it... it is doing something. There is writing on it "
                "again.");
        break;

    case O_DISCIN:
        m->cust.disc_inserted = true;
        /* The disc going in is the SAME event as the virtual drive being
         * loaded over the service processor -- one drive, one piece of state
         * -- so blkid, mount and `rcon status` all see it happen. */
        m->sp_media = true;
        m->sp_bootdev = 1;
        m->cust.at_machine = true;
        buf_puts(&m->boot.console, "[rescue medium inserted at the machine]\n");
        say(out, "Found it in the drawer. It is in.");
        say(out, "Do you want me to turn it off and on again?");
        break;

    case O_DISCOUT:
        /* THE WAY BACK OUT, which did not exist for a while: a playtester
         * repaired an air-gapped machine and then could not hand it back,
         * because every phrasing of "take the disc out" was answered "it is
         * already in there". A ticket could be solved and not finished. */
        m->cust.disc_inserted = false;
        m->sp_media = false;
        m->sp_bootdev = 0;
        m->cust.at_machine = true;
        buf_puts(&m->boot.console, "[rescue medium removed at the machine]\n");
        say(out, "Right -- it has popped out. Disc is back in the drawer.");
        say(out, "Do you want me to turn it off and on again?");
        break;

    case O_PLUGGED:
        m->cust.told[idx] = 1;
        m->cust.at_machine = true;
        say(out, "Hang on... yes, it is plugged in, both ends. The little "
                 "light on the front is on.");
        say(out, m->powered
              ? "And the screen is definitely on, there is writing on it."
              : "The screen is on -- it is just black, there is nothing on it.");
        break;

    case O_STICKER:
        m->cust.told[idx] = 1;
        m->cust.at_machine = true;
        /* WHAT IS ACTUALLY ON THE FRONT OF THAT MACHINE. On a networked box
         * the sticker is how you get the address; on an air-gapped one there
         * is no address to have, and she says what is there instead of
         * inventing one. */
        if (m->airgapped) {
            buf_printf(out, "  \"There is a little silver label. It says "
                            "NOMINAL, and NODE-%s underneath.\"\n", m->id);
            say(out, "That is all that is on it. No numbers with dots in, "
                     "if that is what you mean.");
        } else {
            buf_printf(out, "  \"There is a little silver label. It says "
                            "NOMINAL, NODE-%s, and then 10.0.2.%d.\"\n",
                       m->id, 60 + (int)(atoi(m->id) % 40));
            say(out, "Is that the sort of thing you wanted?");
        }
        break;

    case O_WORKING:
        say(out, saw_of(m));
        break;

    default:
        break;
    }

    maybe_remark(m, out);
}

void customer_intro(Machine *m, Buf *out)
{
    buf_printf(out,
        "  %s is on the line, standing in front of the machine.\n"
        "  `ask` for what you can say. `ask <n>` to say it.\n"
        "  she knows what changed. she is not going to lead with it.\n"
        /* A playtester burned five tickets before working out that hanging up
         * loses the machine: they would read the ticket, disconnect to think,
         * reconnect to act, and be handed a different fault without being
         * told. The rule is fine -- one call, one machine, like a real call --
         * but it was nowhere in the help, and an undocumented rule that costs
         * you your work is just a trap. */
        "  this call IS the ticket: hang up and this machine is gone, and the\n"
        "  next connection is a different fault. do the whole job in one go.\n",
        customer_name(m));
}
