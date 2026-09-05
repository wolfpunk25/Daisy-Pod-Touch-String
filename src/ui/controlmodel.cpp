#include "controlmodel.h"

namespace tspod {

const PageMap kPages[kNumPages] = {
    { "String",  Param::Brightness, Param::Timbre },
    { "Body",    Param::Damping,    Param::Drive  },
    { "Pattern", Param::Density,    Param::Shift  },
    { "Space",   Param::Reverb,     Param::Chance },
};

// Where everything sits at power-on, as 0..1 of its own range.
const float kBoot[kNumParams] = {
    0.50f,   // Brightness — halfway up a range the voice already halves
    0.35f,   // Timbre
    0.50f,   // Damping
    0.00f,   // Drive — upstream's volume compensation means this is the loudest
    1.00f,   // Density — all sixteen, which is upstream's boot value
    0.00f,   // Shift
    0.25f,   // Reverb
    0.00f,   // Chance — nothing random until asked
    0.45f,   // Tempo — upstream's, about 120 BPM
    0.50f,   // Transpose — centre, no transposition
};

void ControlModel::Init(Engine* engine, float knob1, float knob2)
{
    engine_ = engine;
    for(int i = 0; i < kNumParams; ++i) norm_[i] = kBoot[i];
    RearmKnobs(knob1, knob2);
    PushAll();
    SetMode(ArpMode::Off);
    engine_->SetScaleIndex(scale_);
}

void ControlModel::RearmKnobs(float k1, float k2)
{
    k1_.Rearm(k1);
    k2_.Rearm(k2);
}

int8_t ControlModel::TransposeSemis() const
{
    // 25 positions, kTransMin..kTransMax, centre exactly at 0.5.
    const float v = norm_[static_cast<int>(Param::Transpose)];
    const int   n = static_cast<int>(roundf(v * (kTransMax - kTransMin))) + kTransMin;
    return static_cast<int8_t>(n);
}

uint16_t ControlModel::HeldCount() const
{
    if(!engine_) return 0;
    uint16_t n = 0;
    for(int i = 0; i < 128; ++i)
        if(engine_->IsHeld(static_cast<uint8_t>(i))) n++;
    return n;
}

bool ControlModel::DegreeHeld() const
{
    return engine_ && engine_->IsHeld(engine_->NoteForDegree(degree_));
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
    norm_[i]    = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    PushParam(p);
}

void ControlModel::PushParam(Param p)
{
    const int   i = static_cast<int>(p);
    float       v = norm_[i] + mod_[i];
    if(v < 0.0f) v = 0.0f;
    if(v > 1.0f) v = 1.0f;
    switch(p)
    {
        case Param::Brightness: engine_->SetBrightness(v); break;
        case Param::Timbre:     engine_->SetTimbre(v); break;
        case Param::Damping:    engine_->SetDamping(v); break;
        case Param::Drive:      engine_->SetDrive(v); break;
        case Param::Density:    engine_->SetDensity(v); break;
        case Param::Shift:      engine_->SetShift(v); break;
        case Param::Reverb:     engine_->SetReverb(v); break;
        case Param::Chance:     engine_->SetChance(v); break;
        case Param::Tempo:      engine_->SetTempo(v); break;
        case Param::Transpose:  engine_->SetTranspose(TransposeSemis()); break;   // uses norm_ only; bend is separate
        default: break;
    }
}

// Upstream identifies a note by its scale DEGREE, so changing the scale retunes
// whatever is sounding the instant it changes. This port identifies a note by its
// MIDI note number — which is the whole reason the MIDI socket can play it — and
// the pad resolves a degree to an absolute note at the moment of the press.
// Nothing revisited it afterwards, so the scale control moved a number in the
// debug log and changed nothing anyone could hear on a held chord. Reported from
// the board as "I'm not sure the scale is changing", which is exactly what a
// control that works but is inaudible feels like.
//
// This cannot live in Engine, because by the time a note reaches the held set
// there is no longer anything to say whether it came from a degree or off the
// wire — and notes off the wire must NOT be dragged around by the scale control.
// The pad is the only thing that knows, so the pad is what fixes it up.
void ControlModel::RetunePadNotes()
{
    // With the arp off nothing is held; ToggleNote would fire a pluck instead,
    // so a scale change would machine-gun the whole chord.
    if(mode_ == ArpMode::Off) return;

    for(uint8_t d = 0; d < kScaleSize; ++d)
    {
        const uint8_t old_note = pad_note_[d];
        if(old_note == kNoPadNote) continue;

        const uint8_t new_note = engine_->NoteForDegree(d);
        if(new_note == old_note) continue;   // the scales agree on this degree

        if(engine_->IsHeld(old_note)) engine_->ToggleNote(old_note);
        if(!engine_->IsHeld(new_note)) engine_->ToggleNote(new_note);
        pad_note_[d] = new_note;
    }
}

void ControlModel::PushAll()
{
    for(int i = 0; i < kNumParams; ++i) PushParam(static_cast<Param>(i));
}

void ControlModel::SetMode(ArpMode m)
{
    mode_ = m;
    engine_->SetArpOn(m != ArpMode::Off);
    engine_->SetLatch(m == ArpMode::Latched);
}

void ControlModel::Read(bool btn1, bool btn2, float knob1, float knob2,
                        int enc_inc, bool enc_btn, float dt)
{
    if(!engine_) return;

    panicked_ = false;
    if(touch_left_ > dt) touch_left_ -= dt;
    else touch_left_ = 0.0f;

    // ── Button 1: tap cycles the arp mode, hold opens the setup layer ───────
    if(btn1 && !b1_)
    {
        b1_held_     = 0.0f;
        b1_consumed_ = false;
    }
    if(btn1)
    {
        b1_held_ += dt;
        if(!setup_ && b1_held_ >= kHoldSec)
        {
            // Entering the setup layer: the pots now mean something else, so
            // whatever position they are sitting in has to stop counting.
            setup_ = true;
            RearmKnobs(knob1, knob2);
            touch_left_ = 1.0f;
        }
    }
    if(!btn1 && b1_)
    {
        if(!setup_ && !b1_consumed_)
        {
            SetMode(mode_ == ArpMode::Off       ? ArpMode::On
                    : mode_ == ArpMode::On      ? ArpMode::Latched
                                                : ArpMode::Off);
            touch_left_ = 1.0f;
        }
        if(setup_)
        {
            setup_ = false;
            RearmKnobs(knob1, knob2);
        }
        b1_held_ = 0.0f;
    }
    b1_ = btn1;

    // ── Button 2: page ──────────────────────────────────────────────────────
    if(btn2 && !b2_ && !setup_)
    {
        page_ = static_cast<Page>((static_cast<int>(page_) + 1) % kNumPages);
        RearmKnobs(knob1, knob2);
        touch_left_ = 1.0f;
    }
    b2_ = btn2;

    // ── Encoder button: note pad, or panic when held ────────────────────────
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
            for(auto& n : pad_note_) n = kNoPadNote;
            panics_++;
            enc_consumed_ = true;
            panicked_     = true;
            touch_left_   = 1.2f;
        }
    }
    if(!enc_btn && enc_)
    {
        if(!enc_consumed_)
        {
            const uint8_t note = engine_->NoteForDegree(degree_);
            engine_->ToggleNote(note);
            // Remember it only while it is actually in the held set, so a note
            // the pad has taken out again is not dragged along by a later scale
            // change.
            pad_note_[degree_] = engine_->IsHeld(note) ? note : kNoPadNote;
            toggles_++;
            touch_left_ = 1.0f;
        }
        enc_held_ = 0.0f;
    }
    enc_ = enc_btn;

    // ── Encoder turn: scale in the setup layer, note pad otherwise ──────────
    if(enc_inc != 0)
    {
        if(setup_)
        {
            int s = static_cast<int>(scale_) + enc_inc;
            if(s < 0) s = 0;
            if(s >= kScalesCount) s = kScalesCount - 1;
            if(scale_ != static_cast<uint8_t>(s))
            {
                scale_ = static_cast<uint8_t>(s);
                engine_->SetScaleIndex(scale_);
                RetunePadNotes();
            }
            b1_consumed_ = true;   // this was a setup gesture, not a mode tap
        }
        else
        {
            // Wraps. Eight degrees is a short list and getting from the top back
            // to the bottom should not mean seven clicks the other way.
            int d = (static_cast<int>(degree_) + enc_inc) % kScaleSize;
            if(d < 0) d += kScaleSize;
            degree_ = static_cast<uint8_t>(d);
        }
        touch_left_ = 1.0f;
    }

    // ── Knobs ───────────────────────────────────────────────────────────────
    Param p1, p2;
    if(setup_)
    {
        p1 = Param::Tempo;
        p2 = Param::Transpose;
    }
    else
    {
        p1 = kPages[static_cast<int>(page_)].k1;
        p2 = kPages[static_cast<int>(page_)].k2;
    }

    const float before1 = norm_[static_cast<int>(p1)];
    const float before2 = norm_[static_cast<int>(p2)];
    norm_[static_cast<int>(p1)] = k1_.Apply(knob1, before1);
    norm_[static_cast<int>(p2)] = k2_.Apply(knob2, before2);

    if(norm_[static_cast<int>(p1)] != before1)
    {
        PushParam(p1);
        touch_left_  = 1.0f;
        b1_consumed_ = setup_ ? true : b1_consumed_;
    }
    if(norm_[static_cast<int>(p2)] != before2)
    {
        PushParam(p2);
        touch_left_  = 1.0f;
        b1_consumed_ = setup_ ? true : b1_consumed_;
    }
}

} // namespace tspod
