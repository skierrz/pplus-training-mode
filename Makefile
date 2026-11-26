# Compiler/linker flags and the elf -> rel steps are adapted from
# SyriingePluginTemplate Makefile @ 2b3ba22 (MIT, see THIRD_PARTY_NOTICES.md).
#
# The plugin file is the stub (src/stub/ and src/shared/), which Syriinge
# loads, packed together with the modules the stub loads for particular
# scenes: the main module (src/main/ and src/shared/, kind "match") and the
# extra module (src/extra/, kind "extra", linked against the main module)
# for Training matches, and the menu module (src/menu/, kind "menu") for
# Training's character select. See docs/building.md.
#
# Targets:
#   make          build build/TrainingMode.rel (stub + modules + table)
#   make clean    remove build/
#   make tools    populate tools/ (MWCC 4.2 + elf2rel), see scripts/fetch-tools.sh
#   make negative build the deliberately broken plugin files test-reject uses,
#                 and the padded one test-big-file uses
#   make sd-folder    extract the clean SD card image into SD_FOLDER (once)
#   make install      copy the plugin into SD_FOLDER's pf/plugins
#   make run          install, then start Dolphin booting Project+
#   make test-load    install, boot unattended: stub loaded, hooks in, no main module
#   make test-match   install, play into a Training match and out again: main
#                     module loaded, overlay on screen, unloaded, heap back to baseline
#   make test-stress  install, repeat Training matches (STRESS_ARGS), checking
#                     every load and unload
#   make test-reject KIND=badversion|corrupt|truncated
#                     install a broken plugin file, play a match, expect a clean
#                     rejection; puts the normal build back afterwards
#   make test-big-file    test-match with the plugin file padded by BIG_PAD bytes
#                     (1 MiB): module loads do not depend on the file's size
#   make test-lab     install, play a Training match driving the lab menu by
#                     scripted input: percents, lock, info rows, frame advance
#   make test-displays install, play scripted attacks in a Training match and
#                     check the trackers and frame advantage messages against the game
#   make test-osds    install, play scripted techniques in a Training match and
#                     check the technique messages, Ledgedash HUD, colour overlays
#                     and tech invisibility against the game
#   make test-drills  install, boot into Training: the drill list on its character
#                     select, Training Lab matches (DRILLS_ARGS, e.g. --cycles 10)
#   make test-cpu     install, play a Training match driving the CPU through the
#                     fighter-input hook: scripted inputs and the CPU Options,
#                     checked against the game (CPU_ARGS, e.g. --only c1 tdi)
#   make test-display install, play a Training match with each Display Options
#                     row and the General display rows on and off, checked in the
#                     game's memory and on screenshots (DISPLAY_ARGS)
#   make test-di      install, play hits in a Training match and compare the DI
#                     Display's predicted paths with where the fighter really goes
#                     (DI_ARGS, e.g. --only fsmash; --measure prints the measurements)
#   make test-frames  install, play moves with Mario, Fox, Bowser and Jigglypuff and
#                     compare the frame data service's first actionable frame and
#                     frame advantage with what the game then does (FRAMES_ARGS,
#                     e.g. --chars bowser --only rollf rollb)
#   make test-savestate install, save and load Training matches by scripted input and
#                     check the restored state, replays and refusals (SAVESTATE_ARGS,
#                     e.g. --long for the whole matrix)
#   make test-charges install, check character charge states (Lucas, Samus, Mewtwo,
#                     Lucario, DK, Ivysaur, Wario, Squirtle, R.O.B.) across quick
#                     savestates, Recording Save/Restore Positions and the 3-CPU lite
#                     fallback through the real controller path (CHARGES_ARGS)
#   make test-record  install, play a Training match recording and playing back
#                     inputs (RECORD_ARGS, e.g. --only hmn cpu)
#   make test-storage build, then five boots on the tests' own writable copy of
#                     the SD card: settings saved and loaded, export and import,
#                     the browser, a locked card (STORAGE_ARGS, e.g. --evaluate)
#   make test-card CARD=<path> [REL=<path>] [REF=<path>] [CARD_ARGS=...]
#                     check a ready-made SD card image: static checks against REL
#                     and optional REF, then boot a copy in Dolphin through the
#                     real controller path (settings save and persistence)
#   make test-sounds  install, press through the drill list, the browser and the
#                     lab menu: each press must ask the game for its menu sound once
#   make test-rng     install, play Peach against a Peach CPU and check the Character
#                     RNG page's Peach rows: every value's turnip pulls and forward
#                     smashes in the game's memory, also across the lab's savestate
#                     loads and a recording's playback (RNG_ARGS, e.g. --only smash-pan)
#   make test-match-hud install, lab match: Brawl's match timer shown, counting
#                     and hidden on every way out, across savestates (MATCH_HUD_ARGS)
#   make test-harness install, play the same frames twice from a savestate and
#                     compare the test scripts' batched reads with reads sent one
#                     by one (HARNESS_ARGS, e.g. --bench)
#   make match-baseline  test-match's navigation and screenshots without the plugin
#   make match-compare   test-match, then match-baseline, then compare the two
#                     running-match screenshots pixel for pixel
#   make check-host   build parts of the mod with the PC's own C++ compiler and run
#                     their logic tests on the PC, in seconds, without the emulator or
#                     the console toolchain (HOST_DUMP=prefix of a memory dump for the
#                     tests that read one; HOST_ARGS, e.g. "--only frame_data --log").
#                     Also checks that every text the mod draws fits its box, line
#                     count and buffer at the game's font widths; texts that do not
#                     fit today are listed in tests/host/tests/test_text_fit.cpp
#                     (HOST_ARGS="--only text_fit --log" prints each with its numbers)
#
# `make EXPERIMENTS=1 ...` builds into build/experiments with the code under
# src/*/experiments/ (measurements only; never in a normal build).
# `make EXPERIMENTS=1 PAD_KIB=n ...` also pads the main module with n KiB of
# filler (into build/experiments-padn) to test loading at that size, and
# ANY_MATCH=1 loads it for every offline match, not only Training ones.
# EXTRA_PAD_KIB=n adds n KiB of zero-filled storage to the extra module, and
# EXTRA_HEAP=id (a g_HeapInfos index, e.g. 29 for Fighter3Instance) loads it
# into that heap instead of OverlayFighter4.
# `make PERF=1 ...` builds into build/perf with the time measurements of
# src/shared/perf.h; `make PERF=1 test-perf` plays the scenarios of
# scripts/perf_tests.py and prints the times (PERF_ARGS, e.g. "--only idle").
#
# The emulator targets are run by scripts/emu.py; see docs/testing.md.
# Machine-specific settings go in local.mk (see local.mk.example).

