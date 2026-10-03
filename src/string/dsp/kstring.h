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
#ifndef TSPOD_KSTRING_H
#define TSPOD_KSTRING_H

#include <stdint.h>

#include "Dynamics/crossfade.h"
#include "Utility/dcblock.h"
#include "Utility/delayline.h"
#include "Filters/onepole.h"

#ifdef __cplusplus

/** @file KarplusString.h */

namespace tspod
{
namespace dsp
{
using namespace daisysp;
using namespace tspod::dsp;

/**  
       @brief Comb filter / KS string.
       @author Ben Sergentanis
       @date Jan 2021 
       "Lite" version of the implementation used in Rings \n \n 
       Ported from pichenettes/eurorack/plaits/dsp/oscillator/formant_oscillator.h \n
       to an independent module. \n
       Original code written by Emilie Gillet in 2016. \n
*/
class String
{
  public:
    String() {}
    ~String() {}

    /** Initialize the module.
        \param sample_rate Audio engine sample rate
    */
    void Init(float sample_rate);

    /** Clear the delay line */
    void Reset();

    /** Get the next floating point sample
        \param in Signal to excite the string.
    */
    float Process(const float in);

    /** Set the string frequency.
        \param freq Frequency in Hz
    */
    void SetFreq(float freq);

    /** Set the string's behavior.
        \param -1 to 0 is curved bridge, 0 to 1 is dispersion.
    */
    void SetNonLinearity(float non_linearity_amount);

    /** Set the string's overall brightness
        \param Works 0-1.
    */
    void SetBrightness(float brightness);

    /** Set the string's decay time.
        \param damping Works 0-1.
    */
    void SetDamping(float damping);


  private:
    static constexpr size_t kDelayLineSize = 1024;

    enum StringNonLinearity
    {
        NON_LINEARITY_CURVED_BRIDGE,
        NON_LINEARITY_DISPERSION
    };

    template <String::StringNonLinearity non_linearity>
    float ProcessInternal(const float in);

    // THE CHANGE. Everything above the per-sample work in ProcessInternal
    // depends only on these four inputs, so it is derived once when one of them
    // moves rather than on every sample.
    void  UpdateCoefficients();
    bool  coeffs_valid_     = false;
    float c_in_freq_        = -1.0f;
    float c_in_damping_     = -1.0f;
    float c_in_brightness_  = -1.0f;
    float c_in_nonlin_      = -1.0f;
    float c_delay_          = 0.0f;
    float c_src_ratio_      = 1.0f;
    float c_brightness_     = 0.0f;
    float c_damping_comp_   = 1.0f;
    float c_stretch_point_  = 0.0f;
    float c_stretch_corr_   = 1.0f;
    float c_noise_amount_   = 0.0f;
    float c_noise_filter_   = 0.0f;
    float c_bridge_curving_ = 0.0f;
    float c_ap_gain_        = 0.0f;

    DelayLine<float, kDelayLineSize>     string_;
    DelayLine<float, kDelayLineSize / 4> stretch_;

    float frequency_, non_linearity_amount_, brightness_, damping_;

    float sample_rate_;

    OnePole iir_damping_filter_;

    DcBlock dc_blocker_;

    CrossFade crossfade_;

    float dispersion_noise_;
    float curved_bridge_;

    // Very crappy linear interpolation upsampler used for low pitches that
    // do not fit the delay line. Rarely used.
    float src_phase_;
    float out_sample_[2];
};
} // namespace dsp
} // namespace tspod
#endif
#endif
