.DEFAULT_GOAL := all

ifeq ($(origin CC),default)
ifneq ($(OS)$(MSYSTEM),)
CC := gcc
else
CC := ccache gcc
endif
endif
CCACHE_PREFIX ?= distcc
export CC CCACHE_PREFIX

# The shared toy libraries normally live beside this checkout. TOYS_ROOT can
# point at another ace-toys/win-toys checkout for a standalone clone.
TOYS_ROOT ?= $(abspath $(CURDIR)/..)

# The window system and audio backend follow the host: Win32 and WASAPI on
# Windows, Wayland and PipeWire everywhere else.
include $(TOYS_ROOT)/toy-platform/platform.mk

CFLAGS := -O3 -ffast-math -Wall -pthread $(TOY_PLATFORM_CFLAGS)
LIBS   := $(TOY_PLATFORM_LIBS) -lm -pthread

APP_ID   := balloon-tasks
APP_NAME := Balloon Tasks!
TARGET   := $(APP_ID)$(EXE)
RINGMENU_DIR ?= $(TOYS_ROOT)/ring-menu
RINGMENU_LIB := $(RINGMENU_DIR)/libringmenu.a
TOYAUDIO_DIR ?= $(TOYS_ROOT)/toy-audio
ACE_DIR ?= $(TOYS_ROOT)/ace-packaging
TOYAUDIO_LIB := $(TOYAUDIO_DIR)/libtoyaudio.a
ifeq ($(PLATFORM),win32)
include $(TOYAUDIO_DIR)/wasapi.mk
CFLAGS  += $(TOY_AUDIO_WASAPI_CFLAGS)
LIBS    += $(TOY_AUDIO_WASAPI_LIBS)
else
include $(TOYAUDIO_DIR)/pipewire.mk
CFLAGS  += $(TOY_AUDIO_PIPEWIRE_CFLAGS)
LIBS    += $(TOY_AUDIO_PIPEWIRE_LIBS)
endif
CFLAGS  += -I$(RINGMENU_DIR) -I$(TOYAUDIO_DIR)
GHOSTICON_DIR ?= $(TOYS_ROOT)/shared
GHOSTICON_LIB := $(GHOSTICON_DIR)/libghosticon.a

OBJS    := balloon_tasks.o balloon_gen.o task_text.o thunder_synth.o audio.o
TOY_LIBS := $(RINGMENU_LIB) $(TOYAUDIO_LIB) $(GHOSTICON_LIB) $(TOYPLATFORM_LIB)
ifeq ($(PLATFORM),win32)
# The icon, embedded so the taskbar, Explorer and the shortcut all show it.
RES_OBJ := $(APP_ID)_res.o
else
RES_OBJ :=
endif

PREFIX := /usr/local
BINDIR := $(PREFIX)/bin

.PHONY: all clean install uninstall stage icons regen-icons regen-font test

all: $(TARGET)

$(TARGET): $(OBJS) $(RES_OBJ) $(TOY_LIBS)
	$(CC) -o $@ $(OBJS) $(RES_OBJ) $(TOY_LIBS) $(LIBS) $(APP_LDFLAGS)

balloon_tasks.o: balloon_tasks.c balloon_gen.h task_text.h audio.h cursor_hand_grab.h $(RINGMENU_DIR)/ringmenu.h \
	$(GHOSTICON_DIR)/ghost_icon.h $(TOYPLATFORM_DIR)/platform.h $(TOYPLATFORM_DIR)/compat.h
	$(CC) $(CFLAGS) -I$(GHOSTICON_DIR) -c -o $@ $<

balloon_gen.o: balloon_gen.c balloon_gen.h
	$(CC) $(CFLAGS) -c -o $@ $<

task_text.o: task_text.c task_text.h task_font.h assets/fonts/Fredoka-Variable.ttf
	$(CC) $(CFLAGS) -c -o $@ $<

audio.o: audio.c audio.h $(TOYAUDIO_DIR)/toy_audio.h \
	$(TOYAUDIO_DIR)/toy_audio_stream.h
	$(CC) $(CFLAGS) -c -o $@ $<