.SUFFIXES:
.DELETE_ON_ERROR:

-include local.mk

TARGET  := TrainingMode
STUB_ID := 21581
MAIN_ID := 21582
MENU_ID := 21583
EXTRA_ID := 21584
EXPERIMENTS ?= 0
PAD_KIB ?= 0
ANY_MATCH ?= 0
EXTRA_PAD_KIB ?= 0
EXTRA_HEAP ?=
ifeq ($(EXPERIMENTS),1)
BUILD   := build/experiments$(if $(filter-out 0,$(PAD_KIB)),-pad$(PAD_KIB))$(if $(filter 1,$(ANY_MATCH)),-any)$(if \
           $(filter-out 0,$(EXTRA_PAD_KIB)),-xpad$(EXTRA_PAD_KIB))$(if $(EXTRA_HEAP),-xheap$(EXTRA_HEAP))
else
BUILD   := build
ifneq ($(PAD_KIB)$(ANY_MATCH)$(EXTRA_PAD_KIB)$(EXTRA_HEAP),000)
$(error PAD_KIB, ANY_MATCH, EXTRA_PAD_KIB and EXTRA_HEAP need EXPERIMENTS=1)
endif
endif
# PERF=1: the time measurements of src/shared/perf.h (into build/perf);
# make PERF=1 test-perf runs them (scripts/perf_tests.py).
PERF ?= 0
ifeq ($(PERF),1)
ifeq ($(EXPERIMENTS),1)
$(error PERF=1 and EXPERIMENTS=1 build into different folders; use one)
endif
BUILD   := build/perf
STUB_BUDGET ?= 24576
endif
# EXTRA_PROOF=1: the extra module also draws its log line in the match.
EXTRA_PROOF ?= 0
ifeq ($(EXTRA_PROOF),1)
BUILD   := $(BUILD)/proof
endif
# STAGE4C=1: compile Stage 4C P+ challenges and character trainers into extra.
STAGE4C ?= 1
ifeq ($(STAGE4C),0)
BUILD   := $(BUILD)/no-stage4c
endif
TOOLS   ?= tools
# Optional local copy of the upstream template for `make tools`.
TOOLS_SRC ?=
PYTHON  ?= py -3

# Size limits the build enforces (bytes), each on a module's permanent
# block: fixSize + .bss, each rounded up to 32 (relocation data past fixSize
# lives only in a temporary buffer while linking, scripts/pack-rel.py).
# STUB_BUDGET applies to the stub, which lives in the 64 KiB Syringe heap
# shared with sy_core and the other plugins. MAIN_BUDGET applies to the
# block the stub keeps for the main module. The number is the largest block that passed the
# stress tests in its heap (Training with 1 and 3 CPUs, items, and 2- and
# 4-player Versus through the results screen).
ifeq ($(EXPERIMENTS),1)
# Measurement code (module-load log) lives in the stub too.
STUB_BUDGET ?= 24576
else
# With Project+ 3.2.1's stock plugins loaded the heap has about 50 KiB free
# before the stub; 20 KiB leaves room for other plugins.
STUB_BUDGET ?= 20480
endif
MAIN_BUDGET ?= 292832
# The menu module (its block, as above), loaded for Training's character
# select into OverlayMenu (293,728 bytes free on CSS; match is not loaded).
MENU_BUDGET ?= 98304
# The extra module (its block, as above), loaded next to the main module
# into OverlayFighter4 (296,032 bytes free in every Training match measured):
# the largest size that passed the stress test there (8 character pairs,
# No. of CPUs raised to 3 in each match).
EXTRA_BUDGET ?= 295712

