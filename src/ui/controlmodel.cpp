#include "controlmodel.h"

namespace tspod {

void ControlModel::Init(Engine* engine, float knob1, float knob2)
{
    engine_ = engine;

    // The two knobs ARE their values. There is no boot position to restore and
    // nothing to anchor: whatever brightness and density the pots are pointing
    // at is what the instrument comes up on, which is the readability the old
    // relative scheme gave away. The 200 ms ADC spin in panel.cpp still matters
    // — these have to be real readings, not the smoothing filter's ramp.
    norm_[static_cast<int>(Param::Brightness)] = Clamp(knob1);
    norm_[static_cast<int>(Param::Density)]    = Clamp(knob2);

    norm_[static_cast<int>(Param::Reverb)] = 0.25f;
    norm_[static_cast<int>(Param::Chance)] = kChanceSteps[0];
    norm_[static_cast<int>(Param::Tempo)]  = 0.45f;   // upstream's, ~115 BPM

    // Off the panel entirely now.
    norm_[static_cast<int>(Param::Timbre)]  = kTimbreDefault;
    norm_[static_cast<int>(Param::Damping)] = kDampingDefault;
    norm_[static_cast<int>(Param::Drive)]   = kDriveDefault;

    PushAll();
    engine_->SetShift(kShiftDefault);
    engine_->SetTranspose(kTransposeDefault);
    engine_->SetScaleIndex(scale_);
    engine_->SetExciter(exciter_);
    SetMode(ArpMode::Off);
}

void ControlModel::PushParam(Param p)
{
    const int i = static_cast<int>(p);
    float     v = norm_[i] + mod_[i];
    if(v < 0.0f) v = 0.0f;
    if(v > 1.0f) v = 1.0f;

    switch(p)
    {
        case Param::Brightness: engine_->SetBrightness(v); break;
        case Param::Density:    engine_->SetDensity(v); break;
        case Param::Reverb:     engine_->SetReverb(v); break;
        case Param::Chance:     engine_->SetChance(v); break;
        case Param::Tempo:      engine_->SetTempo(v); break;
        case Param::Timbre:     engine_->SetTimbre(v); break;
        case Param::Damping:    engine_->SetDamping(v); break;
        case Param::Drive:      engine_->SetDrive(v); break;
        default: break;
    }
}

void ControlModel::PushAll()
{
    for(int i = 0; i < kNumParams; ++i) PushParam(static_cast<Param>(i));
}

void ControlModel::SetMod(Param p, float offset)
{
    const int i = static_cast<int>(p);
    if(mod_[i] == offset) return;
    mod_[i] = offset;
    PushParam(p);
}

void ControlModel::SetNorm(Param p, float value)
{
    const int i = static_cast<int>(p);
    norm_[i]    = Clamp(value);
    PushParam(p);
}

void ControlModel::SetScaleIndex(uint8_t index)
{
    if(index >= kScalesCount || index == scale_) return;
    scale_ = index;
    engine_->SetScaleIndex(scale_);
}

void ControlModel::SetMode(ArpMode m)
{
    mode_ = m;
    engine_->SetArpOn(m != ArpMode::Off);
    engine_->SetLatch(m == ArpMode::Latched);
}

void ControlModel::SetExciterMode(Exciter e)
{
    exciter_ = e;
    engine_->SetExciter(e);
}

// Four steps rather than a knob: this is a character you choose, not something
// you ride, and it frees the encoder's turn for reverb.
void ControlModel::StepChance()
{
    chance_step_ = static_cast<uint8_t>((chance_step_ + 1) % 4);
    SetNorm(Param::Chance, kChanceSteps[chance_step_]);
}

void ControlModel::Read(bool btn1, bool btn2, float knob1, float knob2,
                        int enc_inc, bool enc_btn, float dt)
{
    if(!engine_) return;

    panicked_ = false;
    if(touch_left_ > dt) touch_left_ -= dt;
    else touch_left_ = 0.0f;

    // ── Button 1: tap cycles the arp mode, hold hands the encoder the tempo ──
    if(btn1 && !b1_)
    {
        b1_held_     = 0.0f;
        b1_consumed_ = false;
    }
    if(btn1)
    {
        b1_held_ += dt;
        if(!tempo_layer_ && b1_held_ >= kHoldSec)
        {
            tempo_layer_ = true;
            Touch();
        }
    }
    if(!btn1 && b1_)
    {
        if(!tempo_layer_ && !b1_consumed_)
        {
            SetMode(mode_ == ArpMode::Off  ? ArpMode::On
                    : mode_ == ArpMode::On ? ArpMode::Latched
                                           : ArpMode::Off);
            Touch();
        }
        tempo_layer_ = false;
        b1_held_     = 0.0f;
    }
    b1_ = btn1;

    // ── Button 2: the exciter ───────────────────────────────────────────────
    if(btn2 && !b2_)
    {
        SetExciterMode(exciter_ == Exciter::Pluck ? Exciter::Bow : Exciter::Pluck);
        Touch();
    }
    b2_ = btn2;

    // ── Encoder button: chance step, or panic when held ─────────────────────
    if(enc_btn && !enc_)
    {
        enc_held_     = 0.0f;
        enc_consumed_ = false;
    }
    if(enc_btn)
    {
        enc_held_ += dt;
        // Fires on reaching the threshold rather than on release, so the panic
        // happens in the hand rather than after it lets go.
        if(!enc_consumed_ && enc_held_ >= kPanicSec)
        {
            engine_->AllNotesOff();
            enc_consumed_  = true;
            panicked_      = true;
            panic_latched_ = true;
            touch_left_    = 1.2f;
        }
    }
    if(!enc_btn && enc_)
    {
        if(!enc_consumed_)
        {
            StepChance();
            Touch();
        }
        // The hand has moved and the eye is free: now the confirmation can fade.
        panic_latched_ = false;
        enc_held_      = 0.0f;
    }
    enc_ = enc_btn;

    // ── Encoder turn: reverb, or the tempo while button 1 is down ───────────
    if(enc_inc != 0)
    {
        if(tempo_layer_)
        {
            SetNorm(Param::Tempo,
                    norm_[static_cast<int>(Param::Tempo)]
                        + static_cast<float>(enc_inc) * kEncTempoStep);
            b1_consumed_ = true;   // a tempo gesture, not a mode tap
        }
        else
        {
            SetNorm(Param::Reverb,
                    norm_[static_cast<int>(Param::Reverb)]
                        + static_cast<float>(enc_inc) * kEncStep);
        }
        Touch();
    }

    // ── The two knobs. Absolute, permanently assigned, never borrowed ───────
    // No arming, no anchoring, no deadband beyond the ADC's own noise: the pot
    // position IS the parameter, which is the entire reason for the rewrite.
    const int   bi = static_cast<int>(Param::Brightness);
    const int   di = static_cast<int>(Param::Density);
    const float k1 = Clamp(knob1);
    const float k2 = Clamp(knob2);

    // A floor so a parked pot's ADC jitter does not keep the panel awake or
    // spam the engine. Same figure every port in this family settled on.
    static constexpr float kFloor = 0.002f;

    if(fabsf(k1 - norm_[bi]) > kFloor)
    {
        norm_[bi] = k1;
        PushParam(Param::Brightness);
        Touch();
    }
    if(fabsf(k2 - norm_[di]) > kFloor)
    {
        norm_[di] = k2;
        PushParam(Param::Density);
        Touch();
    }
}

} // namespace tspod
