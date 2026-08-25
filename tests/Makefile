# Host build of the real engine and the real control model. No Daisy, no board.
#
# Everything in src/string, src/ui/controlmodel.* and src/midi is free of
# hardware — the only Daisy dependency is DaisySP, which is portable C++ — so the
# tests drive the actual instrument rather than a paraphrase of it. That is the
# thing to reach for first on this family of ports: on every one of them,
# reasoning about the code produced a confident wrong answer that a measurement
# then corrected.

CXX      ?= c++
SRC      := ../src
DSP      := ../DaisySP/Source

CXXFLAGS := -std=gnu++17 -O2 -g -Wall -Wextra -Wno-unused-parameter \
            -I$(SRC) -I$(DSP) -I$(DSP)/Utility

ENGINE := \
	$(SRC)/string/engine.cpp \
	$(SRC)/string/scale.cpp \
	$(SRC)/string/clock.cpp \
	$(SRC)/string/cpattern.cpp \
	$(SRC)/string/space.cpp \
	$(SRC)/ui/controlmodel.cpp \
	$(SRC)/midi/weather.cpp

# Only the DaisySP modules the instrument actually reaches.
DAISYSP := \
	$(DSP)/PhysicalModeling/stringvoice.cpp \
	$(DSP)/PhysicalModeling/KarplusString.cpp \
	$(DSP)/Effects/overdrive.cpp \
	$(DSP)/Filters/svf.cpp \
	$(DSP)/Utility/dcblock.cpp \
	$(DSP)/Dynamics/crossfade.cpp

all: touchstring-test

touchstring-test: main.cpp wav.h $(ENGINE) $(DAISYSP)
	$(CXX) $(CXXFLAGS) -o $@ main.cpp $(ENGINE) $(DAISYSP) -lm

test: touchstring-test
	./touchstring-test

# Renders demo WAVs so the instrument can be judged by ear without a board.
audio: touchstring-test
	./touchstring-test --audio

clean:
	rm -rf touchstring-test touchstring-test.dSYM *.wav

.PHONY: all test audio clean
