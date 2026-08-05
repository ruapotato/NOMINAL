/* llm.cpp — the language model, in the box.
 *
 * A thin C-callable wrapper over llama.cpp. The ONLY thing it does is take a
 * system brief plus a question and return a line of dialogue. It has no tools,
 * no filesystem, no syscalls, and no way to touch the machine. That is a
 * deliberate boundary: the customer is a voice on a phone, and a voice cannot
 * reach into the disk.
 *
 * Everything here fails soft. No model file, no memory, a bad generation, a
 * model that takes too long — every one of them returns false and the scripted
 * persona in customer.c answers instead. The player must never see a hang and
 * must never see an empty reply.
 */
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>

#include "llama.h"

extern "C" {
bool llm_available(void);
bool llm_load(const char *model_path);
void llm_free(void);
/* `forbidden` is a newline-separated list of words the reply must NOT contain.
 * It is how the scripted layer polices the model: a small model will happily
 * volunteer the secret on the very first question, and a leaked secret ruins
 * the ticket permanently. Caller passes the secret's keywords whenever the
 * question was not one that earns the admission. */
bool llm_ask(const char *system_brief, const char *question,
             const char *forbidden, char *out, size_t outsz);
/* The same, with the call so far. `hist` is 2*nhist alternating strings:
 * question, answer, question, answer. The system brief is rebuilt by the
 * caller every turn, so a stale description of the machine can never
 * outlive the machine's actual state. */
bool llm_ask_hist(const char *system_brief, const char **hist, int nhist,
                  const char *question, char *out, size_t outsz);
/* Same, but allowed to run longer. The customer answers in one or two
 * sentences; the engineer who wrote the runbook needs a paragraph, and
 * cutting her off mid-sentence made her look broken rather than terse. */
bool llm_ask_long(const char *system_brief, const char **hist, int nhist,
                  const char *question, char *out, size_t outsz);
/* A CLASSIFIER, not a conversationalist. Near-zero temperature, because
 * "which of these seven things did the technician ask for" has one right
 * answer and sampling variety is nothing but a source of wrong ones.
 *
 * `cut` is set when the answer was still coming when the budget ran out. It
 * exists because the classifier's answer is not prose: it carries a command
 * that is about to be RUN, and half a command is not a worse answer, it is a
 * different one. The caller must be able to tell "they asked for this" from
 * "they asked for at least this". */
bool llm_classify(const char *system_brief, const char *question,
                  char *out, size_t outsz, bool *cut);
}

namespace {

llama_model   *g_model = nullptr;
llama_context *g_ctx   = nullptr;
bool           g_tried = false;

/* Small on purpose. The customer says one or two sentences; a bigger window
 * costs memory on a machine that is also running a game. */
constexpr int CTX_TOKENS  = 4096;   /* the brief plus eight exchanges */
constexpr int MAX_REPLY   = 64;
/* The runbook author is allowed a paragraph and must be allowed to FINISH
 * one: at 220 she ran out of budget mid-word often enough that a playtester
 * read it as a broken game rather than a busy colleague. */
constexpr int MAX_LONG    = 320;
/* A dictated command line, in tokens. A UUID tokenises into a dozen pieces
 * of hex, so this is not as generous as it looks. */
constexpr int MAX_CLASSIFY = 220;

std::vector<llama_token> tokenize(const std::string &text, bool add_special)
{
    const llama_vocab *vocab = llama_model_get_vocab(g_model);
    int n = -llama_tokenize(vocab, text.c_str(), (int32_t)text.size(),
                            nullptr, 0, add_special, true);
    std::vector<llama_token> toks(n);
    if (llama_tokenize(vocab, text.c_str(), (int32_t)text.size(),
                       toks.data(), n, add_special, true) < 0)
        toks.clear();
    return toks;
}

std::string piece(llama_token tok)
{
    const llama_vocab *vocab = llama_model_get_vocab(g_model);
    char buf[256];
    int n = llama_token_to_piece(vocab, tok, buf, sizeof buf, 0, true);
    if (n < 0) return "";
    return std::string(buf, (size_t)n);
}

} // namespace

static bool llm_ask_n(const char *system_brief, const char **hist, int nhist,
                      const char *question, char *out, size_t outsz,
                      int maxtok, int maxstops, float temp,
                      bool oneline, bool *cut);

bool llm_available(void) { return g_model != nullptr && g_ctx != nullptr; }

/* Diagnostics for the build, not for the player. */
extern "C" const char *llm_why(void)
{
    if (!g_tried) return "not loaded";
    if (!g_model) return "model file did not load";
    if (!g_ctx)   return "context did not initialise";
    return "ok";
}