thunder_synth.o: thunder_synth.c thunder_synth.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: test_task_text$(EXE)
	./test_task_text$(EXE)

test_task_text$(EXE): test_task_text.c task_text.c task_text.h task_font.h
	$(CC) -O2 -Wall -Wextra -o $@ test_task_text.c task_text.c -lm

# The ring menu, audio core and platform layer live in their own library
# checkouts; delegate so they rebuild whenever their sources change.
$(RINGMENU_LIB): FORCE
	$(MAKE) -C $(RINGMENU_DIR)
$(TOYAUDIO_LIB): FORCE
	$(MAKE) -C $(TOYAUDIO_DIR)
$(GHOSTICON_LIB): FORCE
	$(MAKE) -C $(GHOSTICON_DIR)
$(TOYPLATFORM_LIB): FORCE
	$(MAKE) -C $(TOYPLATFORM_DIR)
FORCE:

clean:
	$(RM) $(TARGET) $(OBJS) $(RES_OBJ) $(APP_ID).ico
	$(RM) -r installer build
	$(RM) xdg-*.h xdg-*.c balloon_assets.h thunder_pcm.h
	$(RM) assets/.apngs_generated assets/.pops_generated

ICON_SIZES    := 16 32 48 64 128 256 512
ICON_FILES    := $(foreach size,$(ICON_SIZES),assets/icon_$(size)x$(size).png)
PYTHON        ?= python3

DATADIR := $(PREFIX)/share

include $(ACE_DIR)/install.mk

icons: $(ICON_FILES)

$(ICON_FILES):
	@echo "Missing $@; run 'make regen-icons'" >&2
	@exit 1

regen-icons:
	$(PYTHON) tools/make_icon.py

regen-font:
	$(PYTHON) tools/make_task_font.py

ifeq ($(PLATFORM),win32)
WIN_DIR ?= $(TOYS_ROOT)/win-packaging
WINDRES ?= windres
include $(WIN_DIR)/install.mk

APP_VERSION := 0.1
ISCC ?= $$(cygpath -u "$$LOCALAPPDATA")/Programs/Inno Setup 6/ISCC.exe
INSTALLER := installer/$(APP_ID)-$(APP_VERSION)-setup.exe
# Keeps MSYS2 from rewriting /FLAG arguments into paths.
WIN_NO_ARGCONV := MSYS2_ARG_CONV_EXCL='*'

# One installer carries both architectures. Each builds with its own MSYS2
# toolchain (CLANGARM64, CLANG64) in its own copy of the sources.
WIN_ARCHES := arm64 x64
WIN_BUILD := build/windows
WIN_LIB_DIRS := toy-platform ring-menu toy-audio shared ace-packaging win-packaging
# Sources only: build outputs belong to whichever architecture made them.
WIN_COPY_EXCLUDES := --exclude=.git --exclude='*.o' --exclude='*.a' --exclude='*.exe' --exclude='*.ico'
win_prefix = $(if $(filter x64,$(1)),/clang64,/clangarm64)
win_env = MSYSTEM=$(if $(filter x64,$(1)),CLANG64,CLANGARM64) \
	MSYSTEM_PREFIX=$(call win_prefix,$(1)) \
	MSYSTEM_CARCH=$(if $(filter x64,$(1)),x86_64,aarch64) \
	PATH="$(call win_prefix,$(1))/bin:$$PATH"

.PHONY: inno

# The Inno Setup installer; see balloon-tasks.iss.
inno: $(addprefix inno-stage-,$(WIN_ARCHES)) $(APP_ID).ico $(APP_ID).iss
	$(WIN_NO_ARGCONV) "$(ISCC)" /Q /DAppVersion=$(APP_VERSION) $(APP_ID).iss
	@echo "installer: $(INSTALLER)"

