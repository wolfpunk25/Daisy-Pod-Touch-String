// VENDORED from DaisySP, with ONE change: the per-sample coefficient block in
// String::ProcessInternal is cached.
//
// Copyright (c) 2020 Electrosmith, Corp, Emilie Gillet. MIT. Ported there from
// pichenettes/eurorack (Rings / Plaits), originally by Emilie Gillet.
//
// WHY. ProcessInternal recomputed two powf and an atanf on EVERY SAMPLE, from
// frequency, damping, brightness and non-linearity -- none of which change
// faster than the control rate. StringVoice makes it worse by calling
// SetBrightness and SetDamping once per sample, so the same answer is derived
// 48,000 times a second.
//
// It cost roughly 17 points of the audio interrupt per voice on the Pod,
// measured: idle fell from 27% to 10% the moment silent voices stopped being
// processed, one always-running StringVoice being the only difference. At that
// price polyphony is impossible -- six voices would saturate the interrupt.
//
// The prologue is now derived only when one of its inputs actually moves.
// Nothing else is altered, so for a given set of parameters the output is
// unchanged. The same finding and a patch for it came out of the ZenTouch Pod
// port; worth filing upstream.
/*
Copyright (c) 2020 Electrosmith, Corp, Emilie Gillet

Use of this source code is governed by an MIT-style
license that can be found in the LICENSE file or at
https://opensource.org/licenses/MIT.
*/

#pragma once
#ifndef TSPOD_KSTRINGVOICE_H
#define TSPOD_KSTRINGVOICE_H

#include "Filters/svf.h"
#include "kstring.h"
#include "Noise/dust.h"
#include <stdint.h>
#ifdef __cplusplus

/** @file stringvoice.h */

namespace tspod
{
namespace dsp
{
using namespace daisysp;
using namespace tspod::dsp;

/**  
       @brief Extended Karplus-Strong, with all the niceties from Rings 
       @author Ben Sergentanis
       @date Jan 2021
       Ported from pichenettes/eurorack/plaits/dsp/physical_modelling/string_voice.h \n
       and pichenettes/eurorack/plaits/dsp/physical_modelling/string_voice.cc \n
       to an independent module. \n
       Original code written by Emilie Gillet in 2016. \n
*/
class StringVoice
{
  public:
    StringVoice() {}
    ~StringVoice() {}

    /** Initialize the module
        \param sample_rate Audio engine sample rate
    */
    void Init(float sample_rate);

    /** Reset the string oscillator */
    void Reset();

    /** Get the next sample
        \param trigger Strike the string. Defaults to false.
    */
    float Process(bool trigger = false);

    /** Continually excite the string with noise.
        \param sustain True turns on the noise.
    */
    void SetSustain(bool sustain);

    /** Strike the string. */
    void Trig();

    /** Set the string root frequency.
        \param freq Frequency in Hz.
    */
    void SetFreq(float freq);

    /** Hit the string a bit harder. Influences brightness and decay.
        \param accent Works 0-1.
    */
    void SetAccent(float accent);

    /** Changes the string's nonlinearity (string type).
        \param structure Works 0-1. 0-.26 is curved bridge, .26-1 is dispersion.
    */
    void SetStructure(float structure);

    /** Set the brighness of the string, and the noise density.
        \param brightness Works best 0-1
    */
    void SetBrightness(float brightness);

    /** How long the resonant body takes to decay relative to the accent level.
        \param damping Works best 0-1. Full damp is only achieved with full accent.
    */
    void SetDamping(float damping);

    /** Get the raw excitation signal. Must call Process() first. */
    float GetAux();

  private:
    float sample_rate_;

    bool  sustain_, trig_;
    float f0_, brightness_, damping_;
    float density_, accent_;
    float aux_;

    Dust   dust_;
    Svf    excitation_filter_;
    String string_;
    size_t remaining_noise_samples_;
};
} // namespace dsp
} // namespace tspod
#endif
#endif