MWCC    := $(TOOLS)/MWCC4_2/mwcceppc.exe
MWLD    := $(TOOLS)/MWCC4_2/mwldeppc.exe
ELF2REL := $(TOOLS)/elf2rel.exe

BH      := lib/BrawlHeaders
SY      := lib/sy_core/include
LCF     := link/rel.lcf
SYMBOLS := $(BH)/RSBE01.lst link/EXTRA.lst
PACK    := scripts/pack-rel.py

# Every .cpp under a folder, at any depth.
rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))
SHARED_SRCS := $(sort $(call rwildcard,src/shared,*.cpp))
STUB_SRCS   := $(sort $(call rwildcard,src/stub,*.cpp)) $(SHARED_SRCS)
MAIN_MOVED_SRCS := src/extra/tech_sparkle.cpp src/extra/reversal_slots.cpp \
                   src/extra/pplus_collision.cpp \
                   src/extra/hitbox_colors.cpp src/extra/lab_tools.cpp \
                   src/extra/action_log.cpp
MAIN_SRCS   := $(sort $(call rwildcard,src/main,*.cpp)) $(MAIN_MOVED_SRCS) $(SHARED_SRCS)
# The menu module draws with the main module's 2D helpers and reads the SD
# card with its file helpers, compiled again from where they live.
MENU_SRCS   := $(sort $(call rwildcard,src/menu,*.cpp)) src/main/gfx/draw.cpp src/main/gfx/text.cpp \
               src/main/ui/style.cpp src/main/storage/file_io.cpp $(SHARED_SRCS)
# The extra module is linked against the main module and uses its code, so
# it compiles only its own sources and the destructor chain every module needs.
EXTRA_SRCS  := $(filter-out $(MAIN_MOVED_SRCS),$(sort $(call rwildcard,src/extra,*.cpp))) src/shared/global_chain.cpp
ifneq ($(STAGE4C),1)
EXTRA_SRCS  := $(filter-out src/extra/challenge_backends.cpp,$(EXTRA_SRCS))
endif
ifneq ($(EXPERIMENTS),1)
STUB_SRCS := $(filter-out src/stub/experiments/%,$(STUB_SRCS))
MAIN_SRCS := $(filter-out src/main/experiments/%,$(MAIN_SRCS))
endif
# Each module compiles the shared sources itself (with its own TM_MODULE_*),
# so objects mirror the tree under $(BUILD)/stub/ and $(BUILD)/main/.
STUB_OBJS := $(patsubst src/%.cpp,$(BUILD)/stub/%.o,$(STUB_SRCS))
MAIN_OBJS := $(patsubst src/%.cpp,$(BUILD)/main/%.o,$(MAIN_SRCS))
MENU_OBJS := $(patsubst src/%.cpp,$(BUILD)/menu/%.o,$(MENU_SRCS))
EXTRA_OBJS := $(patsubst src/%.cpp,$(BUILD)/extra/%.o,$(EXTRA_SRCS))

BUILT_REL := $(BUILD)/$(TARGET).rel
REL      ?= $(BUILT_REL)
STUB_REL := $(BUILD)/stub/$(TARGET)Stub.rel
MAIN_REL := $(BUILD)/main/$(TARGET)Main.rel
MENU_REL := $(BUILD)/menu/$(TARGET)Menu.rel
EXTRA_REL := $(BUILD)/extra/$(TARGET)Extra.rel
# The main module's global symbols as elf2rel symbol lines that point into
# module MAIN_ID, generated from its ELF so they always match it.
MAIN_EXPORTS := $(BUILD)/main/exports.lst
NEGATIVE := $(BUILD)/negative

# Paths before -I- are searched for "quoted" includes only; the rest for both.
INCLUDES := -Isrc -I- \
            -I$(SY) \
            -I$(BH)/Brawl/Include \
            -I$(BH)/nw4r/include \
            -I$(BH)/OpenRVL/include \
            -I$(BH)/OpenRVL/include/MetroTRK \
            -I$(BH)/OpenRVL/include/revolution \
            -I$(BH)/OpenRVL/include/RVLFaceLib \
            -I$(BH)/OpenRVL/include/stl \
            -I$(BH)/std/Include \
            -I$(BH)/utils/include

CFLAGS   := -msgstyle gcc -Cpp_exceptions off -proc gekko -nostdinc \
            -opt level=4,space,peephole,nostrength \
            -inline auto -fp hard -enum int -sdata 0 -sdata2 0 -func_align 4
