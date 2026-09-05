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
            // pod.seed.PrintLine() truncates at 128 characters, silently. Two
            // shorter lines rather than one that loses its last field.
            pod.seed.PrintLine("cpu %d/%d  mode %d  page %d  setup %d  deg %d held %d  midi %d",
                               static_cast<int>(cpu.GetAvgCpuLoad() * 100.0f),
                               static_cast<int>(cpu.GetMaxCpuLoad() * 100.0f),
                               static_cast<int>(m.Mode()),
                               static_cast<int>(m.CurrentPage()),
                               m.SetupLayer() ? 1 : 0,
                               static_cast<int>(m.Degree()),
                               m.DegreeHeld() ? 1 : 0,
                               panel.Link().Linked() ? 1 : 0);
            const Engine::Stages st  = engine.TakeStages();
            const Panel::MidiTally& mt = panel.Midi();
            // The stage meters are in thousandths of full scale, so a screaming
            // stage is obvious and the one before it says where it came from.
            pod.seed.PrintLine("enc down %d held_ms %d  toggles %d panics %d  notes %d",
                               m.EncDown() ? 1 : 0,
                               static_cast<int>(m.EncHeld() * 1000.0f),
                               m.Toggles(), m.Panics(), m.HeldCount());
            pod.seed.PrintLine("plucks %d  lvl vox %d dry %d wet %d out %d",
                               static_cast<int>(engine.Plucks()),
                               static_cast<int>(st.vox * 1000.0f),
                               static_cast<int>(st.dry * 1000.0f),
                               static_cast<int>(st.wet * 1000.0f),
                               static_cast<int>(st.out * 1000.0f));
            pod.seed.PrintLine("midi on %d off %d cc %d bend %d srt %d drop %d  last t%d c%d %d %d",
                               mt.notes_on, mt.notes_off, mt.ccs, mt.bends,
                               mt.clocks, mt.discarded, mt.last_type, mt.last_ch,
                               mt.last_d0, mt.last_d1);
            pod.seed.PrintLine("br %d ti %d da %d dr %d de %d sh %d rv %d ch %d  bpm %d tr %+d sc %d",
                               static_cast<int>(m.Norm(Param::Brightness) * 100.0f),
                               static_cast<int>(m.Norm(Param::Timbre) * 100.0f),
                               static_cast<int>(m.Norm(Param::Damping) * 100.0f),
                               static_cast<int>(m.Norm(Param::Drive) * 100.0f),
                               static_cast<int>(m.Norm(Param::Density) * 100.0f),
                               static_cast<int>(m.Norm(Param::Shift) * 100.0f),
                               static_cast<int>(m.Norm(Param::Reverb) * 100.0f),
                               static_cast<int>(m.Norm(Param::Chance) * 100.0f),
                               static_cast<int>(engine.Tempo()),
                               static_cast<int>(m.TransposeSemis()),
                               static_cast<int>(engine.ScaleIndex()));
        }
#endif

        System::Delay(1);
    }
}
