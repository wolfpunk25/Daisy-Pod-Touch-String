#pragma once
// The clock, from upstream's clk.h.
//
// Generates internal ticks at 48 ppqn and, when told to, disciplines them
// against an external 24 ppqn source. The resync arithmetic is upstream's,
// derived there from the Maximum MIDI Programmer's ToolKit (© 1993-1998
// Paul Messick): count fractional ticks in microseconds, and on every external
// pulse both realign the timeline and nudge the tempo estimate towards what the
// pulses are actually saying.
//
// One change: upstream reads its external clock off a GPIO pin (`#define
// EXTERNAL_SYNC`, a +5 V clock into S31). The Pod has no CV input and does have
// a MIDI socket, so external sync here is MIDI Timing Clock. Same 24 ppqn, same
// arithmetic, different door — and the same choice the Terrarium and Wrangler
// Pod ports made.
//
// The switch between the two is the bottom of the tempo control: any tempo under
// kBPMMin means "follow whatever is coming in". That is upstream's mechanism,
// not an addition — see `Internal()`.
#include <stdint.h>
#include <functional>

namespace tspod {

static constexpr float kBPMMin   = 40.0f;
static constexpr float kBPMRange = 200.0f;

class Clock
{
  public:
    // `interval_us` is how long one call to Tick() represents — one audio block.
    void Init(float interval_us, uint32_t ppqn_in, uint32_t ppqn_out);

    void SetOnTick(std::function<void()> f) { on_tick_ = f; }

    // Audio callback, once per block.
    void Tick()
    {
        if(running_) EmitTicks();
    }

    // One MIDI Timing Clock byte arrived.
    void ExternalPulse();

    void  SetTempo(float norm);
    float Tempo() const { return 60000000.0f / static_cast<float>(tempo_us_); }
    float TempoNorm() const { return raw_tempo_; }

    // True when the tempo control is above the sync threshold, i.e. we are
    // running our own time rather than following someone else's.
    bool Internal() const { return manual_tempo_ >= kBPMMin; }

    // Under external sync this only schedules playback; the first incoming pulse
    // is what actually starts it.
    void Run()
    {
        if(Internal()) running_ = true;
        else about_to_run_ = true;
    }

    void Stop()
    {
        running_      = false;
        about_to_run_ = false;
        Reset();
    }

    bool IsRunning() const { return running_; }

  private:
    void EmitTicks();
    void Reset();

    static uint32_t TempoUs(float bpm)
    {
        return static_cast<uint32_t>(60.0f * 1e6f / bpm);
    }

    std::function<void()> on_tick_;

    float    manual_tempo_ = 120.0f;
    float    raw_tempo_    = -1.0f;
    uint32_t ppqn_out_     = 48;
    uint32_t tr_time_      = 0;
    uint32_t ticks_per_clock_ = 2;
    uint32_t ticks_            = 0;
    uint32_t fticks_           = 0;
    uint32_t ticks_at_last_    = 0;
    uint32_t tempo_ticks_      = 0;
    uint32_t tempo_us_         = 500000;
    bool     hold_             = false;
    bool     resync_           = false;
    bool     running_          = false;
    bool     about_to_run_     = false;
};

} // namespace tspod
