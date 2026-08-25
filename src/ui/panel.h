#pragma once
// The Pod half of the panel: read the actual controls, light the actual LEDs,
// parse the actual MIDI. Everything about what the controls MEAN lives in
// controlmodel.h, which has no hardware in it and is what tests/ drives.
#include "daisy_pod.h"

#include "../hw/pod_leds.h"
#include "../midi/weather.h"
#include "../string/engine.h"
#include "controlmodel.h"

namespace tspod {

class Panel
{
  public:
    void Init(daisy::DaisyPod* pod, Engine* engine, PodLeds* leds);

    // Main loop, ~1 kHz.
    void ProcessControls(float dt);
    void ProcessMidi();

    // The UART delivers a burst of garbage as it settles — measured on hardware,
    // 128 stray note-offs on every single boot with nothing plugged in. Note-offs
    // are harmless; a stray note-on would pluck the string before anyone touched
    // it, and a stray controller would move a parameter out from under the panel.
    // So the first fraction of a second off the socket is drained and discarded.
    static constexpr float kMidiSettleSec = 0.25f;
    void UpdateLeds(float dt);

    const ControlModel& Model() const { return model_; }
    const WeatherLink&  Link() const { return link_; }

#if TS_DEBUG
    // Everything that has come off the MIDI socket since boot. A spurious byte
    // as the UART settles is the difference between the host and the board that
    // is worth being able to see.
    struct MidiTally
    {
        uint16_t notes_on = 0, notes_off = 0, ccs = 0, bends = 0, clocks = 0,
                 other = 0;
        uint8_t  last_type = 0, last_ch = 0, last_d0 = 0, last_d1 = 0;
        uint16_t discarded = 0;
    };
    const MidiTally& Midi() const { return midi_; }
#endif

  private:
    void Hsv(float h, float s, float v, float& r, float& g, float& b);

    daisy::DaisyPod* pod_    = nullptr;
    Engine*          engine_ = nullptr;
    PodLeds*         leds_   = nullptr;
    ControlModel     model_;
    WeatherLink      link_;

    float pluck_flash_ = 0.0f;
    float panic_flash_ = 0.0f;
    float midi_settle_ = kMidiSettleSec;
#if TS_DEBUG
    MidiTally midi_;
#endif
};

} // namespace tspod