CXXFLAGS := -lang=c++ $(CFLAGS) -DTM_STUB_MODULE_ID=$(STUB_ID)
ifeq ($(EXPERIMENTS),1)
CXXFLAGS += -DTM_EXPERIMENTS=1 -DTM_PAD_BYTES=$(PAD_KIB)*1024 -DTM_EXTRA_PAD_BYTES=$(EXTRA_PAD_KIB)*1024
ifneq ($(EXTRA_HEAP),)
CXXFLAGS += -DTM_EXTRA_HEAP=$(EXTRA_HEAP)
endif
ifeq ($(ANY_MATCH),1)
CXXFLAGS += -DTM_LOAD_ANY_MATCH=1
endif
endif
ifeq ($(EXTRA_PROOF),1)
CXXFLAGS += -DTM_EXTRA_PROOF=1
endif
ifeq ($(STAGE4C),1)
CXXFLAGS += -DTM_STAGE4C=1
endif
ifeq ($(PERF),1)
CXXFLAGS += -DTM_PERF=1
endif
# -r1: partial link (elf2rel resolves game symbols); -m: module entry point.
LDFLAGS  := -lcf $(LCF) -r1 -fp hard -m _prolog -g -strip_partial -code_merging all,aggressive

# MWCC writes dependency files with CRLF, backslashes and absolute Windows
# paths (C:\...), which make cannot parse. Rewrite build/x.d into build/x.dep
# with forward slashes and MSYS-style drive paths (/c/...), as one rule plus
# an empty rule per header so a deleted header does not break the build.
FIXDEPS = tr -d '\r' < $(@:.o=.d) \
    | sed -e 's|\\$$||' -e 's|\\|/|g' \
    | awk '{ for (i = 1; i <= NF; i++) { p = $$i; \
                 if (p ~ /^[A-Za-z]:\//) p = "/" tolower(substr(p, 1, 1)) substr(p, 3); \
                 w[n++] = p } } \
           END { line = w[0]; for (i = 1; i < n; i++) line = line " " w[i]; print line; \
                 for (i = 2; i < n; i++) print w[i] ":" }' \
    > $(@:.o=.dep) && rm -f $(@:.o=.d)

ifeq ($(V),1)
Q :=
else
Q := @
endif

.PHONY: all clean tools negative sd-folder install run test-load test-match test-stress \
        test-reject test-lab test-displays test-osds test-drills test-cpu test-recovery-combo test-stage3f test-scenarios \
        test-display test-controls test-di test-frames test-savestate test-record test-storage \
        test-card test-extra-heaps test-fighter-sets test-fighter-sets-no-extra test-fighter-swap test-big-file test-sounds test-rng test-fighter-mechanics test-stage-options test-stage3d test-stage5d test-stage4a-core test-stage4a-hover-ps test-stage4a-pplus-tech test-stage4c test-grab-visualizer test-psa-inspector test-hud-theme-meter test-match-hud test-code-menu test-boot test-harness test-perf match-baseline \
        match-compare check-host

all: $(BUILT_REL)

MODULES := --module match:$(MAIN_ID):$(MAIN_REL) --module menu:$(MENU_ID):$(MENU_REL) \
           --module extra:$(EXTRA_ID):$(EXTRA_REL)

$(BUILT_REL): $(STUB_REL) $(MAIN_REL) $(MENU_REL) $(EXTRA_REL) $(PACK)
	@echo "pack    $@"
	$(Q)$(PYTHON) $(PACK) pack --stub $(STUB_REL) $(MODULES) --stub-budget $(STUB_BUDGET) \
	    --budget match=$(MAIN_BUDGET) --budget menu=$(MENU_BUDGET) --budget extra=$(EXTRA_BUDGET) -o $@

negative: $(STUB_REL) $(MAIN_REL) $(MENU_REL) $(EXTRA_REL) $(PACK)
	@mkdir -p $(NEGATIVE)
	$(Q)for kind in badversion corrupt truncated; do \
	    $(PYTHON) $(PACK) pack --stub $(STUB_REL) $(MODULES) \
	        --break $$kind -o $(NEGATIVE)/$(TARGET)-$$kind.rel || exit 1; done
	$(Q)$(PYTHON) $(PACK) pack --stub $(STUB_REL) $(MODULES) --break corrupt --break-module extra \
	    -o $(NEGATIVE)/$(TARGET)-extra-corrupt.rel
	$(Q)$(PYTHON) $(PACK) pack --stub $(STUB_REL) $(MODULES) --pad $(BIG_PAD) -o $(NEGATIVE)/$(TARGET)-big.rel

REL_ID = $(if $(findstring /stub/,$@),$(STUB_ID),$(if $(findstring /menu/,$@),$(MENU_ID),$(if \
         $(findstring /extra/,$@),$(EXTRA_ID),$(MAIN_ID))))
# $(1): the symbol files. elf2rel takes the first definition of a name, so
# the game's symbols win over the main module's exports.
ELF2REL_RUN = out=$$($(ELF2REL) -i $< -o $@ -s $(1) --rel-id $(REL_ID) 2>&1); \
	  st=$$?; if [ -n "$$out" ]; then echo "$$out"; fi; \
	  if [ $$st -ne 0 ] || echo "$$out" | grep -q "Unresolved"; then \
	    echo "error: $@: a symbol did not resolve" >&2; rm -f $@; exit 1; fi

%.rel: %.elf $(SYMBOLS) | $(ELF2REL)
	@echo "rel     $@"
	$(Q)$(call ELF2REL_RUN,$(SYMBOLS))

