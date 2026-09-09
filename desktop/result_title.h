#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace plane_pet_result {
constexpr unsigned kFrames=120, kColumns=10, kRows=12, kHz=50;
constexpr int kWidth=192, kHeight=80, kLeft=24, kWorldTop=100;
constexpr int kWinResource=179, kLostResource=180;
enum class Art { None, Win, Lost };
inline Art Select(bool finished, bool failed, uint8_t viewerSlot, uint8_t winnerSlot) {
  if(!finished || failed || viewerSlot<1 || viewerSlot>2 || winnerSlot<1 || winnerSlot>2) return Art::None;
  return viewerSlot==winnerSlot ? Art::Win : Art::Lost;
}
class Timeline {
 public:
  void Reset() { *this=Timeline{}; }
  void Observe(bool finished,double now,uint64_t round) {
    if(!std::isfinite(now)) return;
    if(!finished) { Reset(); return; }
    if(!active_ || round_!=round) { active_=true; round_=round; start_=now; }
  }
  unsigned Frame(double now) const {
    if(!active_ || !std::isfinite(now) || now<start_) return 0;
    const double frames=(now-start_)*kHz;
    if(!std::isfinite(frames)) return 0;
    // Windows' seconds-valued clock can have an epoch-sized origin. Subtracting
    // two ~1.8e9 values loses sub-microsecond precision; allow 2 microseconds
    // (0.0001 frame) so exact 20ms boundaries do not fall into the prior frame.
    return std::min(kFrames-1,static_cast<unsigned>(std::fmod(frames+1e-4,double(kFrames))));
  }
 private:
  double start_=0;
  uint64_t round_=0;
  bool active_=false;
};
static_assert(kColumns*kRows==kFrames);
static_assert(kLeft*2+kWidth==240 && kWorldTop+kHeight<=205);
} // namespace plane_pet_result
