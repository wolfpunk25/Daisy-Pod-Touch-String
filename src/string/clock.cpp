#include "clock.h"
#include <math.h>

namespace tspod {

void Clock::Init(float interval_us, uint32_t ppqn_in, uint32_t ppqn_out)
{
    tr_time_         = static_cast<uint32_t>(static_cast<float>(ppqn_out) * interval_us);
    ticks_per_clock_ = ppqn_out / ppqn_in;
    ppqn_out_        = ppqn_out;
}

void Clock::SetTempo(float norm)
{
    if(norm < 0.0f) norm = 0.0f;
    if(norm > 1.0f) norm = 1.0f;
    // Two decimal places of the normalised control, so ADC noise does not
    // restart the clock every pass. Upstream's `fcomp`, inlined.
    if(static_cast<int32_t>(roundf(norm * 20.0f))
       == static_cast<int32_t>(roundf(raw_tempo_ * 20.0f)))
        return;
    raw_tempo_ = norm;

    // The bottom tenth of the control lands under kBPMMin, which is what selects
    // external sync. Upstream's ten-BPM offset, kept: it makes the sync position
    // a deliberate move to the very bottom rather than something you can land on
    // by accident.
    const float kOff = 10.0f;
    manual_tempo_    = (kBPMRange - kOff) * norm + kBPMMin - kOff;
    tempo_us_        = TempoUs(manual_tempo_ > 1.0f ? manual_tempo_ : 1.0f);

    if(!Internal())
    {
        // Crossing into sync territory parks playback until pulses arrive.
        if(running_)
        {
            running_      = false;
            about_to_run_ = true;
        }
        Reset();
    }
    else if(about_to_run_)
    {
        // ...and crossing back out resumes it on our own time.
        about_to_run_ = false;
        running_      = true;
    }
}

void Clock::ExternalPulse()
{
    if(Internal()) return;
    if(!running_ && !about_to_run_) return;

    if(about_to_run_)
    {
        about_to_run_ = false;
        running_      = true;
    }
    else
    {
        resync_ = true;
        hold_   = false;
        EmitTicks();
    }
}

// Called once per audio block, and again on every external pulse.
void Clock::EmitTicks()
{
    uint32_t nticks = 0;

    // We have already produced every tick this external pulse is worth. Keep
    // counting, so the tempo estimate below sees the overshoot, but do not
    // advance the timeline until the next pulse arrives.
    if(hold_)
    {
        nticks = (fticks_ + tr_time_) / tempo_us_;
        fticks_ += tr_time_ - (nticks * tempo_us_);
        tempo_ticks_ += nticks;
        return;
    }

    if(resync_)
    {
        fticks_        = 0;
        nticks         = ticks_per_clock_ - (ticks_ - ticks_at_last_);
        ticks_at_last_ = ticks_ + nticks;
        tempo_us_ -= (static_cast<int32_t>(ticks_per_clock_)
                      - static_cast<int32_t>(tempo_ticks_))
                     * static_cast<int32_t>(tempo_us_)
                     / static_cast<int32_t>(ppqn_out_);
        tempo_ticks_ = 0;
        resync_      = false;
    }
    else
    {
        nticks = (fticks_ + tr_time_) / tempo_us_;
        fticks_ += tr_time_ - nticks * tempo_us_;
        if(!Internal())
        {
            tempo_ticks_ += nticks;
            if(ticks_ - ticks_at_last_ + nticks >= ticks_per_clock_)
            {
                nticks = ticks_per_clock_ - 1 - (ticks_ - ticks_at_last_);
                hold_  = true;
            }
        }
    }

    ticks_ += nticks;

    if(on_tick_)
        for(uint32_t i = 0; i < nticks; i++) on_tick_();
}

void Clock::Reset()
{
    fticks_        = 0;
    ticks_         = 0;
    ticks_at_last_ = 0;
    tempo_ticks_   = 0;
    hold_          = false;
    resync_        = false;
}

} // namespace tspod