# The extra module also links against the main module's symbols.
$(EXTRA_REL): $(BUILD)/extra/$(TARGET)Extra.elf $(SYMBOLS) $(MAIN_EXPORTS) | $(ELF2REL)
	@echo "rel     $@"
	$(Q)$(call ELF2REL_RUN,$(SYMBOLS) $(MAIN_EXPORTS))

$(MAIN_EXPORTS): $(BUILD)/main/$(TARGET)Main.elf scripts/rel-exports.py
	$(Q)$(PYTHON) scripts/rel-exports.py $< --id $(MAIN_ID) -o $@

$(BUILD)/stub/$(TARGET)Stub.elf: $(STUB_OBJS) $(LCF) | $(MWLD)
	@echo "link    $@"
	$(Q)$(MWLD) $(STUB_OBJS) $(LDFLAGS) -map $(@:.elf=.map) -o $@

$(BUILD)/main/$(TARGET)Main.elf: $(MAIN_OBJS) $(LCF) | $(MWLD)
	@echo "link    $@"
	$(Q)$(MWLD) $(MAIN_OBJS) $(LDFLAGS) -map $(@:.elf=.map) -o $@

$(BUILD)/menu/$(TARGET)Menu.elf: $(MENU_OBJS) $(LCF) | $(MWLD)
	@echo "link    $@"
	$(Q)$(MWLD) $(MENU_OBJS) $(LDFLAGS) -map $(@:.elf=.map) -o $@

$(BUILD)/extra/$(TARGET)Extra.elf: $(EXTRA_OBJS) $(LCF) | $(MWLD)
	@echo "link    $@"
	$(Q)$(MWLD) $(EXTRA_OBJS) $(LDFLAGS) -map $(@:.elf=.map) -o $@

$(BUILD)/stub/%.o: src/%.cpp Makefile | $(MWCC)
	@echo "c++     $< (stub)"
	@mkdir -p $(@D)
	$(Q)$(MWCC) $(CXXFLAGS) -DTM_MODULE_STUB=1 -MMD -gccdep $(INCLUDES) -c $< -o $@
	$(Q)$(FIXDEPS)

# Main and extra use -O4,s -ipa file -inline on,noauto -schedule off
# -func_align 4 -use_lmw_stmw on -str pool,reuse,readonly -fp_contract on for
# strong code-size headroom while keeping global_chain.cpp and rel.cpp at
# standard CXXFLAGS. All main_thunks wrappers are static inline and verified at
# pack time by scripts/pack-rel.py check_extra_thunks.
COMMA := ,
COMPACT_CXXFLAGS := $(subst -inline auto,-inline on$(COMMA)noauto,$(CXXFLAGS)) \
                    -ipa file -schedule off -func_align 4 -use_lmw_stmw on -str pool,reuse,readonly -fp_contract on

$(BUILD)/main/shared/global_chain.o: src/shared/global_chain.cpp Makefile | $(MWCC)
	@echo "c++     $< (main)"
	@mkdir -p $(@D)
	$(Q)$(MWCC) $(CXXFLAGS) -DTM_MODULE_MAIN=1 -MMD -gccdep $(INCLUDES) -c $< -o $@
	$(Q)$(FIXDEPS)

$(BUILD)/main/main/rel.o: src/main/rel.cpp Makefile | $(MWCC)
	@echo "c++     $< (main)"
	@mkdir -p $(@D)
	$(Q)$(MWCC) $(CXXFLAGS) -DTM_MODULE_MAIN=1 -MMD -gccdep $(INCLUDES) -c $< -o $@
	$(Q)$(FIXDEPS)

$(BUILD)/main/%.o: src/%.cpp Makefile | $(MWCC)
	@echo "c++     $< (main)"
	@mkdir -p $(@D)
	$(Q)$(MWCC) $(COMPACT_CXXFLAGS) -DTM_MODULE_MAIN=1 -MMD -gccdep $(INCLUDES) -c $< -o $@
	$(Q)$(FIXDEPS)

$(BUILD)/menu/%.o: src/%.cpp Makefile | $(MWCC)
	@echo "c++     $< (menu)"
	@mkdir -p $(@D)
	$(Q)$(MWCC) $(CXXFLAGS) -DTM_MODULE_MENU=1 -MMD -gccdep $(INCLUDES) -c $< -o $@
	$(Q)$(FIXDEPS)

$(BUILD)/extra/shared/global_chain.o: src/shared/global_chain.cpp Makefile | $(MWCC)
	@echo "c++     $< (extra)"
	@mkdir -p $(@D)
	$(Q)$(MWCC) $(CXXFLAGS) -DTM_MODULE_EXTRA=1 -MMD -gccdep $(INCLUDES) -c $< -o $@
	$(Q)$(FIXDEPS)

$(BUILD)/extra/extra/rel.o: src/extra/rel.cpp Makefile | $(MWCC)
	@echo "c++     $< (extra)"
	@mkdir -p $(@D)
	$(Q)$(MWCC) $(CXXFLAGS) -DTM_MODULE_EXTRA=1 -MMD -gccdep $(INCLUDES) -c $< -o $@
	$(Q)$(FIXDEPS)

