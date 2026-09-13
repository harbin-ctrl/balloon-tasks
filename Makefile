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

OBJS    := balloon_tasks.o balloon_gen.o thunder_synth.o audio.o
TOY_LIBS := $(RINGMENU_LIB) $(TOYAUDIO_LIB) $(GHOSTICON_LIB) $(TOYPLATFORM_LIB)
ifeq ($(PLATFORM),win32)
# The icon, embedded so the taskbar, Explorer and the shortcut all show it.
RES_OBJ := $(APP_ID)_res.o
else
RES_OBJ :=
endif

PREFIX := /usr/local
BINDIR := $(PREFIX)/bin

.PHONY: all clean install uninstall stage icons regen-icons

all: $(TARGET)

$(TARGET): $(OBJS) $(RES_OBJ) $(TOY_LIBS)
	$(CC) -o $@ $(OBJS) $(RES_OBJ) $(TOY_LIBS) $(LIBS) $(APP_LDFLAGS)

balloon_tasks.o: balloon_tasks.c balloon_gen.h audio.h cursor_hand_grab.h $(RINGMENU_DIR)/ringmenu.h \
	$(GHOSTICON_DIR)/ghost_icon.h $(TOYPLATFORM_DIR)/platform.h $(TOYPLATFORM_DIR)/compat.h
	$(CC) $(CFLAGS) -I$(GHOSTICON_DIR) -c -o $@ $<

balloon_gen.o: balloon_gen.c balloon_gen.h
	$(CC) $(CFLAGS) -c -o $@ $<

audio.o: audio.c audio.h $(TOYAUDIO_DIR)/toy_audio.h \
	$(TOYAUDIO_DIR)/toy_audio_stream.h
	$(CC) $(CFLAGS) -c -o $@ $<

thunder_synth.o: thunder_synth.c thunder_synth.h
	$(CC) $(CFLAGS) -c -o $@ $<

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

ifeq ($(PLATFORM),win32)
WIN_DIR ?= $(TOYS_ROOT)/win-packaging
WINDRES ?= windres
include $(WIN_DIR)/install.mk

# One per-user win-toys folder and a shortcut in the Start menu's Ace folder,
# as the ace-toys package installs on Linux. See win-packaging/install.mk.
install: $(TARGET)
	$(call win_install,$(APP_ID),$(APP_NAME),$(TARGET))

uninstall:
	$(call win_uninstall,$(APP_ID),$(APP_NAME))

# Into the package the root Makefile's `package` builds.
stage: $(TARGET)
	$(call win_stage,$(APP_ID),$(APP_NAME),$(TARGET),$(DESTDIR))

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
