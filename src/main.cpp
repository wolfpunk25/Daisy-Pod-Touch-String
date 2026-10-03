// TouchString for the Daisy Pod.
//
// Upstream runs on the Synthux Simple Touch: twelve capacitive pads, eight
// knobs, two faders and two three-position switches. The Pod has two knobs, two
// buttons, an encoder and two LEDs, so the whole port is a question of what to
// fold and what to drop — see src/ui/controlmodel.h for the answer and
// docs/PORTING.md for the reasoning.
#include "daisy_pod.h"

#include "hw/pod_leds.h"
#include "string/engine.h"
#include "ui/panel.h"

using namespace daisy;
using namespace tspod;

static DaisyPod pod;
static Engine   engine;
static PodLeds  leds;
static Panel    panel;

#if TS_DEBUG
static CpuLoadMeter cpu;
#endif

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
#if TS_DEBUG
    cpu.OnBlockStart();
#endif

    engine.Process(out[0], out[1], size);

    // One PWM step per block. The pins cannot change faster than this anyway —
    // calling it per sample only bunched four writes into a few microseconds and
    // threw the extra resolution away. See hw/pod_leds.h.
    leds.Update();

#if TS_DEBUG
    cpu.OnBlockEnd();
#endif
}

int main(void)
{
    pod.Init();
    pod.SetAudioBlockSize(4);   // upstream's, and the clock is timed against it
    pod.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    const float sr    = pod.AudioSampleRate();
    const float block = static_cast<float>(pod.AudioBlockSize());

    leds.Init();
    leds.SelfTest();

    engine.Init(sr, block);

#if TS_DEBUG
    pod.seed.StartLog(false);
    cpu.Init(sr, pod.AudioBlockSize());
#endif

    pod.StartAdc();
    panel.Init(&pod, &engine, &leds);
    pod.StartAudio(AudioCallback);

    uint32_t last = System::GetNow();
#if TS_DEBUG
    uint32_t last_log = last;
#endif

    while(1)
    {
        const uint32_t now = System::GetNow();
        const float    dt  = static_cast<float>(now - last) * 0.001f;
        last               = now;

        panel.ProcessMidi();
        panel.ProcessControls(dt);
        panel.UpdateLeds(dt);

#if TS_DEBUG
        // 10 Hz while anything is moving, 1 Hz when nothing is. At 1 Hz you
        // cannot tell a knob turned just before a button release from one turned
        // just after, which is the question the log exists to answer.
        const uint32_t interval = panel.Model().TouchLeft() > 0.0f ? 100 : 1000;
        if(now - last_log >= interval)
        {
            last_log            = now;
            const ControlModel& m = panel.Model();
            // pod.seed.PrintLine() truncates at 128 characters, silently.
            pod.seed.PrintLine("cpu %d/%d  arp %d  exciter %d  poly %d  voices %d  chance %d  sc %d  midi %d",
                               static_cast<int>(cpu.GetAvgCpuLoad() * 100.0f),
                               static_cast<int>(cpu.GetMaxCpuLoad() * 100.0f),
                               static_cast<int>(m.Mode()),
                               static_cast<int>(m.GetExciter()),
                               m.Poly() ? 1 : 0,
                               static_cast<int>(engine.ActiveVoices()),
                               static_cast<int>(m.ChanceStep()),
                               static_cast<int>(m.ScaleIndex()),
                               panel.Link().Linked() ? 1 : 0);
            // GetMaxCpuLoad() is a maximum since the last Reset(), so without
            // this every "peak" reading is just the worst moment since boot and
            // cannot be attributed to the state it is printed beside. That
            // misled the voice-count decision once already.
            cpu.Reset();
            const Engine::Stages st    = engine.TakeStages();
            const Panel::MidiTally& mt = panel.Midi();
            pod.seed.PrintLine("bright %d dens %d verb %d  bpm %d  notes %d plucks %d",
                               static_cast<int>(m.Norm(Param::Brightness) * 100.0f),
                               static_cast<int>(m.Norm(Param::Density) * 100.0f),
                               static_cast<int>(m.Norm(Param::Reverb) * 100.0f),
                               static_cast<int>(engine.Tempo()),
                               static_cast<int>(engine.HeldCount()),
                               static_cast<int>(engine.Plucks()));
            pod.seed.PrintLine("lvl vox %d dry %d wet %d out %d   midi on %d off %d cc %d drop %d",
                               static_cast<int>(st.vox * 1000.0f),
                               static_cast<int>(st.dry * 1000.0f),
                               static_cast<int>(st.wet * 1000.0f),
                               static_cast<int>(st.out * 1000.0f),
                               mt.notes_on, mt.notes_off, mt.ccs, mt.discarded);
        }
#endif

        System::Delay(1);
    }
}
