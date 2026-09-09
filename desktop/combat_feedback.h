#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Visual-only feedback follows the SAME interpolated HP as the game renderer.
// Nothing here writes back to collision, input prediction, networking or stats.
namespace plane_pet_feedback {
constexpr double kHeartLife = .76, kHitLife = .88;
constexpr unsigned kHz = 50, kHeartFrames = 38, kHitFrames = 44;
constexpr int kHeartCellWidth = 21, kHeartCellHeight = 30;
constexpr int kHeartOriginX = 10, kHeartOriginY = 8;
constexpr int kHitCellWidth = 44, kHitCellHeight = 24;
constexpr int kHitBaseWidth = 29; // Previously 23; ~26% larger in the same HUD.
constexpr double kWarningLife = 1.0, kWarningAttack = .12;
constexpr double kWarningPeak = .62, kWarningBreathPeriod = 2.4;
constexpr int kWarningWidth = 24; // Logical pixels, left/right only.
inline double Unit(double v) { return std::clamp(v, 0.0, 1.0); }
inline double Ease(double v) { v = Unit(v); return v * v * (3 - 2 * v); }
inline double DamagePulse(double age) {
  if (!std::isfinite(age) || age < 0 || age >= kWarningLife) return 0;
  return kWarningPeak * (age < kWarningAttack ? Ease(age / kWarningAttack)
      : 1 - Ease((age - kWarningAttack) / (kWarningLife - kWarningAttack)));
}
inline double LowHealthBreath(double age) {
  if (!std::isfinite(age) || age < 0) return 0;
  constexpr double tau = 6.2831853071795864769;
  return Ease(age / .18) * (.14 + .22 * (1 - std::cos(tau * age / kWarningBreathPeriod)) / 2);
}
inline double SideEdgeFalloff(int distance, int width) {
  if (distance < 0 || distance >= width || width <= 1) return 0;
  const double inward = 1 - distance / double(width - 1);
  return inward * inward;
}
struct HeartPose { double crack = 0, separation = 0, drop = 0, alpha = 1; bool split = false; };
inline HeartPose Heart(double age) {
  HeartPose p;
  p.crack = Unit(age / .12);
  p.split = age >= .12;
  p.separation = 1.25 * Ease((age - .12) / .08);
  const double fall = Unit((age - .50) / .26);
  p.drop = 9.5 * fall * fall; p.alpha = 1 - fall;
  return p;
}
// Complementary pixel cut: a single mask is retained through ALL later frames.
inline int HeartCut(int y) {
  constexpr int cut[12] = {6,6,6,7,6,5,6,7,6,5,6,6};
  return cut[std::clamp(y, 0, 11)];
}
struct HitPose { double scale = 1, alpha = 1, sweep = -1; };
inline HitPose Hit(double age) {
  HitPose p;
  if (age < .08) p.scale = .70 + .55 * Ease(age / .08);
  else if (age < .40) p.scale = 1.25;
  else if (age < .48) p.scale = 1.25 - .15 * Ease((age - .40) / .08);
  else p.scale = 1.10;
  if (age >= .08 && age < .36) p.sweep = (age - .08) / .28;
  p.alpha = age < .48 ? 1 : 1 - Ease((age - .48) / .40);
  return p;
}
struct Stamp { bool active = false; double born = 0; };
class State {
 public:
  void Reset() { *this = State{}; }
  void Observe(double now, const std::array<uint8_t,2> &incoming, bool damageAllowed) {
    if (!std::isfinite(now) || (ready_ && now < last_)) return;
    std::array<uint8_t,2> hp{{std::min<uint8_t>(3,incoming[0]),std::min<uint8_t>(3,incoming[1])}};
    if (ready_) for (unsigned side=0;side<2;++side) {
      // Replayed snapshots and prediction rollback must not restart a heart.
      if (hp[side] > health_[side]) {
        for (unsigned slot=0;slot<hp[side];++slot) hearts_[side][slot].active=false;
        hits_[1-side].active=false;
      }
      if (damageAllowed && hp[side] < health_[side]) {
        bool fresh=false;
        for (unsigned slot=hp[side];slot<health_[side];++slot) {
          const unsigned bit=1U<<slot;
          if (!(seenLoss_[side]&bit)) {
            seenLoss_[side]|=bit; hearts_[side][slot]={true,now}; fresh=true;
          }
        }
        // This duel has two players; the shooter is the opposite of the victim.
        if (fresh) hits_[1-side]={true,now};
      }
    }
    for (unsigned side=0;side<2;++side) {
      if (hp[side]!=1 || !damageAllowed) lowHealth_[side].active=false;
      else if (!lowHealth_[side].active) lowHealth_[side]={true,now};
    }
    health_=hp; ready_=true; last_=now;
  }
  static double Age(const Stamp &stamp, double now, double life) {
    const double age=now-stamp.born;
    return stamp.active && std::isfinite(now) && age>=0 && age<life ? age : -1;
  }
  double HeartAge(unsigned side, unsigned slot, double now) const {
    return side<2 && slot<3 ? Age(hearts_[side][slot],now,kHeartLife) : -1;
  }
  double HitAge(unsigned shooter, double now) const {
    return shooter<2 ? Age(hits_[shooter],now,kHitLife) : -1;
  }
  double WarningAlpha(unsigned viewer, double now, bool playing) const {
    if (!playing || !ready_ || viewer>=2 || !health_[viewer] || !std::isfinite(now)) return 0;
    // Victim-owned, deduplicated damage stamps. Retain an earlier pulse during
    // a rapid second hit, so a new fade-in cannot abruptly clear the red edge.
    double pulse=0;
    for (const auto &stamp : hearts_[viewer])
      pulse=std::max(pulse,DamagePulse(Age(stamp,now,kWarningLife)));
    const double breath=health_[viewer]==1 && lowHealth_[viewer].active
        ? LowHealthBreath(now-lowHealth_[viewer].born) : 0;
    // Continuous cross-fade; entering 1 HP neither stacks opacity nor flickers
    // when the one-shot pulse hands over to the persistent breath.
    return breath + pulse * (1 - breath / kWarningPeak);
  }
  std::array<std::array<Stamp,3>,2> hearts_{};
  std::array<Stamp,2> hits_{};
 private:
  std::array<uint8_t,2> health_{{3,3}};
  std::array<unsigned,2> seenLoss_{};
  std::array<Stamp,2> lowHealth_{};
  double last_=0;
  bool ready_=false;
};
} // namespace plane_pet_feedback
