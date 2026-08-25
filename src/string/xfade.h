#pragma once
// The square law crossfade, from upstream's xfade.h. Adopted there from
// Will C. Pirkle, "Designing Software Synthesizer Plugins in C++".
namespace tspod {

class XFade
{
  public:
    void Set(float value)
    {
        const float v  = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        const float sq = v * v;
        dry_           = 1.0f - sq;
        wet_           = 2.0f * v - sq;
    }
    float Dry() const { return dry_; }
    float Wet() const { return wet_; }

  private:
    float dry_ = 1.0f;
    float wet_ = 0.0f;
};

} // namespace tspod
