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
    void UpdateLeds(float dt);

    const ControlModel& Model() const { return model_; }
    const WeatherLink&  Link() const { return link_; }

  private:
    void Hsv(float h, float s, float v, float& r, float& g, float& b);

    daisy::DaisyPod* pod_    = nullptr;
    Engine*          engine_ = nullptr;
    PodLeds*         leds_   = nullptr;
    ControlModel     model_;
    WeatherLink      link_;

    float pluck_flash_ = 0.0f;
    float panic_flash_ = 0.0f;
};

} // namespace tspod
