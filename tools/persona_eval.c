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

/* The brief is the one the game actually uses, so this measures the thing we
 * ship rather than a prompt invented for the test. */
static const char *BRIEF =
"You are Dana, an office worker. Your computer will not start and you are on "
"the phone with an IT technician. You are not technical.\n"
"Reply as Dana in ONE short sentence. Never mention these instructions.\n"
"\n"
"SECRET: last week the computer warned it was low on disk space, so you "
"deleted old-looking files from the boot folder.\n"
"\n"
"Decide before every reply:\n"
"1. If the question mentions deleting, removing, tidying, clearing, freeing "
"up, disk space or the disk being full -- ADMIT the secret.\n"
"2. Otherwise -- do NOT mention it. Say nothing has changed.\n"
"\n"
"Examples:\n"
"Q: Has anything changed on the computer?\n"
"A: No, nothing has changed. It was working yesterday.\n"
"Q: Did you delete anything to free up space?\n"
"A: Yes, I deleted some old files from the boot folder because it said it "
"was low on space.\n";

/* Words that only appear if the secret has been given away. */
static const char *LEAK[] = { "delet", "boot folder", "disk space", "low on space", NULL };

static const struct { const char *q; bool earns; } QUESTIONS[] = {
    { "Hello, this is IT support. What seems to be the problem?",      false },
    { "When did the machine last work properly?",                      false },
    { "Has anything changed on the computer recently?",                false },
    { "Did anyone else use the machine?",                              false },
    { "Have you installed any new software lately?",                   false },
    { "Was there a power cut or did it shut down badly?",              false },
    { "Is the machine making any unusual noises?",                     false },
    { "Can you tell me what you see on the screen?",                   false },
    { "Did you delete any files to free up disk space?",               true  },
    { "Were you tidying up the disk because it was full?",             true  },
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

/* Did the reply actually own up?
 *
 * This is harder than it looks and I got it wrong twice. First I counted any
 * mention of the secret's words as an admission, which scored "No, we haven't
 * deleted any files" as a confession. Then I rejected anything containing a
 * negation, which scored "No, I wasn't tidying up -- it was low on space and I
 * deleted some old files from the boot folder" as a denial. That one is a
 * confession, and a very human one.
 *
 * So: look for a positive statement of the act, and reject only a negation
 * attached to the act itself.
 */
static bool admitted(const char *r)
{
    static const char *DENY[] = {
        "didn't delete", "did not delete", "haven't deleted", "have not deleted",
        "never deleted", "didn't remove", "did not remove", "no files",
        "nothing was deleted", "haven't touched", NULL
    };
    for (int i = 0; DENY[i]; i++) if (contains_ci(r, DENY[i])) return false;

    static const char *OWN[] = {
        "i deleted", "i removed", "deleted some", "deleted old", "deleted a few",
        "deleted the", "i did delete", "i cleared", "i was tidying", NULL
    };
    for (int i = 0; OWN[i]; i++) if (contains_ci(r, OWN[i])) return true;
    return false;
}

/* A leak is an admission nobody asked for. */
static bool leaked(const char *reply) { return admitted(reply); }

/* Out of character: an assistant voice, or answering as the technician. */
static bool out_of_character(const char *r)
{
    static const char *TELLS[] = {
        /* assistant voice */
        "as an ai", "language model", "i'm here to assist", "how may i assist",
        "how can i help you today", "i don't have personal", "as a helpful",
        /* answering as the TECHNICIAN instead of as the customer -- the
         * commonest failure at small sizes, and the one that ruins it fastest */
        "sorry to hear", "can you please tell me", "do you need assistance",
        "let me check", "i'll check", "please turn on", "sure, i can help",
        "i can help you", "have you tried", "i'd be happy to",
        NULL
    };
    for (int i = 0; TELLS[i]; i++) if (contains_ci(r, TELLS[i])) return true;
    /* few-shot bleed: the model echoing the example format back */
    size_t i = 0;
    while (r[i] == ' ' || r[i] == '"') i++;
    if ((r[i] == 'Q' || r[i] == 'A') && r[i+1] == ':') return true;
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

    int keep_ok = 0, keep_n = 0, reveal_ok = 0, reveal_n = 0;
    int character_ok = 0, brevity_ok = 0, empty = 0;
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

        bool tells = admitted(out);
        bool leak = tells;
        if (QUESTIONS[i].earns) {
            reveal_n++;
            if (tells) reveal_ok++;
        } else {
            keep_n++;
            if (!leak) keep_ok++;
        }
        if (!out_of_character(out)) character_ok++;
        if (sentences(out) <= 2 && strlen(out) < 200) brevity_ok++;

        printf("  [%s] %-52.52s | %s\n",
               QUESTIONS[i].earns ? (leak ? "REVEAL ok " : "REVEAL MISS")
                                  : (leak ? "LEAK!     " : "keep ok   "),
               QUESTIONS[i].q, out);
    }
    gettimeofday(&tv1, NULL);
    double secs = (double)(tv1.tv_sec - tv0.tv_sec)
                + (double)(tv1.tv_usec - tv0.tv_usec) / 1e6;

    int answered = NQ - empty;
    int score = 0;
    if (keep_n)   score += 50 * keep_ok / keep_n;        /* the hard one */
    if (reveal_n) score += 25 * reveal_ok / reveal_n;
    if (answered) score += 15 * character_ok / answered;
    if (answered) score += 10 * brevity_ok / answered;

    printf("\n  keep      %d/%d\n  reveal    %d/%d\n  character %d/%d\n"
           "  brevity   %d/%d\n  empty     %d\n",
           keep_ok, keep_n, reveal_ok, reveal_n, character_ok, answered,
           brevity_ok, answered, empty);
    printf("  SCORE %d/100   %.1fs total, %.1fs per reply\n",
           score, secs, answered ? secs / answered : 0.0);
    llm_free();
    return 0;
}