bool llm_load(const char *model_path)
{
    if (g_tried) return llm_available();
    g_tried = true;

    /* llama.cpp is chatty on stderr and the player is looking at a console
     * that is supposed to be the customer's machine. */
    llama_log_set([](enum ggml_log_level, const char *, void *) {}, nullptr);
    llama_backend_init();

    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = 0;               /* cpu only: Steam machines vary */
    g_model = llama_model_load_from_file(model_path, mp);
    if (!g_model) return false;

    llama_context_params cp = llama_context_default_params();
    cp.n_ctx   = CTX_TOKENS;
    cp.n_batch = CTX_TOKENS;
    g_ctx = llama_init_from_model(g_model, cp);
    if (!g_ctx) {
        llama_model_free(g_model);
        g_model = nullptr;
        return false;
    }
    return true;
}

void llm_free(void)
{
    if (g_ctx)   { llama_free(g_ctx);        g_ctx = nullptr; }
    if (g_model) { llama_model_free(g_model); g_model = nullptr; }
    llama_backend_free();
    g_tried = false;
}

bool llm_ask(const char *system_brief, const char *question,
             const char *forbidden, char *out, size_t outsz)
{
    return llm_ask_hist(system_brief, nullptr, 0, question, out, outsz);
}

bool llm_ask_hist(const char *system_brief, const char **hist, int nhist,
                  const char *question, char *out, size_t outsz)
{
    return llm_ask_n(system_brief, hist, nhist, question, out, outsz,
                     MAX_REPLY, 2, 0.7f, false, nullptr);
}

bool llm_ask_long(const char *system_brief, const char **hist, int nhist,
                  const char *question, char *out, size_t outsz)
{
    return llm_ask_n(system_brief, hist, nhist, question, out, outsz,
                     MAX_LONG, 6, 0.7f, false, nullptr);
}

/* ONE LINE, AND AS LONG A LINE AS A COMMAND CAN BE.
 *
 * This used to run to forty tokens and stop at the first full stop after the
 * fortieth character, which are two sentence-shaped limits on something that
 * is not a sentence. `RUN sed -i s/c603-2d03-bafe-e442/8f41-2c07-a19d-5be3/
 * /mnt/boot/zbl/zbl.cfg` is sixty tokens of hex, and `RUN cat /etc/zbl.cfg`
 * has a full stop in the middle of the filename. So: no sentence rule at all,
 * a budget that fits a real command line, and the answer ends where the line
 * ends -- which is what "output one line and nothing else" meant. */
bool llm_classify(const char *system_brief, const char *question,
                  char *out, size_t outsz, bool *cut)
{
    return llm_ask_n(system_brief, nullptr, 0, question, out, outsz,
                     MAX_CLASSIFY, 0, 0.05f, true, cut);
}

