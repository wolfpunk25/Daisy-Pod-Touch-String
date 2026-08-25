# TouchString for Daisy Pod
#
# make DEBUG=1 turns on the USB serial log: the audio-interrupt CPU meter, which
# layer the panel is on, where every control is sitting and whether MIDI is
# arriving. The CPU figure is the one number the host tests cannot produce.
# Non-DEBUG builds do not enumerate on USB at all — that is expected, not a bad
# flash.
ifeq ($(DEBUG), 1)
C_DEFS += -DTS_DEBUG=1
endif

TARGET = touchstring_pod

CPP_STANDARD = -std=gnu++17

LIBDAISY_DIR = libDaisy
DAISYSP_DIR  = DaisySP

C_INCLUDES  = -Isrc/
C_USR_FLAGS = -ffast-math -funroll-loops

CPP_SOURCES = \
	src/main.cpp \
	$(wildcard src/string/*.cpp) \
	$(wildcard src/ui/*.cpp) \
	$(wildcard src/midi/*.cpp)

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

libs:
	cd $(LIBDAISY_DIR) && $(MAKE)
	cd $(DAISYSP_DIR) && $(MAKE)

clean-libs:
	cd $(LIBDAISY_DIR) && $(MAKE) clean
	cd $(DAISYSP_DIR) && $(MAKE) clean

# The host tests. Reach for these first.
test:
	$(MAKE) -C tests test

audio:
	$(MAKE) -C tests audio

.PHONY: libs clean-libs test audio