$(BUILD)/extra/%.o: src/%.cpp Makefile | $(MWCC)
	@echo "c++     $< (extra)"
	@mkdir -p $(@D)
	$(Q)$(MWCC) $(COMPACT_CXXFLAGS) -DTM_MODULE_EXTRA=1 -MMD -gccdep $(INCLUDES) -c $< -o $@
	$(Q)$(FIXDEPS)

$(BH)/RSBE01.lst:
	@echo "error: $@ is missing; run: git submodule update --init --recursive" >&2
	@exit 1

$(MWCC) $(MWLD) $(ELF2REL):
	@echo "error: $@ is missing; run: make tools" >&2
	@exit 1

tools:
	@sh scripts/fetch-tools.sh --dest "$(TOOLS)" $(if $(TOOLS_SRC),--from "$(TOOLS_SRC)")

clean:
	rm -rf build

# The tests on the PC (tests/host/; docs/testing.md, "Tests on the PC"). They
# need neither the console toolchain nor the emulator, and nothing else
# depends on them. HOST_DUMP (here, in local.mk, or TM_HOST_DUMP in the
# environment) names a memory dump of the running game for the tests that
# read one; without it those are skipped.
HOST_DUMP ?=
HOST_ARGS ?=
ifneq ($(HOST_DUMP),)
export TM_HOST_DUMP := $(HOST_DUMP)
endif
check-host:
	$(Q)MSYS2_ARG_CONV_EXCL='*' $(PYTHON) tests/host/build.py $(HOST_ARGS)

# Emulator targets. Settings are passed in the environment, not on the command
# line, so paths with spaces need no quoting. MSYS2_ARG_CONV_EXCL stops MSYS2
# from rewriting arguments that look like POSIX paths. EMU_SPEED=0 runs a
# harness test without Dolphin's speed limit, HARNESS_AUDIT=1 checks every
# batched read against a read of its own (docs/testing.md).
TEST_TIMEOUT ?= 600
export DOLPHIN DOLPHIN_USER LAUNCHER_DOL BRAWL_ISO SD_SOURCE SD_FOLDER SD_IMAGE NAND_SEED TEST_TIMEOUT \
       SCREENSHOT_DIR VIDEO_BACKEND HARNESS_PORT EMU_SPEED EMU_CSS_STATE HARNESS_BATCH HARNESS_AUDIT \
       HARNESS_PROFILE EMU_HOLD_FILE EMU_MIN_FREE_MB EMU_MIN_FREE_DISK_MB
EMU = MSYS2_ARG_CONV_EXCL='*' $(PYTHON) scripts/emu.py

sd-folder:
	$(Q)$(EMU) sd-folder

install: $(REL)
	$(Q)$(EMU) install $(REL)

run: install
	$(Q)$(EMU) run $(REL)

test-load: install
	$(Q)$(EMU) test-load $(REL)

# MATCH_ARGS picks the match, e.g. MATCH_ARGS="--p1 samus --p1-costume 9 --cpu fox".
test-match: install
	$(Q)$(EMU) test-match $(REL) $(MATCH_ARGS)

# STRESS_ARGS, e.g. STRESS_ARGS="--cycles 5 --cpus 3 --items".
test-stress: install
	$(Q)$(EMU) test-stress $(REL) $(STRESS_ARGS)

# LAB_ARGS, e.g. LAB_ARGS="--presses 40".
test-lab: install
	$(Q)$(EMU) test-lab $(REL) $(LAB_ARGS)

# DISPLAYS_ARGS, e.g. DISPLAYS_ARGS="--only jab-hit".
test-displays: install
	$(Q)$(EMU) test-displays $(REL) $(DISPLAYS_ARGS)

# OSDS_ARGS, e.g. OSDS_ARGS="--only wavedash-1 ledgedash-early".
test-osds: install
	$(Q)$(EMU) test-osds $(REL) $(OSDS_ARGS)

# DRILLS_ARGS, e.g. DRILLS_ARGS="--cycles 10".
test-drills: install
	$(Q)$(EMU) test-drills $(REL) $(DRILLS_ARGS)

# CPU_ARGS, e.g. CPU_ARGS="--only c1 counter-ground".
test-cpu: install
	$(Q)$(EMU) test-cpu $(REL) $(CPU_ARGS)
# RECOVERY_COMBO_ARGS, e.g. RECOVERY_COMBO_ARGS="--only pplus-fox combo-reset-noclip".
test-recovery-combo: install
	$(Q)$(EMU) test-recovery-combo $(REL) $(RECOVERY_COMBO_ARGS)
# STAGE3F_ARGS, e.g. STAGE3F_ARGS="--only ledge-options clatter-escape adaptive-cpu".
test-stage3f: install
	$(Q)$(EMU) test-stage3f $(REL) $(STAGE3F_ARGS)
# SCENARIOS_ARGS, e.g. SCENARIOS_ARGS="--only multi-hit-sdi battlefield-platforms".
test-scenarios: install
	$(Q)$(EMU) test-scenarios $(REL) $(SCENARIOS_ARGS)