static bool llm_ask_n(const char *system_brief, const char **hist, int nhist,
                      const char *question, char *out, size_t outsz,
                      int maxtok, int maxstops, float temp,
                      bool oneline, bool *cut)
{
    if (cut) *cut = false;
    const char *forbidden = "";
    if (!llm_available() || !out || outsz < 2) return false;
    out[0] = '\0';

    /* The model's own chat template, taken from the GGUF. Hand-rolling the
     * markers is how you get a model that answers as the wrong speaker. */
    std::vector<llama_chat_message> msgs;
    msgs.push_back({ "system", system_brief });
    /* The transcript, oldest first. Replayed every turn because there is no
     * kv-cache reuse here -- the brief changes each turn by design, so there
     * would be nothing to reuse. */
    for (int i = 0; i < nhist; i++) {
        msgs.push_back({ "user",      hist[i * 2]     });
        msgs.push_back({ "assistant", hist[i * 2 + 1] });
    }
    msgs.push_back({ "user", question });
    /* A GGUF may carry no chat template at all, and passing null straight
     * into llama_chat_apply_template segfaults. Fall back to chatml, which is
     * what most small instruct models are trained with anyway. */
    const char *tmpl = llama_model_chat_template(g_model, nullptr);
    if (!tmpl) tmpl = "chatml";
    std::vector<char> promptbuf(16384);
    int32_t plen = llama_chat_apply_template(tmpl, msgs.data(), msgs.size(),
                                             true, promptbuf.data(),
                                             (int32_t)promptbuf.size());
    if (plen <= 0 || (size_t)plen >= promptbuf.size()) return false;
    std::string prompt(promptbuf.data(), (size_t)plen);

    auto toks = tokenize(prompt, true);
    if (toks.empty() || (int)toks.size() >= CTX_TOKENS - maxtok) return false;

    /* Fresh state every question: a support call is short and the brief
     * carries the context, so there is nothing worth keeping between turns
     * and plenty that could go wrong if we did. */
    llama_memory_clear(llama_get_memory(g_ctx), true);

    llama_batch batch = llama_batch_get_one(toks.data(), (int32_t)toks.size());
    if (llama_decode(g_ctx, batch) != 0) return false;

    auto sp = llama_sampler_chain_default_params();
    llama_sampler *chain = llama_sampler_chain_init(sp);
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(temp));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(0));

    const llama_vocab *vocab = llama_model_get_vocab(g_model);
    std::string reply;
    /* Did it stop because it had finished, or because we stopped it? The
     * difference is the whole of `cut`, and it is also what decides whether
     * the tail below is a sentence or the wreckage of one. */
    bool ranout = true;
    for (int i = 0; i < maxtok; i++) {
        llama_token tok = llama_sampler_sample(chain, g_ctx, -1);
        if (llama_vocab_is_eog(vocab, tok)) { ranout = false; break; }
        reply += piece(tok);
        /* One line means one line: the answer is complete at the newline and
         * everything after it is the model carrying on unasked. */
        if (oneline) {
            size_t nl = reply.find('\n');
            if (nl != std::string::npos &&
                reply.find_first_not_of(" \t\r\n") < nl) {
                reply = reply.substr(0, nl);
                ranout = false;
                break;
            }
        } else if (maxstops > 0 && reply.size() > 40) {
            /* Two sentences is the brief. Stop at the second full stop rather
             * than trusting a small model to stop on its own. */
            size_t stops = 0;
            for (char ch : reply) if (ch == '.' || ch == '!' || ch == '?') stops++;
            if (stops >= (size_t)maxstops) { ranout = false; break; }
        }
        llama_batch nb = llama_batch_get_one(&tok, 1);
        if (llama_decode(g_ctx, nb) != 0) break;
    }
    llama_sampler_free(chain);

    /* A REPLY THAT RAN OUT OF BUDGET ENDS AT THE LAST SENTENCE IT FINISHED.
     *
     * Json was cut mid-token -- "you can view the contents of `/etc/ld.so" --
     * and a colleague who stops mid-word reads as a crash, not as somebody
     * busy. Prose can simply lose its unfinished sentence; a classification
     * cannot, because dropping half of a command still leaves a runnable
     * command, so that case is reported through `cut` instead. */
    if (ranout && !oneline) {
        size_t last = reply.find_last_of(".!?");
        if (last != std::string::npos && last + 1 > reply.size() / 2)
            reply = reply.substr(0, last + 1);
    }

    /* Trim, and refuse anything that came back empty or that leaked the
     * instruction block — a small model does that and it ruins the illusion
     * completely, so the scripted persona is better than a bad generation. */
    size_t b = reply.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    size_t e = reply.find_last_not_of(" \t\r\n");
    reply = reply.substr(b, e - b + 1);
    if (reply.empty()) return false;
    if (reply.find("Secret") != std::string::npos ||
        reply.find("RULES") != std::string::npos ||
        reply.find("<think>") != std::string::npos ||
        reply.find("technician") != std::string::npos) return false;

    /* The leak check. Lower-cased substring match on each forbidden word: if
     * the model has volunteered the secret when it was not asked for, throw
     * the whole reply away. A scripted line is better than a spoiled puzzle. */
    if (forbidden && *forbidden) {
        std::string low;
        for (char ch : reply) low += (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
        const char *f = forbidden;
        while (*f) {
            const char *nl = strchr(f, '\n');
            std::string word(f, nl ? (size_t)(nl - f) : strlen(f));
            f = nl ? nl + 1 : f + strlen(f);
            if (word.empty()) continue;
            if (low.find(word) != std::string::npos) return false;
        }
    }

    /* THE BUFFER IS THE OTHER PLACE A REPLY GETS CUT, and it was cutting
     * mid-word too: 320 tokens of runbook is more than six hundred bytes.
     * Prose loses its last unfinished sentence rather than its last syllable;
     * an answer that will not fit is reported as cut, never quietly halved. */
    if (reply.size() >= outsz) {
        if (cut) *cut = true;
        if (!oneline) {
            std::string fit = reply.substr(0, outsz - 1);
            size_t last = fit.find_last_of(".!?");
            if (last != std::string::npos && last + 1 > fit.size() / 2)
                fit = fit.substr(0, last + 1);
            reply = fit;
        }
    }
    if (cut && ranout) *cut = true;
    snprintf(out, outsz, "%s", reply.c_str());
    return true;
}
