#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// Presentation only: world-pixel coordinates, monotonic seconds. Never feeds
// back into simulation, collision detection, network packets, or statistics.
namespace plane_pet_fx {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSmokeHalfAngle = kPi / 12.0;
constexpr double kTrailLife = 1.05;
constexpr double kTrailSampleHz = 60.0, kTrailDrift = 58.0;
constexpr double kTrailCoreWidth = 1.5, kTrailGlowWidth = 3.0;
constexpr double kTrailGlowOpacity = .20;
constexpr double kTrailHealthyAlpha = .96, kTrailDamagedAlpha = .38;
constexpr double kHitLife = .48;
constexpr double kExplosionLife = 1.08;
constexpr unsigned kHitFrames = 12, kExplosionFrames = 12, kSmokeVariants = 6;
constexpr unsigned kMaxTrailPoints = 72, kMaxSmoke = 64, kMaxHits = 12;
static_assert(kMaxTrailPoints > kTrailLife * kTrailSampleHz + 2,
    "The bounded history must retain the complete extended trail.");
struct Point { double x = 0, y = 0; };
inline Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
inline Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
inline Point operator*(Point a, double s) { return {a.x * s, a.y * s}; }
inline Point Blend(Point a, Point b, double t) { return a + (b - a) * t; }
inline double Distance(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }
inline double Back(unsigned side) { return side ? -1.0 : 1.0; }
inline Point Wing(Point p, unsigned side, unsigned wing) {
  // D2's 36px sprite: main wing trailing corners, not the small tail fins.
  return p + Point{wing ? 8.0 : -8.5, Back(side) * 1.0};
}
inline Point Engine(Point p, unsigned side) { return p + Point{0, Back(side) * 11.0}; }
inline unsigned Frame(double age, double duration, unsigned count) {
  return std::min(count - 1, static_cast<unsigned>(std::max(0.0, age) / duration * count));
}
struct TrailPoint { Point p{}; double born = 0; bool breakBefore = false; };
struct Smoke {
  Point origin{}, velocity{};
  double born = 0, life = 1, size = 1, angle = 0;
  unsigned variant = 0, side = 0;
  Point At(double now) const { return origin + velocity * std::max(0.0, now - born); }
  double Size(double now) const { return size * (1.0 + .95 * std::clamp((now - born) / life, 0.0, 1.0)); }
  double Alpha(double now) const {
    const double t = std::clamp((now - born) / life, 0.0, 1.0);
    return .70 * (1.0 - t) * (1.0 - t);
  }
};
struct Hit { uint64_t key = 0; unsigned side = 0; Point offset{}; double born = 0; };
struct Explosion { bool started = false; Point p{}; double born = 0; };

class State {
 public:
  void Reset(uint64_t round = 0) {
    *this = State{};
    round_ = round;
    rng_ ^= static_cast<uint32_t>(round ^ (round >> 32U));
    if (!rng_) rng_ = 1;
  }
  void Advance(double now, const std::array<Point, 2> &positions,
               const std::array<uint8_t, 2> &health, bool playing) {
    if (!std::isfinite(now)) return;
    if (clockReady_ && now < last_) return;
    const double dt = clockReady_ ? now - last_ : 0;
    Prune(now);
    for (unsigned side = 0; side < 2; ++side) {
      // Corrections, new rounds and resume gaps must not paint a line across
      // the map or emit hundreds of overdue particles in one update.
      const bool discontinuity = !clockReady_ || dt > .15 ||
          Distance(positions[side], previous_[side]) > 24;
      if (!playing || discontinuity) {
        nextTrail_[side] = now;
        nextSmoke_[side] = now;
        if (discontinuity) for (auto &trail : trails_[side]) trail.clear();
      }
      if (playing && health[side] && !explosions_[side].started) {
        if (previousHealth_[side] != health[side]) nextSmoke_[side] = now;
        unsigned count = 0;
        while (nextTrail_[side] <= now && count++ < 10) {
          const double born = nextTrail_[side];
          const double t = dt > 0 && !discontinuity ? std::clamp((born - last_) / dt, 0.0, 1.0) : 1;
          const auto p = Blend(previous_[side], positions[side], t);
          for (unsigned wing = 0; wing < 2; ++wing) {
            auto &trail = trails_[side][wing];
            if (trail.size() >= kMaxTrailPoints) trail.erase(trail.begin());
            trail.push_back({Wing(p, side, wing), born, discontinuity && count == 1});
          }
          nextTrail_[side] += 1.0 / kTrailSampleHz;
        }
        count = 0;
        while (health[side] < 3 && nextSmoke_[side] <= now && count++ < 4) {
          const double born = nextSmoke_[side];
          const double t = dt > 0 && !discontinuity ? std::clamp((born - last_) / dt, 0.0, 1.0) : 1;
          const double theta = Random(-kSmokeHalfAngle, kSmokeHalfAngle);
          const double speed = Random(19, 27);
          Smoke puff;
          puff.origin = Engine(Blend(previous_[side], positions[side], t), side);
          puff.velocity = {std::sin(theta) * speed, Back(side) * std::cos(theta) * speed};
          puff.born = born; puff.life = Random(.70, 1.0);
          puff.size = health[side] == 1 ? Random(10, 13) : Random(6, 8);
          puff.angle = Random(-20, 20); puff.side = side;
          puff.variant = static_cast<unsigned>(Random(0, kSmokeVariants)) % kSmokeVariants;
          if (smoke_.size() >= kMaxSmoke) smoke_.erase(smoke_.begin());
          smoke_.push_back(puff);
          // Independent jitter, never a metronomic chain or alternating sides.
          nextSmoke_[side] += health[side] == 1 ? Random(.075, .115) : Random(.22, .32);
        }
      }
    }
    positions_ = previous_ = positions;
    health_ = previousHealth_ = health;
    last_ = now; clockReady_ = true;
  }
  bool Impact(uint64_t key, unsigned side, Point offset, double now, double age = 0) {
    if (!key || side > 1 || age < 0 || age >= kHitLife) return false;
    if (std::find(seen_.begin(), seen_.end(), key) != seen_.end()) return false;
    // There are at most six damaging contacts per round. Bounded defensively.
    if (seen_.size() >= 64) seen_.erase(seen_.begin());
    seen_.push_back(key);
    if (hits_.size() >= kMaxHits) hits_.erase(hits_.begin());
    hits_.push_back({key, side, offset, now - age});
    return true;
  }
  void Finish(double now, bool destroyed, const std::array<Point, 2> &positions,
              const std::array<uint8_t, 2> &health) {
    if (finished_) return;
    finished_ = true;
    positions_ = positions; health_ = health;
    if (!destroyed) return;
    for (unsigned side = 0; side < 2; ++side) if (!health[side])
      explosions_[side] = {true, positions[side] + Point{0, -Back(side) * 5.0}, now};
  }
  bool Finishing(double now) const {
    for (const auto &e : explosions_) if (e.started && now - e.born < kExplosionLife) return true;
    return false;
  }
  bool HidePlane(unsigned side, double now) const {
    return explosions_[side].started && now - explosions_[side].born >= .09;
  }
  Point TrailAt(const TrailPoint &p, unsigned side, double now) const {
    return p.p + Point{0, Back(side) * kTrailDrift * std::max(0.0, now - p.born)};
  }
  double TrailAlpha(const TrailPoint &p, unsigned side, double now) const {
    const double age = std::clamp((now - p.born) / kTrailLife, 0.0, 1.0);
    return (health_[side] <= 1 ? kTrailDamagedAlpha : kTrailHealthyAlpha) *
        std::pow(1.0 - age, 1.25);
  }
  uint64_t round_ = 0;
  std::array<Point, 2> positions_{};
  std::array<uint8_t, 2> health_{{3, 3}};
  std::array<std::array<std::vector<TrailPoint>, 2>, 2> trails_{};
  std::vector<Smoke> smoke_;
  std::vector<Hit> hits_;
  std::array<Explosion, 2> explosions_{};
  bool finished_ = false;
 private:
  double Random(double low, double high) {
    rng_ ^= rng_ << 13U; rng_ ^= rng_ >> 17U; rng_ ^= rng_ << 5U;
    return low + (high - low) * (rng_ / 4294967296.0);
  }
  void Prune(double now) {
    for (auto &actor : trails_) for (auto &trail : actor)
      trail.erase(std::remove_if(trail.begin(), trail.end(), [now](const TrailPoint &p) { return now - p.born >= kTrailLife; }), trail.end());
    smoke_.erase(std::remove_if(smoke_.begin(), smoke_.end(), [now](const Smoke &p) { return now - p.born >= p.life; }), smoke_.end());
    hits_.erase(std::remove_if(hits_.begin(), hits_.end(), [now](const Hit &p) { return now - p.born >= kHitLife; }), hits_.end());
  }
  std::vector<uint64_t> seen_;
  std::array<Point, 2> previous_{};
  std::array<uint8_t, 2> previousHealth_{{3, 3}};
  std::array<double, 2> nextTrail_{}, nextSmoke_{};
  double last_ = 0;
  bool clockReady_ = false;
  uint32_t rng_ = 0x71834abcU;
};
} // namespace plane_pet_fx
