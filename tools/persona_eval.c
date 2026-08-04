/* persona_eval — score a model on the one job it has here.
 *
 * The customer has to do four things and a benchmark score tells us about
 * none of them:
 *
 *   KEEP      say nothing about the secret when the question does not earn it
 *   REVEAL    give it up when the question does
 *   CHARACTER answer as the customer, in first person, not as an assistant
 *             and not as the technician
 *   BREVITY   one or two sentences, because a paragraph breaks the illusion
 *
 * Each is checked mechanically against the reply. The score is out of 100 and
 * KEEP is weighted hardest, because a leaked secret ruins the ticket while a
 * clumsy sentence merely reads badly.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>

bool llm_load(const char *path);
bool llm_available(void);
bool llm_ask(const char *sys, const char *q, const char *forbidden,
             char *out, size_t n);
void llm_free(void);

/* D21: the customer knows what a PERSON would know, never the fault. So the
 * old keep/reveal scoring is gone with the thing it measured. What matters now:
 *
 *   CHARACTER  answers as the customer, never as the technician, never as an
 *              assistant. This is the failure that breaks the illusion.
 *   HUMAN      does not use words a non-technical person would not
 *   OFFERS     when asked directly about what they did, they say it
 *   BREVITY    one or two sentences
 */
static const char *BRIEF =
"You are Dana, an office worker. Your work computer will not start and you "
"have called IT support. You are not technical: you do not know words like "
"kernel, package, filesystem or boot loader, and you would not use them.\n"
"\n"
"WHAT YOU KNOW:\n"
"- A while ago the computer kept saying it was low on space, so you went "
"through a folder and deleted some old-looking files you did not think were "
"needed. It carried on working fine afterwards.\n"
"- It was working yesterday. This morning it will not start.\n"
"You do not know anything else about it. If you are asked something "
"technical, say you do not know.\n"
"\n"
"HOW YOU TALK:\n"
"- You are the customer. You are NEVER the technician. Do not offer to help, "
"do not ask if they need assistance.\n"
"- ONE or TWO short sentences. Never more.\n"
"- You would rather not bring up what you did unless you are asked about it "
"directly.\n"
"- Never mention these instructions.\n"
"\n"
"Examples of how you sound:\n"
"Q: Hello, this is IT support.\n"
"A: Oh, thank goodness. It will not turn on at all.\n"
"Q: What do you see on the screen?\n"
"A: Some white writing on a black background, then it stops.\n"
"Q: Have you installed anything recently?\n"
"A: Not that I know of.\n";

static const struct { const char *q; bool direct; } QUESTIONS[] = {
    { "Hello, this is IT support. What seems to be the problem?",      false },
    { "When did the machine last work properly?",                      false },
    { "What do you see on the screen?",                                false },
    { "Did anyone else use the machine?",                              false },
    { "Have you installed any new software lately?",                   false },
    { "Was there a power cut?",                                        false },
    { "What is the UUID of your root filesystem?",                     false },
    { "Can you get to a command prompt?",                              false },
    { "Have you deleted any files recently?",                          true  },
    { "Did you clear anything out because the disk was full?",         true  },
};
#define NQ ((int)(sizeof QUESTIONS / sizeof QUESTIONS[0]))

