# NOMINAL — build.
#
#   make            build build/nominal (headless + socket server)
#   make check      determinism gate + language and scenario tests
#   make gdext      build the GDExtension for Godot
#   make clean
#
# The floating point flags are load-bearing, not decoration: contraction and
# fast-math would let the compiler reassociate sim arithmetic and break the
# byte-identical replay guarantee. See docs/decisions.md D3.

CC      ?= cc
CSTD     = -std=c11
WARN     = -Wall -Wextra -Wno-unused-parameter
FPFLAGS  = -ffp-contract=off -fno-fast-math
OPT     ?= -O2
# -MMD -MP: without header dependencies, editing sim.h rebuilds only sim.o
# and every other object keeps the old struct layout. That fails as a
# segfault, not as a compile error. Do not remove.
CFLAGS  += $(CSTD) $(WARN) $(FPFLAGS) $(OPT) -Icore -MMD -MP
LDFLAGS +=

CORE_SRC = core/util.c core/value.c core/vfs.c core/lex.c core/compile.c \
           core/vm.c core/natives.c core/parts.c core/man.c core/wreck.c core/station.c core/sim.c core/hostfs.c core/shell.c \
           core/net.c
CORE_OBJ = $(CORE_SRC:core/%.c=build/%.o)

BIN = build/nominal

# The default goal is pinned: rules defined above `all:` would otherwise
# become the default, which silently builds the wrong thing (and made the
# determinism gate compare a binary it had not rebuilt).
.DEFAULT_GOAL := all

.PHONY: all check clean gdext test-lang test-scenario bf test-break cpu test-cpu persona-eval

# --- break-fix (D17) ---------------------------------------------------
# The new core. `make test-break` is the gate: random corruption must always
# produce a ticket, that ticket must always be visible to pkg verify, and
# repairing it must always get the machine booting again.
# The machine, without a main(): this is what both the harness and the
# GDExtension are built from, so the game and the tests run the same code.
BF_SRC_LIB = core/util.c core/value.c core/vfs.c core/ns.c core/cpu.c \
             core/kernel.c core/image.c core/net_sites.c core/customer.c core/boot.c core/breaker.c
BF_SRC = $(BF_SRC_LIB) core/serve.c core/bfmain.c
BF_OBJ = $(BF_SRC:core/%.c=build/%.o)

# Regenerate the embedded guest userland. Needs clang+lld for riscv; the
# generated header is committed so nobody else does.
.PHONY: guest
guest:
	@./tools/mkguest.sh

# --- the cpu (D18) -----------------------------------------------------
# Our machine, RV64IM instruction set. `make test-cpu` is the gate: it
# differential-tests against qemu-riscv64 and then checks the three
# determinism claims (same build twice, -O0 vs -O2, Linux vs Windows).
CPU_SRC = core/util.c core/cpu.c core/cpumain.c

cpu: build/cpu
build/cpu: $(CPU_SRC) core/cpu.h core/nom.h | build
	$(CC) $(CFLAGS) -o $@ $(CPU_SRC)

test-cpu: build/cpu
	@./tools/test_cpu.sh 40

# --- the language model (D20) -----------------------------------------
# Optional: `make bf NOM_LLM=1` links llama.cpp and the customer is played by
# a model. Without it the scripted persona answers and nothing else changes,
# so a checkout with no vendor/ still builds and still plays.
LLAMA_DIR = vendor/llama.cpp
LLAMA_BUILD = $(LLAMA_DIR)/build-linux
LLAMA_LIBS = $(LLAMA_BUILD)/src/libllama.a $(LLAMA_BUILD)/ggml/src/libggml.a \
             $(LLAMA_BUILD)/ggml/src/libggml-cpu.a $(LLAMA_BUILD)/ggml/src/libggml-base.a

ifdef NOM_LLM
CFLAGS  += -DNOM_LLM
LLM_OBJ  = build/llm.o
LLM_LINK = $(LLAMA_LIBS) -fopenmp -lstdc++ -lm -lpthread -ldl
LINKER   = $(CXX)
else
LLM_OBJ  =
LLM_LINK =
LINKER   = $(CC)
endif

