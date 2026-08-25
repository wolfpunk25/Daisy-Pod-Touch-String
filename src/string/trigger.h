#pragma once
// Divides the 48 ppqn clock down to sixteenths, which is the rate the pattern is
// sampled at.
//
// Upstream's trigger.h also carries a swing amount. Nothing ever sets it — grep
// the sketch and Rosa's re-port and `SetSwing` has no caller in either — so it
// is not carried over here rather than being carried over dead. There is no
// control slot free on the Pod to expose it on, and a swing that only exists in
// a header is worse than one that does not exist.
#include <stdint.h>

namespace tspod {

class Trigger
{
  public:
    explicit Trigger(uint32_t ppqn) : per_trigger_(ppqn / 4) {}

    bool Tick()
    {
        if(++counter_ < per_trigger_) return false;
        counter_ = 0;
        return true;
    }

    // Primed so the first tick after a reset fires on the downbeat rather than a
    // sixteenth after it.
    void Reset() { counter_ = per_trigger_; }

  private:
    uint32_t per_trigger_;
    uint32_t counter_ = 0;
};

} // namespace tspod