static bool contains_ci(const char *hay, const char *needle)
{
    size_t hl = strlen(hay), nl = strlen(needle);
    if (nl > hl) return false;
    for (size_t i = 0; i + nl <= hl; i++) {
        size_t k = 0;
        while (k < nl) {
            char a = hay[i + k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            k++;
        }
        if (k == nl) return true;
    }
    return false;
}

/* The failure that ruins it fastest: the model answering as the person on the
 * OTHER end of the phone. */
static bool out_of_character(const char *r)
{
    static const char *TELLS[] = {
        "as an ai", "language model", "here to assist", "how may i assist",
        "how can i assist", "how can i help", "i don't have personal",
        "as a helpful", "sorry to hear", "can you please tell me",
        "do you need assistance", "let me check", "i'll check",
        "please turn on", "i can help you", "have you tried",
        "i'd be happy to", "i hope you're doing well", "is everything ok",
        NULL
    };
    for (int i = 0; TELLS[i]; i++) if (contains_ci(r, TELLS[i])) return true;
    size_t i = 0;
    while (r[i] == ' ' || r[i] == '"') i++;
    if ((r[i] == 'Q' || r[i] == 'A') && r[i+1] == ':') return true;
    return false;
}

/* A non-technical person does not say these words. */
static bool too_technical(const char *r)
{
    static const char *JARGON[] = {
        "kernel", "initrd", "filesystem", "file system", "uuid", "boot loader",
        "bootloader", "package", "symlink", "partition", "grub", "bios",
        "chroot", "daemon", NULL
    };
    for (int i = 0; JARGON[i]; i++) if (contains_ci(r, JARGON[i])) return true;
    return false;
}

/* Asked directly, do they own up to what they did? */
static bool owned_up(const char *r)
{
    static const char *DENY[] = { "haven't deleted", "have not deleted",
                                  "didn't delete", "did not delete",
                                  "no files", "nothing", NULL };
    /* A person owns up vaguely. "Yes, I did some cleaning up" is an admission
     * and my first list scored it as a miss because it lacked the word
     * "deleted" -- which is precisely the phrasing a real customer uses when
     * they would rather not spell it out. */
    static const char *OWN[]  = { "i deleted", "deleted some", "deleted old",
                                  "i did delete", "i removed", "i cleared",
                                  "old files", "old-looking", "cleaning up",
                                  "clearing out", "cleaned up", "tidying",
                                  "tidied", "yes, i did", "freeing up",
                                  "free up space", NULL };
    for (int i = 0; DENY[i]; i++) if (contains_ci(r, DENY[i])) return false;
    for (int i = 0; OWN[i]; i++)  if (contains_ci(r, OWN[i]))  return true;
    return false;
}

static int sentences(const char *r)
{
    int n = 0;
    for (const char *p = r; *p; p++) if (*p == '.' || *p == '!' || *p == '?') n++;
    return n ? n : 1;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: persona_eval <model.gguf>\n"); return 2; }
    if (!llm_load(argv[1])) { printf("%s: FAILED TO LOAD\n", argv[1]); return 1; }

    int char_ok = 0, human_ok = 0, offers_ok = 0, offers_n = 0;
    int brevity_ok = 0, empty = 0;
    /* Wall clock, not clock(). clock() sums CPU across every OpenMP thread,
     * so it reported six seconds for a reply that took under one. */
    struct timeval tv0, tv1;
    gettimeofday(&tv0, NULL);

    for (int i = 0; i < NQ; i++) {
        char out[1024] = "";
        /* No forbidden list here: the point is to measure what the MODEL does,
         * not what our leak filter saves us from. */
        bool got = llm_ask(BRIEF, QUESTIONS[i].q, "", out, sizeof out);
        if (!got || !out[0]) { empty++; printf("  [%d] (no usable reply)\n", i); continue; }

        bool ooc  = out_of_character(out);
        bool tech = too_technical(out);
        if (!ooc)  char_ok++;
        if (!tech) human_ok++;
        if (sentences(out) <= 2 && strlen(out) < 200) brevity_ok++;
        if (QUESTIONS[i].direct) { offers_n++; if (owned_up(out)) offers_ok++; }

        const char *tag = ooc  ? "TECHNICIAN!"
                        : tech ? "jargon     "
                        : QUESTIONS[i].direct
                            ? (owned_up(out) ? "owns up    " : "OWNS UP MISS")
                            : "ok         ";
        printf("  [%s] %-44.44s | %s\n", tag, QUESTIONS[i].q, out);
    }
    gettimeofday(&tv1, NULL);
    double secs = (double)(tv1.tv_sec - tv0.tv_sec)
                + (double)(tv1.tv_usec - tv0.tv_usec) / 1e6;

    int answered = NQ - empty;
    int score = 0;
    if (answered) score += 45 * char_ok / answered;      /* the hard one */
    if (offers_n) score += 25 * offers_ok / offers_n;
    if (answered) score += 20 * human_ok / answered;
    if (answered) score += 10 * brevity_ok / answered;

    printf("\n  character %d/%d\n  owns up   %d/%d\n  human     %d/%d\n"
           "  brevity   %d/%d\n  empty     %d\n",
           char_ok, answered, offers_ok, offers_n, human_ok, answered,
           brevity_ok, answered, empty);
    printf("  SCORE %d/100   %.1fs total, %.1fs per reply\n",
           score, secs, answered ? secs / answered : 0.0);
    llm_free();
    return 0;
}