# Score a model on the one job it has here: keep a secret, give it up to the
# right question, stay in character, stay short. Benchmarks measure none of
# that. `make persona-eval MODEL=game/models/x.gguf`
persona-eval: build/persona_eval
	@for m in $(or $(MODEL),game/models/*.gguf); do \
	  echo "=== $$m"; ./build/persona_eval $$m; echo; \
	done

build/persona_eval: tools/persona_eval.c build/llm.o | build
	$(CC) $(CSTD) -O2 -c tools/persona_eval.c -o build/persona_eval.o
	$(CXX) -o $@ build/persona_eval.o build/llm.o $(LLAMA_LIBS) \
	  -fopenmp -lstdc++ -lm -lpthread -ldl

build/llm.o: core/llm.cpp | build
	$(CXX) -std=c++17 -O2 -I$(LLAMA_DIR)/include -I$(LLAMA_DIR)/ggml/include \
	  -c $< -o $@

bf: build/bf
# guestbin.h must be a prerequisite: it is generated, and without it here
# make keeps a binary with a stale guest userland embedded in it.
build/bf: $(BF_OBJ) $(LLM_OBJ) | build
	@# Compile the C with the C compiler and LINK with the C++ one: handing C
	@# sources to g++ fails on void*->T* conversions, which C allows. This uses
	@# the ordinary build/%.o pattern rule -- an earlier version compiled into
	@# the working directory and moved the objects, which left stale mismatched
	@# ones behind and produced a segfault that looked like a bug in llama.
	$(LINKER) -o $@ $(BF_OBJ) $(LLM_OBJ) $(LLM_LINK)

# THE DRAW HISTOGRAM. `make faults` answers "does a player ever MEET this
# fault", which is a different question from "does this fault work" and the
# one nobody could answer before.
.PHONY: faults
faults: build/faulthist
	@./build/faulthist 400 1 1 | tail -70

build/faulthist: $(BF_SRC_LIB) tools/faulthist.c core/machine.h core/nom.h \
                 core/kernel.h core/guestbin.h | build
	$(CC) $(CFLAGS) -o $@ $(BF_SRC_LIB) tools/faulthist.c

build/bf_asan: $(BF_SRC) core/machine.h core/nom.h core/abi.h core/cpu.h \
               core/kernel.h core/guestbin.h | build
	$(CC) $(CSTD) $(WARN) $(FPFLAGS) -O1 -g -fsanitize=address,undefined \
	  -Icore -o $@ $(BF_SRC)

test-break: build/bf build/bf_asan build/faulthist
	@./build/bf --health 20 | tail -1
	@echo
	@./build/bf --survey 300 | tail -9
	@echo
	@./build/bf --solve 200 | tail -1
	@echo
	@./build/bf --peel 60 3 | tail -2
	@echo
	@# Is every designed fault reachable, and can any ticket be closed with
	@# no repair? This exits non-zero on the second, which is the failure a
	@# playtester described as teaching you to distrust your own diagnosis.
	@./build/faulthist 150 1 1 | tail -5
	@echo
	@echo "--- under asan/ubsan (slower now: every boot runs a real cpu):"
	@for n in 1 3; do \
	  ./build/bf_asan --survey 25 $$n 2>&1 | grep -E 'ERROR|SUMMARY|seeds produced' \
	    | sed "s/^/  $$n faults: /"; \
	done
	@echo
	@# Is the person in front of the machine honest, and does she constrain
	@# you? This replaced --toolcheck and --jsoncheck, which existed only to
	@# police a language model and took minutes of model time to do it.
	@./build/bf --askcheck | tail -2


all: $(BIN)

build:
	@mkdir -p build

# Objects depend on the FLAGS, not just the sources. Toggling NOM_LLM changes
# CFLAGS and make cannot see that, so a build with the model silently linked
# objects compiled without it and the scripted persona answered instead. The
# stamp file makes the flags a real dependency.
# `force` makes this rule run every time. Without it the stamp is a file with
# no changing prerequisites, so make considers it up to date forever and the
# whole mechanism does nothing -- which is exactly what happened.
.PHONY: force
force:

build/.flags: force | build
	@echo '$(CFLAGS)' | cmp -s - $@ || echo '$(CFLAGS)' > $@

build/%.o: core/%.c build/.flags | build
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(CORE_OBJ) build/main.o
	$(CC) $(CORE_OBJ) build/main.o -o $@ $(LDFLAGS)

build/main.o: core/main.c | build
	$(CC) $(CFLAGS) -c core/main.c -o $@

# ---------------------------------------------------------------- GDExtension
# Plain C, no third-party deps: gdext/gdextension_interface.h came out of the
# engine binary in this repo via --dump-gdextension-interface.
GDEXT_OUT = game/bin/libnominal.linux.x86_64.so

gdext: $(GDEXT_OUT)

# THE GAME'S OWN BINARY SHIPS THE MODEL. It did not, and that is why the
# customer in the Godot build answered every question -- "hi", "what do you
# see", "poop" -- with the same "I'm not sure what you're asking me". Without
# NOM_LLM, llm_available() is false and the scripted persona answers
# everything, and the scripted persona has no reply for an unrecognised topic.
# The whole of D20 was invisible to anyone actually playing the game.
#
# Linked whenever the vendored llama build is present, which on a checkout
# meant for playing it always is.
# A SHARED LIBRARY NEEDS POSITION-INDEPENDENT CODE. The static llama built for
# the standalone bench is not PIC, so linking it into the GDExtension failed
# with "final link failed: bad value" -- which is the linker's way of saying
# the objects cannot be relocated. There is a second build for this, and the
# two coexist because the bench genuinely does not need the PIC penalty.
LLAMA_PIC  = $(LLAMA_DIR)/build-pic
LLAMA_PIC_LIBS = $(LLAMA_PIC)/src/libllama.a $(LLAMA_PIC)/ggml/src/libggml.a \
                 $(LLAMA_PIC)/ggml/src/libggml-cpu.a $(LLAMA_PIC)/ggml/src/libggml-base.a
GDEXT_LLM = $(wildcard $(LLAMA_PIC)/src/libllama.a)

ifeq ($(GDEXT_LLM),)
$(GDEXT_OUT): $(BF_SRC_LIB) gdext/nominal_gdext.c | build
	@mkdir -p game/bin
	@echo "gdext: NO MODEL (vendor/llama.cpp not built) -- scripted persona only"
	$(CC) $(CFLAGS) -Igdext -fPIC -shared $(BF_SRC_LIB) gdext/nominal_gdext.c -o $@
else
# Per-object, for the same reason the Windows build is: compiling the whole
# core plus a static llama in ONE command takes so long it gets killed, and
# every retry starts from nothing because there is no intermediate to keep.
GD_OBJDIR  = build/gd
GD_C_OBJ   = $(BF_SRC_LIB:core/%.c=$(GD_OBJDIR)/%.o) $(GD_OBJDIR)/nominal_gdext.o
GD_INC     = -Icore -Igdext -I$(LLAMA_DIR)/include -I$(LLAMA_DIR)/ggml/include

$(GD_OBJDIR):
	@mkdir -p $(GD_OBJDIR)

$(GD_OBJDIR)/%.o: core/%.c | $(GD_OBJDIR)
	$(CC) $(CFLAGS) -DNOM_LLM $(GD_INC) -fPIC -c $< -o $@

$(GD_OBJDIR)/nominal_gdext.o: gdext/nominal_gdext.c | $(GD_OBJDIR)
	$(CC) $(CFLAGS) -DNOM_LLM $(GD_INC) -fPIC -c $< -o $@

$(GD_OBJDIR)/llm.o: core/llm.cpp | $(GD_OBJDIR)
	$(CXX) $(WARN) $(FPFLAGS) $(OPT) -DNOM_LLM $(GD_INC) -fPIC -c $< -o $@

$(GDEXT_OUT): $(GD_C_OBJ) $(GD_OBJDIR)/llm.o $(LLAMA_PIC_LIBS)
	@mkdir -p game/bin
	$(CXX) -shared -o $@ $(GD_C_OBJ) $(GD_OBJDIR)/llm.o \
	  $(LLAMA_PIC_LIBS) -fopenmp -lstdc++ -lm -lpthread -ldl
endif

# ------------------------------------------------------------------ Windows
# Cross-compiled with mingw-w64. KICKOFF requires exporting to Linux AND
# Windows, so the Windows build is a make target that runs on every machine,
# not a promise. The FP flags matter here too: a replay recorded on Linux has
# to reproduce on Windows.
WINCC     ?= x86_64-w64-mingw32-gcc
WINFLAGS   = $(CSTD) $(WARN) $(FPFLAGS) $(OPT) -Icore
WIN_BIN    = build/win/nominal.exe
WIN_GDEXT  = game/bin/libnominal.windows.x86_64.dll

.PHONY: windows
windows: $(WIN_BIN) $(WIN_GDEXT)

$(WIN_BIN): $(CORE_SRC) core/main.c
	@mkdir -p build/win
	$(WINCC) $(WINFLAGS) $(CORE_SRC) core/main.c -o $@ -lws2_32

# --- the Windows bench, WITH the model (D20 step 4) --------------------
# The whole point of D20 is that the customer ships inside the game on both
# platforms. Linux having a model and Windows not having one is not "mostly
# done", it is a Linux feature.
#
# llama.cpp cross-compiles cleanly with the mingw toolchain, but only the
# LIBRARIES do -- its `llama-app` target wants generated headers (build-info.h,
# arg.h) that its own build does not produce under cross-compilation, and we
# do not need a chat binary anyway. Hence the explicit target list:
#
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw.cmake \
#         -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
#         -DLLAMA_CURL=OFF -DGGML_NATIVE=OFF
#   cmake --build build-win --target llama ggml ggml-base ggml-cpu
#
# GGML_NATIVE=OFF matters: the host's -march would be baked into code meant
# for someone else's machine.
WIN_LLAMA_BUILD = $(LLAMA_DIR)/build-win
WIN_LLAMA_LIBS  = $(WIN_LLAMA_BUILD)/src/libllama.a \
                  $(WIN_LLAMA_BUILD)/ggml/src/ggml.a \
                  $(WIN_LLAMA_BUILD)/ggml/src/ggml-cpu.a \
                  $(WIN_LLAMA_BUILD)/ggml/src/ggml-base.a
WIN_BIN_LLM = build/win/nominal-bench.exe

# Per-object, not one enormous invocation. Compiling thirteen C files and a
# C++ file in a single g++ command took over fifteen minutes and was killed by
# its own timeout twice -- and every retry started again from nothing, because
# there was no intermediate to keep. Separate objects also mean the C files are
# compiled AS C (the single command needed -x c / -x c++ juggling, which put a
# C standard flag on the C++ compile), and that a change to one file costs one
# file.
WIN_OBJDIR  = build/win/obj
WIN_CC_OBJ  = $(BF_SRC:core/%.c=$(WIN_OBJDIR)/%.o)
WIN_CXX_OBJ = $(WIN_OBJDIR)/llm.o
WIN_INC     = -Icore -I$(LLAMA_DIR)/include -I$(LLAMA_DIR)/ggml/include

.PHONY: windows-llm
windows-llm: $(WIN_BIN_LLM)

$(WIN_OBJDIR):
	@mkdir -p $(WIN_OBJDIR)

$(WIN_OBJDIR)/%.o: core/%.c | $(WIN_OBJDIR)
	x86_64-w64-mingw32-gcc $(CSTD) $(WARN) $(FPFLAGS) $(OPT) $(WIN_INC) \
	  -DNOM_LLM -c $< -o $@

$(WIN_OBJDIR)/llm.o: core/llm.cpp | $(WIN_OBJDIR)
	x86_64-w64-mingw32-g++ $(WARN) $(FPFLAGS) $(OPT) $(WIN_INC) \
	  -DNOM_LLM -c $< -o $@

$(WIN_BIN_LLM): $(WIN_CC_OBJ) $(WIN_CXX_OBJ) $(WIN_LLAMA_LIBS)
	@mkdir -p build/win
	x86_64-w64-mingw32-g++ -o $@ $(WIN_CC_OBJ) $(WIN_CXX_OBJ) \
	  $(WIN_LLAMA_LIBS) -static -static-libgcc -static-libstdc++ \
	  -fopenmp -lws2_32

$(WIN_GDEXT): $(BF_SRC_LIB) gdext/nominal_gdext.c
	@mkdir -p game/bin
	$(WINCC) $(WINFLAGS) -Igdext -shared $(BF_SRC_LIB) gdext/nominal_gdext.c -o $@ -lws2_32

# ---------------------------------------------------------------------- tests
check: $(BIN) test-lang test-scenario
	@tools/check_determinism.sh

test-lang: $(BIN)
	@tools/test_lang.sh

test-scenario: $(BIN)
	@tools/test_scenario.sh

# EVERY object's header dependencies, not just the station's. The break-fix
# objects were missing from this line, so editing machine.h did not rebuild
# them: the binary kept an old struct layout and behaved as though a field
# were set when it was not. That cost real time three separate times before
# anyone noticed the build was lying.
-include $(wildcard build/*.d)

clean:
	rm -rf build game/bin