# SAVESTATE_ARGS, e.g. SAVESTATE_ARGS="--long" or "--only basic dpad".
test-savestate: install
	$(Q)$(EMU) test-savestate $(REL) $(SAVESTATE_ARGS)

# CHARGES_ARGS, e.g. CHARGES_ARGS="--only lucas samus mewtwo lucario".
test-charges: install
	$(Q)$(EMU) test-charges $(REL) $(CHARGES_ARGS)

# RECORD_ARGS, e.g. RECORD_ARGS="--only hmn takeover".
test-record: install
	$(Q)$(EMU) test-record $(REL) $(RECORD_ARGS)

# EXTRA_HEAPS_ARGS, e.g. EXTRA_HEAPS_ARGS="--stages 0,4,14 --victims ice_climbers,mario".
# Use EXPERIMENTS=1 for the log of every module the game loads.
test-extra-heaps: install
	$(Q)$(EMU) test-extra-heaps $(REL) $(EXTRA_HEAPS_ARGS)

# Code Menu character changes in a Training match with three CPUs: a fourth
# fighter heap set is refused while the extra module holds its code heap.
test-fighter-sets: install
	$(Q)$(EMU) test-fighter-sets $(REL)

# The same without the extra module: nothing is refused.
test-fighter-sets-no-extra: install negative
	$(Q)$(EMU) test-fighter-sets $(REL) --without-extra $(NEGATIVE)/$(TARGET)-extra-corrupt.rel
# FIGHTER_SWAP_ARGS, e.g. FIGHTER_SWAP_ARGS="--only p1-swap-and-costume stress-swaps".
test-fighter-swap: install
	$(Q)$(EMU) test-fighter-swap $(REL) $(FIGHTER_SWAP_ARGS)
# STORAGE_ARGS, e.g. STORAGE_ARGS="--only save load" or "--evaluate". Uses
# its own copy of SD_IMAGE (see scripts/emu.py, storage_image), not SD_FOLDER.
test-storage: $(REL)
	$(Q)$(EMU) test-storage $(REL) $(STORAGE_ARGS)

# CARD=<path to sd.raw>, optional REF=<path to reference sd.raw>, REL (default
# build/TrainingMode.rel), CARD_ARGS (e.g. --only static play persist upgrade).
CARD ?=
REF ?=
CARD_ARGS ?=
test-card: $(if $(filter $(BUILT_REL),$(REL)),$(BUILT_REL))
	@if [ -z "$(CARD)" ]; then echo "error: test-card needs CARD=<path to sd.raw>" >&2; exit 2; fi
	$(Q)$(EMU) test-card "$(CARD)" --rel "$(REL)" $(if $(REF),--ref "$(REF)") $(CARD_ARGS)

# DISPLAY_ARGS, e.g. DISPLAY_ARGS="--only hitboxes speed".
test-display: install
	$(Q)$(EMU) test-display $(REL) $(DISPLAY_ARGS)

# CONTROLS_ARGS, e.g. CONTROLS_ARGS="--only advance-z shortcuts".
test-controls: install
	$(Q)$(EMU) test-controls $(REL) $(CONTROLS_ARGS)

# DI_ARGS, e.g. DI_ARGS="--only fsmash" or "--measure".
test-di: install
	$(Q)$(EMU) test-di $(REL) $(DI_ARGS)

# FRAMES_ARGS, e.g. FRAMES_ARGS="--chars bowser jigglypuff --only rollf rollb" or "--keep-going".
test-frames: install
	$(Q)$(EMU) test-frames $(REL) $(FRAMES_ARGS)

test-sounds: install
	$(Q)$(EMU) test-sounds $(REL)

# RNG_ARGS, e.g. RNG_ARGS="--only smash-pan turnip-stitch" or "--keep-going".
test-rng: install
	$(Q)$(EMU) test-rng $(REL) $(RNG_ARGS)

# FIGHTER_MECH_ARGS, e.g. FIGHTER_MECH_ARGS="--only samus mewtwo dedede" or "--keep-going".
test-fighter-mechanics: install
	$(Q)$(EMU) test-fighter-mechanics $(REL) $(FIGHTER_MECH_ARGS)

# STAGE_OPTIONS_ARGS, e.g. STAGE_OPTIONS_ARGS="--only luigis-mansion fod".
test-stage-options: install
	$(Q)$(EMU) test-stage-options $(REL) $(STAGE_OPTIONS_ARGS)

# STAGE3D_ARGS, e.g. STAGE3D_ARGS="--only start-on-input cc-hitstun heatmap".
test-stage3d: install
	$(Q)$(EMU) test-stage3d $(REL) $(STAGE3D_ARGS)

# STAGE5D_ARGS, e.g. STAGE5D_ARGS="--only ledge-5-grab wall-info nana-throw".
test-stage5d: install
	$(Q)$(EMU) test-stage5d $(REL) $(STAGE5D_ARGS)

