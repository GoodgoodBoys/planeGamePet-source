#include "../desktop/combat_feedback.h"
#include "../desktop/game_layout.h"
#include "../desktop/result_title.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace plane_pet_feedback;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n",__LINE__,#c); std::exit(1); } } while(0)
int main() {
  State s; s.Observe(1,{{3,3}},false); s.Observe(2,{{3,2}},true);
  CHECK(s.HitAge(0,2)==0 && s.HitAge(1,2)<0);
  CHECK(s.HeartAge(1,2,2)==0 && s.HeartAge(0,2,2)<0);
  s.Observe(2.1,{{3,2}},true); CHECK(s.hits_[0].born==2);
  s.Observe(2.2,{{2,1}},true);
  CHECK(s.HitAge(0,2.2)==0 && s.HitAge(1,2.2)==0);
  CHECK(s.hearts_[1][2].born==2 && s.hearts_[1][1].born==2.2);
  s.Observe(2.3,{{0,0}},true);
  CHECK(s.HeartAge(0,0,2.3)==0 && s.HeartAge(0,1,2.3)==0 && s.HeartAge(1,0,2.3)==0);
  CHECK(s.HeartAge(0,0,3.061)<0 && s.HitAge(1,3.181)<0);
  s.Observe(2.4,{{3,3}},false); s.Observe(2.5,{{0,0}},true);
  CHECK(s.HitAge(0,2.5)<0 && s.HitAge(1,2.5)<0); // rollback/replay suppressed
  s.Reset(); s.Observe(4,{{3,3}},false); s.Observe(4.1,{{2,3}},true);
  CHECK(s.HitAge(1,4.1)==0); // new round re-arms the slot
  s.Reset(); s.Observe(5,{{1,2}},true);
  CHECK(s.HitAge(0,5)<0 && s.HitAge(1,5)<0); // initial join is not an impact
  s.Observe(6,{{0,0}},false); CHECK(s.HitAge(0,6)<0); // disconnect is not a hit
  s.Observe(std::numeric_limits<double>::quiet_NaN(),{{0,0}},true);
  s.Observe(3,{{0,0}},true); CHECK(s.HitAge(0,6)<0);
  s.Reset(); s.Observe(1,{{255,255}},false); s.Observe(2,{{1,3}},true);
  CHECK(s.HeartAge(0,1,2)==0 && s.HeartAge(0,2,2)==0);
  CHECK(s.HeartAge(2,3,2)<0 && s.HitAge(2,2)<0);
  for (unsigned i=10;i<=25;++i) {
    auto p=Heart(i*.02); CHECK(p.split && p.separation==1.25 && p.drop==0 && p.alpha==1);
  }
  CHECK(Heart(.1).crack>0 && !Heart(.1).split);
  CHECK(Heart(.64).drop>0 && Heart(.64).alpha<1 && Heart(.76).alpha==0);
  for (int y=0;y<12;++y) for (int x=0;x<13;++x)
    CHECK(int(x<HeartCut(y))+int(x>=HeartCut(y))==1);
  CHECK(HeartCut(3)!=HeartCut(5));
  for (int ms=80;ms<400;ms+=10) CHECK(Hit(ms*.001).scale==1.25);
  CHECK(Hit(.359).sweep>=0 && Hit(.361).sweep<0 && Hit(.42).scale<1.25);
  CHECK(Hit(.88).alpha==0);
  CHECK(kHitBaseWidth==29 && std::lround(kHitBaseWidth*Hit(.12).scale)==36);
  CHECK(DamagePulse(-1)==0 && DamagePulse(0)==0 && DamagePulse(1)==0);
  CHECK(std::abs(DamagePulse(.12)-kWarningPeak)<1e-12);
  CHECK(DamagePulse(.06)>0 && DamagePulse(.06)<DamagePulse(.12));
  CHECK(DamagePulse(.5)>DamagePulse(.9));
  CHECK(LowHealthBreath(0)==0 && LowHealthBreath(1.2)>LowHealthBreath(2.4));
  CHECK(std::abs(LowHealthBreath(2.5)-LowHealthBreath(4.9))<1e-12);
  State warning; warning.Observe(1,{{3,3}},false); warning.Observe(2,{{2,3}},true);
  CHECK(warning.WarningAlpha(0,2,true)==0);
  CHECK(warning.WarningAlpha(0,2.12,true)>0 && warning.WarningAlpha(1,2.12,true)==0);
  CHECK(warning.WarningAlpha(0,3.01,true)==0);
  const double ongoing=warning.WarningAlpha(0,2.30,true);
  warning.Observe(2.30,{{1,3}},true);
  CHECK(std::abs(warning.WarningAlpha(0,2.30,true)-ongoing)<1e-12);
  for(int ms=0;ms<20000;++ms) {
    const double alpha=warning.WarningAlpha(0,2.30+ms*.001,true);
    CHECK(alpha>=0 && alpha<=kWarningPeak+1e-12);
    if(ms>1000) CHECK(alpha>=.14-1e-12);
    CHECK(warning.WarningAlpha(1,2.30+ms*.001,true)==0);
  }
  CHECK(warning.WarningAlpha(0,2.42,false)==0);
  CHECK(warning.WarningAlpha(2,2.42,true)==0);
  CHECK(warning.WarningAlpha(0,std::numeric_limits<double>::quiet_NaN(),true)==0);
  warning.Observe(2.4,{{1,3}},true); // Repeated snapshot must not restart breathing.
  CHECK(std::abs(warning.WarningAlpha(0,3.50,true)-LowHealthBreath(1.20))<1e-12);
  warning.Observe(4,{{3,3}},false); warning.Observe(4.1,{{1,3}},true);
  CHECK(warning.WarningAlpha(0,4.1,true)==0); // No replayed damage pulse.
  warning.Observe(5,{{0,3}},true); CHECK(warning.WarningAlpha(0,5.12,true)==0);
  warning.Reset(); CHECK(warning.WarningAlpha(0,6,true)==0);
  warning.Observe(6,{{1,3}},true); // Joining at 1 HP breathes without a fake hit.
  CHECK(warning.HitAge(1,6.12)<0);
  CHECK(std::abs(warning.WarningAlpha(0,7.2,true)-LowHealthBreath(1.2))<1e-12);
  CHECK(SideEdgeFalloff(0,24)==1 && SideEdgeFalloff(23,24)==0);
  CHECK(SideEdgeFalloff(-1,24)==0 && SideEdgeFalloff(24,24)==0 && SideEdgeFalloff(0,1)==0);
  using namespace plane_pet_ui;
  for (int scale : {1,2,3,4}) for(int i=0;i<3;++i) {
    int ownLeft=(kGameOwnHeartX+i*kGameHeartPitch-kGameHeartWidth/2)*scale;
    int peerLeft=(kGamePeerHeartX+(2-i)*kGameHeartPitch-kGameHeartWidth/2)*scale;
    CHECK(ownLeft+peerLeft+kGameHeartWidth*scale==kGameClientWidth*scale);
  }
  for(int scale : {1,2,3,4}) for(int x=1;x<kWarningWidth*scale;++x)
    CHECK(SideEdgeFalloff(x,kWarningWidth*scale)<=SideEdgeFalloff(x-1,kWarningWidth*scale));
  {
    using namespace plane_pet_result;
    CHECK(Select(true,false,1,1)==Art::Win && Select(true,false,2,1)==Art::Lost);
    CHECK(Select(true,false,2,2)==Art::Win && Select(true,false,1,2)==Art::Lost);
    CHECK(Select(true,false,1,0)==Art::None && Select(true,true,1,1)==Art::None);
    CHECK(Select(false,false,1,1)==Art::None && Select(true,false,0,1)==Art::None);
    Timeline timeline; CHECK(timeline.Frame(1)==0);
    timeline.Observe(true,100,1);
    for(unsigned i=0;i<120;++i) CHECK(timeline.Frame(100+i*.02)==i);
    CHECK(timeline.Frame(102.4)==0 && timeline.Frame(102.42)==1);
    for(double origin : {1788874057.285373926,2000000000.9,2147483648.25}) {
      Timeline epoch; epoch.Observe(true,origin,1);
      for(unsigned i=0;i<360;++i) CHECK(epoch.Frame(origin+i*.02)==i%120);
      CHECK(epoch.Frame(origin+.0199)==0 && epoch.Frame(origin+.0201)==1);
    }
    timeline.Observe(true,103,1); CHECK(timeline.Frame(103)==30); // No repeated-snapshot reset.
    timeline.Observe(true,104,2); CHECK(timeline.Frame(104)==0); // New round re-arms.
    timeline.Observe(false,104.1,2); CHECK(timeline.Frame(105)==0);
    timeline.Observe(true,105,2); CHECK(timeline.Frame(105.2)==10);
    CHECK(timeline.Frame(104)==0 && timeline.Frame(std::numeric_limits<double>::quiet_NaN())==0);
    timeline.Observe(true,std::numeric_limits<double>::quiet_NaN(),2); CHECK(timeline.Frame(105.2)==10);
    timeline.Reset(); CHECK(timeline.Frame(106)==0);
    std::puts("RESULT_TITLE_TIMELINE_OK 120frames_2400ms=1 repeat_no_restart=1 round_reset=1 winner_both_views=1 draw_abort_fallback=1");
  }
  std::puts("COMBAT_FEEDBACK_OK shooter_ownership=1 both_views=1 multi_hp=1 duplicate_rollback=1 reset=1 disconnect=1 stage05_hold_300ms=1 immutable_cut=1 shine_before_shrink=1 symmetry_1x_2x_3x_4x=1 enlarged_hit=1 warning_victim_only=1 warning_rise_fall=1 breathing_2_4s=1 rapid_hit_continuity=1 warning_end_reset=1");
}
