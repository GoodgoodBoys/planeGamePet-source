#include "../desktop/game_effects.h"
#include <cstdio>
#include <cstdlib>
#include <set>
#include <limits>
using namespace plane_pet_fx;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (0)

struct Metrics { unsigned emitted = 0; double size = 0; };
Metrics Run(unsigned hp, unsigned hz) {
  State effects; effects.Reset(61);
  std::set<double> births;
  Metrics m;
  for (unsigned step = 0; step <= hz * 10; ++step) {
    const double now = step / double(hz);
    effects.Advance(now, {{{120, 240}, {119, 80}}}, {{uint8_t(hp), 3}}, true);
    for (const auto &p : effects.smoke_) {
      if (births.insert(p.born).second) { ++m.emitted; m.size += p.size; }
      CHECK(std::abs(std::atan2(p.velocity.x, std::abs(p.velocity.y))) <= kSmokeHalfAngle + 1e-10);
      CHECK(p.velocity.y > 0 && p.variant < 6);
      CHECK(p.At(now).y >= p.origin.y && p.Size(now + .1) >= p.Size(now));
      CHECK(p.Alpha(now + .1) <= p.Alpha(now));
      CHECK(std::abs(p.At(now).x - p.origin.x) <= std::abs(p.At(now).y - p.origin.y) * std::tan(kSmokeHalfAngle) + 1e-8);
    }
    CHECK(effects.smoke_.size() <= kMaxSmoke);
    CHECK(effects.trails_[0][0].size() <= kMaxTrailPoints);
  }
  if (m.emitted) m.size /= m.emitted;
  return m;
}
int main() {
  const auto healthy = Run(3, 60), light = Run(2, 60), heavy = Run(1, 60);
  CHECK(!healthy.emitted && light.emitted >= 30 && light.emitted <= 50);
  CHECK(heavy.emitted > light.emitted * 2 && heavy.size > light.size * 1.4);
  CHECK(Run(1,30).emitted == heavy.emitted && Run(1,144).emitted == heavy.emitted);
  State e; e.Reset(12);
  for (unsigned i = 0; i <= 60; ++i)
    e.Advance(i / 60.0, {{{80 + double(i), 230 + 10 * std::sin(i * .1)}, {119,80}}}, {{3,1}}, true);
  CHECK(e.trails_[0][0].size() > 20 && !e.smoke_.empty());
  const auto old = e.trails_[0][0].front();
  CHECK(old.p.x < Wing(e.positions_[0], 0, 0).x - 15);
  const auto smoke = e.smoke_.back();
  CHECK(smoke.velocity.y < 0); // far/opponent plane emits the opposite direction
  e.Advance(1.016, {{{141, 231}, {122, 82}}}, {{1,1}}, true);
  CHECK(e.TrailAt(old, 0, 1.016).x == old.p.x);
  CHECK(smoke.At(1.016).x != Engine(e.positions_[1], 1).x); // independent of aircraft
  CHECK(e.TrailAlpha(e.trails_[0][0].back(), 0, 1.016) <= kTrailDamagedAlpha);
  e.Advance(5, {{{20,40},{210,280}}}, {{1,1}}, true);
  CHECK(e.trails_[0][0].size() == 1 && e.smoke_.size() == 2); // no resume burst or bridging line
  CHECK(!e.Impact(17,0,{0,-8},5,-.001));
  CHECK(e.Impact(17,0,{0,-8},5));
  CHECK(!e.Impact(17,0,{0,-8},5.1));
  std::set<unsigned> frames;
  for (unsigned i=0;i<12;++i) frames.insert(Frame(i*.04+.001,kHitLife,kHitFrames));
  CHECK(frames.size()==12);
  e.Finish(5,true,e.positions_,{{0,1}});
  const auto started = e.explosions_[0].born;
  e.Finish(5.3,true,e.positions_,{{0,1}});
  CHECK(e.explosions_[0].born == started && !e.explosions_[1].started);
  CHECK(e.Finishing(6.079) && !e.Finishing(6.081));
  CHECK(!e.HidePlane(0,5.01) && e.HidePlane(0,5.10));
  e.Advance(7,e.positions_,{{0,1}},false);
  CHECK(e.hits_.empty() && e.smoke_.empty() && e.trails_[0][0].empty());
  CHECK(!e.Impact(17,0,{},7)); // expired replay stays suppressed
  e.Reset(13); CHECK(e.Impact(17,0,{},7)); // new round can reuse IDs
  e.Finish(7,false,e.positions_,{{0,0}}); CHECK(!e.Finishing(7)); // no explosion on disconnect/draw
  e.Reset(); e.Finish(8,true,e.positions_,{{0,0}});
  CHECK(e.explosions_[0].started && e.explosions_[1].started);
  // A longer lifetime also needs a larger bounded history, otherwise the
  // retained 60Hz samples would silently cut the beam short again.
  for (unsigned hz : {30U,60U,144U}) {
    State trail; trail.Reset(62);
    for (unsigned i=0;i<=hz*2;++i)
      trail.Advance(i/double(hz), {{{120,240},{119,80}}}, {{3,1}}, true);
    const auto &points=trail.trails_[0][0];
    CHECK(points.size()>=62 && points.size()<=kMaxTrailPoints);
    CHECK(2-points.front().born>.99 && 2-points.front().born<=kTrailLife);
    const TrailPoint probe{Wing({120,240},0,0),1.3,false};
    CHECK(trail.TrailAt(probe,0,2).y-probe.p.y>40);
    CHECK(trail.TrailAlpha(probe,0,2)>.20);
    CHECK(trail.TrailAlpha(probe,1,2)<trail.TrailAlpha(probe,0,2)*.41);
    CHECK(trail.TrailAlpha(probe,0,2.351)==0);
  }
  CHECK(kTrailCoreWidth==1.5 && kTrailGlowWidth==3);
  CHECK(kTrailLife==1.05 && kTrailDrift==58 && kTrailGlowOpacity==.20);
  std::printf("EXTENDED_TRAIL_OK life=%.2f width=%.1f halo=%.1f history=%u full_history_fps=30/60/144 damage_fade=1\n",kTrailLife,kTrailCoreWidth,kTrailGlowWidth,kMaxTrailPoints);
  std::printf("GAME_EFFECTS_OK smoke_2hp=%u smoke_1hp=%u sizes=%.2f/%.2f angle=+-15 fps=30/60/144 bounded=1 dedup=1\n",light.emitted,heavy.emitted,light.size,heavy.size);
}