# STAGE4A_CORE_ARGS, e.g. STAGE4A_CORE_ARGS="--only ledge-core wavedash-waveland".
test-stage4a-core: install
	$(Q)$(EMU) test-stage4a-core $(REL) $(STAGE4A_CORE_ARGS)

# STAGE4A_HOVER_PS_ARGS, e.g. STAGE4A_HOVER_PS_ARGS="--only peach-fc mewtwo-hc".
test-stage4a-hover-ps: install
	$(Q)$(EMU) test-stage4a-hover-ps $(REL) $(STAGE4A_HOVER_PS_ARGS)

# STAGE4A_PPLUS_TECH_ARGS, e.g. STAGE4A_PPLUS_TECH_ARGS="--only rar dacus-gatling".
test-stage4a-pplus-tech: install
	$(Q)$(EMU) test-stage4a-pplus-tech $(REL) $(STAGE4A_PPLUS_TECH_ARGS)

# SKILL_DRILLS_UI_ARGS, e.g. SKILL_DRILLS_UI_ARGS="--only boot-menu ledgedash-hud".
test-skill-drills-ui: install
	$(Q)$(EMU) test-skill-drills-ui $(REL) $(SKILL_DRILLS_UI_ARGS)

# STAGE4C_ARGS, e.g. STAGE4C_ARGS="--only cancel-sprint flow-course snake-recovery". Needs STAGE4C=1.
test-stage4c: install
ifneq ($(STAGE4C),1)
	$(error test-stage4c needs the STAGE4C=1 build: make STAGE4C=1 test-stage4c)
endif
	$(Q)$(EMU) test-stage4c $(REL) $(STAGE4C_ARGS)

# GRAB_VIZ_ARGS, e.g. GRAB_VIZ_ARGS="--only off-noop standard-grab".
test-grab-visualizer: install
	$(Q)$(EMU) test-grab-visualizer $(REL) $(GRAB_VIZ_ARGS)

# PSA_ARGS, e.g. PSA_ARGS="--aspect 4:3 --only live-threads".
test-psa-inspector: install
	$(Q)$(EMU) test-psa-inspector $(REL) $(PSA_ARGS)

# OVERNIGHT_TOOLS_ARGS, e.g. OVERNIGHT_TOOLS_ARGS="--aspect 4:3".
test-overnight-tools: install
	$(Q)$(EMU) test-overnight-tools $(REL) $(OVERNIGHT_TOOLS_ARGS)

# HUD_THEME_METER_ARGS, e.g. HUD_THEME_METER_ARGS="--only theme-v2-persist".
test-hud-theme-meter: install
	$(Q)$(EMU) test-hud-theme-meter $(REL) $(HUD_THEME_METER_ARGS)

# PERF_ARGS, e.g. PERF_ARGS="--only idle fight --frames 300". Needs PERF=1.
test-perf: install
ifneq ($(PERF),1)
	$(error test-perf needs the PERF=1 build: make PERF=1 test-perf)
endif
	$(Q)$(EMU) test-perf $(REL) $(PERF_ARGS)

# MATCH_HUD_ARGS, e.g. MATCH_HUD_ARGS="--only lab gametimer".
test-match-hud: install
	$(Q)$(EMU) test-match-hud $(REL) $(MATCH_HUD_ARGS)

# CODE_MENU_ARGS, e.g. CODE_MENU_ARGS="--only hide-hud --aspect 4:3".
test-code-menu: install
	$(Q)$(EMU) test-code-menu $(REL) $(CODE_MENU_ARGS)

# Boots with no button (port 1, then port 2), X and L held, 4-port drill list
# hand-over, and a regular offline Versus match with no match module loaded.
test-boot: install
	$(Q)$(EMU) test-boot $(REL) $(BOOT_ARGS)

# HARNESS_ARGS, e.g. HARNESS_ARGS="--bench".
test-harness: install
	$(Q)$(EMU) test-harness $(REL) $(HARNESS_ARGS)

KIND ?= badversion
test-reject: install negative
	$(Q)$(EMU) test-reject $(NEGATIVE)/$(TARGET)-$(KIND).rel $(KIND) --restore $(REL)

# The plugin file padded far past the heaps it was once read whole into
# (MenuInstance is 0x79B00 bytes in the match layout): the modules still load
# at the character select and in a Training match.
BIG_PAD ?= 0x100000
test-big-file: install negative
	$(Q)$(EMU) test-match $(REL) --install $(NEGATIVE)/$(TARGET)-big.rel

match-baseline: $(REL)
	$(Q)$(EMU) test-match $(REL) --without-plugin $(MATCH_ARGS)

# Both runs pause at the same game frame with identical input; compares the
# running-match screenshots outside the overlay.
SHOTS_DIR = $(or $(SCREENSHOT_DIR),$(DOLPHIN_USER)/ScreenShots)
match-compare: test-match match-baseline
	$(Q)$(EMU) compare-shots "$(SHOTS_DIR)/match-plugin.png" "$(SHOTS_DIR)/match-noplugin.png"

-include $(STUB_OBJS:.o=.dep) $(MAIN_OBJS:.o=.dep) $(MENU_OBJS:.o=.dep) $(EXTRA_OBJS:.o=.dep)