# installer/stage/<arch>: the program and the runtime DLLs it loads.
inno-stage-%: FORCE
	rm -rf $(WIN_BUILD)/$* installer/stage/$*
	mkdir -p $(WIN_BUILD)/$*/$(APP_ID) installer/stage/$*
	tar -c $(WIN_COPY_EXCLUDES) --exclude=./build --exclude=./installer . | \
		tar -x -C $(WIN_BUILD)/$*/$(APP_ID)
	tar -c -C "$(TOYS_ROOT)" $(WIN_COPY_EXCLUDES) $(WIN_LIB_DIRS) | tar -x -C $(WIN_BUILD)/$*
	env $(call win_env,$*) $(MAKE) -C $(WIN_BUILD)/$*/$(APP_ID) TOYS_ROOT="$(CURDIR)/$(WIN_BUILD)/$*" $(TARGET)
	cp $(WIN_BUILD)/$*/$(APP_ID)/$(TARGET) installer/stage/$*/
	env $(call win_env,$*) ldd installer/stage/$*/$(TARGET) | \
		awk -v prefix="$(call win_prefix,$*)/" 'index($$3, prefix) == 1 { print $$3 }' | \
		sort -u | xargs -r -I{} cp {} installer/stage/$*/

# Installs through the installer, replacing any earlier win-toys copy.
install: inno
	$(call win_uninstall,$(APP_ID),$(APP_NAME))
	$(WIN_NO_ARGCONV) "./$(INSTALLER)" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS

uninstall:
	$(call win_uninstall,$(APP_ID),$(APP_NAME))
	uninstaller="$$(cygpath -u "$$LOCALAPPDATA")/Programs/$(APP_ID)/unins000.exe"; \
	if [ -f "$$uninstaller" ]; then $(WIN_NO_ARGCONV) "$$uninstaller" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART; fi

# An .ico entry holds at most 256 px.
ICO_SIZES := 16 32 48 64 128 256

$(APP_ID).ico: $(foreach size,$(ICO_SIZES),assets/icon_$(size)x$(size).png)
	$(call win_ico,$@,$(foreach size,$(ICO_SIZES),assets/icon_$(size)x$(size).png))

$(APP_ID)_res.o: $(APP_ID).rc $(APP_ID).ico
	$(WINDRES) $< -O coff -o $@
else
# System install: binary plus the desktop entry, Ace menu and
# every icon size — the same set the user install ships, like splat.
# The caches are refreshed only for a live install. A staged one (DESTDIR,
# as in a package build) would otherwise ship system-wide caches; the
# package's triggers refresh the real ones.
install: $(TARGET) icons ace-install
	install -Dm755 $(TARGET) "$(DESTDIR)$(BINDIR)/$(TARGET)"
	install -Dm644 $(APP_ID).desktop "$(DESTDIR)$(DATADIR)/applications/$(APP_ID).desktop"
	for size in $(ICON_SIZES); do \
		install -Dm644 assets/icon_$${size}x$${size}.png \
			"$(DESTDIR)$(DATADIR)/icons/hicolor/$${size}x$${size}/apps/$(APP_ID).png"; \
	done
	if [ -z "$(DESTDIR)" ]; then \
		update-desktop-database "$(DESTDIR)$(DATADIR)/applications" 2>/dev/null || true; \
		gtk-update-icon-cache -f -t -q "$(DESTDIR)$(DATADIR)/icons/hicolor" 2>/dev/null || true; \
	fi

uninstall: ace-uninstall
	$(RM) "$(DESTDIR)$(BINDIR)/$(TARGET)"
	$(RM) "$(DESTDIR)$(DATADIR)/applications/$(APP_ID).desktop"
	for size in $(ICON_SIZES); do \
		$(RM) "$(DESTDIR)$(DATADIR)/icons/hicolor/$${size}x$${size}/apps/$(APP_ID).png"; \
	done
	if [ -z "$(DESTDIR)" ]; then \
		update-desktop-database "$(DESTDIR)$(DATADIR)/applications" 2>/dev/null || true; \
		gtk-update-icon-cache -f -t -q "$(DESTDIR)$(DATADIR)/icons/hicolor" 2>/dev/null || true; \
	fi
endif
