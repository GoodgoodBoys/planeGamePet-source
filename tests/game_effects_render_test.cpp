// Real production renderer/resources with only a deterministic visual clock.
#define PLANE_PET_EFFECTS_SELF_TEST
#define main IncludedClientRegressionMain
#include "client_edge_test.cpp"
#undef main

namespace {
bool SaveFxFrame(PetClient &client, const std::filesystem::path &path,
                 std::vector<unsigned char> *copy = nullptr, int scale = 1,
                 bool warningOnly = false) {
  const int width=kGameClientWidth*scale, height=kGameClientHeight*scale;
  BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
  void *pixels = nullptr; HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  bool ok = false;
  if (dc && bitmap && pixels) {
    const auto old = SelectObject(dc, bitmap);
    if(warningOnly) {
      RECT all{0,0,width,height}; PetClient::Fill(dc,all,RGB(7,10,19));
      client.DrawDamageWarning(dc,MakeGameLayout(width,height),client.snapshot_,client.slot_-1);
    } else client.DrawGameWindow(dc,width,height);
    GdiFlush();
    if (copy) copy->assign(static_cast<unsigned char *>(pixels),
        static_cast<unsigned char *>(pixels) + width * height * 4);
    BITMAPFILEHEADER header{}; header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + width * height * 4;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(&header), sizeof(header));
    out.write(reinterpret_cast<const char *>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
    out.write(static_cast<const char *>(pixels), width * height * 4);
    ok = out.good(); SelectObject(dc, old);
  }
  if (bitmap) DeleteObject(bitmap);
  if (dc) DeleteDC(dc);
  return ok;
}

unsigned GoldPixels(const std::vector<unsigned char> &pixels, bool right) {
  unsigned count=0;
  for(unsigned y=5;y<30;++y) for(unsigned x=right?140:70;x<(right?171:100);++x) {
    const auto *p=&pixels[(y*240+x)*4];
    count += p[2]>150 && p[1]>90 && p[2]>p[0]*1.4;
  }
  return count;
}

int VerifyFeedback(const std::filesystem::path &folder) {
  using namespace plane_pet_feedback;
  using namespace plane_pet_ui;
  PetClient assets;
  for(unsigned red=0;red<2;++red) for(unsigned hp=1;hp<=2;++hp) {
    auto *original=assets.PetPlaneBitmap(red), *damaged=assets.GamePlaneBitmap(red,hp);
    if(!original || !damaged || original==damaged) return 101;
    unsigned changed=0;
    for(unsigned y=0;y<512;++y) for(unsigned x=0;x<512;++x) {
      Gdiplus::Color a,b; original->GetPixel(x,y,&a); damaged->GetPixel(x,y,&b);
      const bool protect=a.GetA()!=255 || y>=354 || std::min({a.GetR(),a.GetG(),a.GetB()})>120;
      if(a.GetA()!=b.GetA() || (protect && a.GetValue()!=b.GetValue())) return 102;
      changed += a.GetValue()!=b.GetValue();
    }
    if(changed<100) return 103;
    if(assets.GamePlaneBitmap(red,3)!=original) return 104;
  }
  for(unsigned color=0;color<2;++color) for(unsigned f=0;f<kHeartFrames;++f) {
    auto *b=assets.HeartFeedbackBitmap(color,f);
    if(!b || b->GetWidth()!=kHeartCellWidth || b->GetHeight()!=kHeartCellHeight) return 105;
    auto *hold=assets.HeartFeedbackBitmap(color,10);
    if(f>=10 && f<=25) for(int y=0;y<kHeartCellHeight;++y) for(int x=0;x<kHeartCellWidth;++x) {
      Gdiplus::Color a,c; b->GetPixel(x,y,&a); hold->GetPixel(x,y,&c);
      if(a.GetValue()!=c.GetValue()) return 106;
    }
  }
  for(unsigned f=0;f<kHitFrames;++f) {
    auto *word=assets.HitFeedbackBitmap(f); if(!word) return 107;
    // Check actual raster extents, including the pop and slanted shine. The
    // enlarged word never touches hearts, timer, latency or HUD bounds.
    for(int y=0;y<kHitCellHeight;++y) for(int x=0;x<kHitCellWidth;++x) {
      Gdiplus::Color c; word->GetPixel(x,y,&c); if(!c.GetA()) continue;
      const int left=kGameOwnHitX-kHitCellWidth/2+x;
      const int right=kGamePeerHitX-kHitCellWidth/2+x;
      const int row=kGameHitY-kHitCellHeight/2+y;
      if(left<=kGameOwnHeartX+2*kGameHeartPitch+kGameHeartWidth/2+1 || left>=kGameTimerLeft ||
         right<kGameTimerRight || right>=kGamePeerHeartX-kGameHeartWidth/2-1 || row<0 || row>=27) return 113;
    }
  }
  for(unsigned viewer=0;viewer<2;++viewer) for(unsigned target=0;target<2;++target) {
    PetClient c; c.slot_=viewer+1; c.currentRoundId_=600+viewer*2+target;
    c.haveSnapshot_=c.gameMode_=true; c.telemetryEnabled_=false; c.rttMs_=80;
    c.snapshot_.phase=plink::GamePhase::Playing; c.snapshot_.phaseRemainingMs=175000;
    c.snapshot_.players[0]={120,240,3,1}; c.snapshot_.players[1]={119,80,3,1};
    const auto base=Clock::now(); c.fxTestNow_=PetClient::FxSeconds(base); c.AdvanceGameEffects(base);
    c.snapshot_.players[target].health=2;
    const auto hit=base+std::chrono::milliseconds(100);
    c.AdvanceGameEffects(hit);
    for(unsigned frame=0;frame<50;++frame) {
      const auto now=hit+std::chrono::milliseconds(frame*20);
      c.fxTestNow_=PetClient::FxSeconds(now); c.AdvanceGameEffects(now);
      char name[100]; std::snprintf(name,sizeof(name),"feedback-v%u-target%u-%03u.bmp",viewer,target,frame);
      std::vector<unsigned char> pixels;
      if(!SaveFxFrame(c,folder/name,&pixels)) return 108;
      if(frame==12) {
        if((GoldPixels(pixels,false)>0)!=(viewer==1-target) ||
           (GoldPixels(pixels,true)>0)!=(viewer==target)) return 109;
        const auto red=pixels[((kGameHudHeight+100)*240)*4+2];
        if((red>50)!=(viewer==target)) return 114;
      }
      if(frame==48 && (GoldPixels(pixels,false) || GoldPixels(pixels,true))) return 110;
    }
  }
  // New round cancels earlier feedback; draw/disconnect never invent a hit.
  for(auto reason : {plink::MatchEndReason::TimeLimitDraw,plink::MatchEndReason::PlayerDisconnected}) {
    PetClient c; c.haveSnapshot_=true; c.telemetryEnabled_=false; c.currentRoundId_=700;
    c.snapshot_.phase=plink::GamePhase::Playing;
    c.snapshot_.players[0]={120,240,3,1}; c.snapshot_.players[1]={119,80,3,1};
    auto now=Clock::now(); c.AdvanceGameEffects(now);
    c.snapshot_.players[0].health=0; c.snapshot_.phase=plink::GamePhase::Finished; c.snapshot_.endReason=reason;
    now+=std::chrono::milliseconds(20); c.AdvanceGameEffects(now);
    if(c.combatFeedback_.HitAge(1,PetClient::FxSeconds(now))>=0) return 111;
    c.snapshot_.phase=plink::GamePhase::Countdown; c.currentRoundId_=701;
    now+=std::chrono::milliseconds(20); c.AdvanceGameEffects(now);
    if(c.combatFeedback_.HeartAge(0,0,PetClient::FxSeconds(now))>=0) return 112;
  }
  std::puts("REAL_FEEDBACK_OK assets=4+76+44 unchanged_alpha_outline_exhaust=1 stage05_identical=1 shooter_both_views=1 expires=1 round_reset=1 no_disconnect_hit=1");
  return 0;
}

std::vector<unsigned char> ResultPixels(const std::vector<unsigned char> &pixels,int scale=1) {
  using namespace plane_pet_result;
  std::vector<unsigned char> result;
  const int stride=240*scale*4;
  for(int y=(kGameHudHeight+kWorldTop)*scale;y<(kGameHudHeight+kWorldTop+kHeight)*scale;++y) {
    const auto begin=pixels.begin()+y*stride+kLeft*scale*4;
    result.insert(result.end(),begin,begin+kWidth*scale*4);
  }
  return result;
}

int VerifyResultTitles(const std::filesystem::path &folder) {
  using namespace plane_pet_result;
  PetClient assets; auto *win=assets.ResultTitleBitmap(true), *lost=assets.ResultTitleBitmap(false);
  if(!win || !lost) return 140;
  for(unsigned i=0;i<kFrames;++i) {
    unsigned solid=0,clear=0;
    for(int y=0;y<kHeight;++y) for(int x=0;x<kWidth;++x) {
      Gdiplus::Color a,b; win->GetPixel(i%kColumns*kWidth+x,i/kColumns*kHeight+y,&a);
      solid+=a.GetA()>128; clear+=a.GetA()==0;
      if((x==0 || y==0 || x==kWidth-1 || y==kHeight-1) && a.GetA()!=0) return 141;
      if(i>=45) { win->GetPixel(x,y,&b); if(a.GetValue()!=b.GetValue()) return 142; }
    }
    if(solid<1000 || clear<2000) return 143;
  }
  for(unsigned viewer=1;viewer<=2;++viewer) for(unsigned winner=1;winner<=2;++winner) {
    PetClient c; c.slot_=viewer; c.haveSnapshot_=c.gameMode_=true; c.telemetryEnabled_=false;
    c.currentRoundId_=1000+viewer*2+winner; c.rttMs_=80;
    c.snapshot_.phase=plink::GamePhase::Finished; c.snapshot_.endReason=plink::MatchEndReason::Destroyed;
    c.snapshot_.winnerSlot=winner; c.snapshot_.matchElapsedMs=12000;
    c.snapshot_.players[0]={120,280,uint8_t(winner==1?2:0),1};
    c.snapshot_.players[1]={119,40,uint8_t(winner==2?2:0),1};
    // Keep effects outside the title comparison ROI; the live return method
    // also uses wall clock, so the fixture origin is deliberately in the past.
    const auto base=Clock::now()-std::chrono::seconds(5);
    std::vector<unsigned char> first,shine;
    for(unsigned frame : {0U,6U,16U,26U,45U,119U,120U,126U,136U}) {
      const auto now=base+std::chrono::milliseconds(frame*20);
      c.fxTestNow_=PetClient::FxSeconds(now); c.AdvanceGameEffects(now);
      std::vector<unsigned char> pixels;
      const auto name="result-v"+std::to_string(viewer)+"-winner"+std::to_string(winner)+"-"+std::to_string(frame)+".bmp";
      if(c.resultTimeline_.Frame(c.fxTestNow_)!=frame%kFrames) return 155;
      if(!SaveFxFrame(c,folder/name,&pixels)) return 144;
      auto roi=ResultPixels(pixels);
      unsigned gold=0,red=0;
      for(size_t i=0;i<roi.size();i+=4) {
        gold+=roi[i+2]>180 && roi[i+1]>100 && roi[i+1]>roi[i]*1.3;
        red+=roi[i+2]>130 && roi[i+2]>roi[i+1]*1.8 && roi[i+2]>roi[i]*1.8;
      }
      if(frame==0) {
        first=roi;
        if(viewer==winner ? gold<800 : red<800) return 145;
        if(!c.gameEffects_.Finishing(c.fxTestNow_) || !c.GameFinishedVisible()) return 146;
      }
      if(viewer!=winner && roi!=first) return 147; // LOST stays fully static.
      if(frame==16) { shine=roi; if(viewer==winner && roi==first) return 148; }
      if((frame==45 || frame==119 || frame==120) && roi!=first) {
        std::printf("RESULT_HOLD_DIFFERENT frame=%u actual=%u viewer=%u winner=%u\n",frame,c.resultTimeline_.Frame(c.fxTestNow_),viewer,winner);
        return 149;
      }
      if(frame==136 && roi!=shine) {
        for(size_t at=0;at<roi.size();++at) if(roi[at]!=shine[at]) {
          std::printf("RESULT_LOOP_DIFFERENT viewer=%u winner=%u actual_frame=%u pixel=%zu/%zu channel=%zu values=%u/%u clock=%.9f\n",
              viewer,winner,c.resultTimeline_.Frame(c.fxTestNow_),(at/4)%kWidth,(at/4)/kWidth,at%4,roi[at],shine[at],c.fxTestNow_);
          break;
        }
        return 150;
      }
    }
    for(int scale : {2,3,4}) {
      c.fxTestNow_=PetClient::FxSeconds(base)+.32;
      if(!SaveFxFrame(c,folder/("result-scale"+std::to_string(scale)+"-v"+std::to_string(viewer)+"-winner"+std::to_string(winner)+".bmp"),nullptr,scale)) return 151;
    }
    // Result loop is visual only and cannot prolong the explosion return gate.
    c.ReturnToPetFromFinished(); if(c.gameMode_ || !c.returnToPetRequested_) return 152;
  }
  // Preserve Chinese draw/abort captions and defensive missing-art fallback.
  for(unsigned mode=0;mode<4;++mode) {
    PetClient c; c.slot_=1; c.haveSnapshot_=true; c.currentRoundId_=2000+mode;
    c.telemetryEnabled_=false; c.snapshot_.phase=plink::GamePhase::Finished;
    c.snapshot_.players[0]={120,240,2,1}; c.snapshot_.players[1]={119,80,2,1};
    c.snapshot_.winnerSlot=mode==0?0:1; c.syncFailed_=mode==1;
    c.snapshot_.endReason=mode==0?plink::MatchEndReason::TimeLimitDraw:
        mode==1?plink::MatchEndReason::ServerUnavailable:plink::MatchEndReason::PlayerDisconnected;
    c.snapshot_.matchElapsedMs=180000;
    c.fxTestNow_=PetClient::FxSeconds(Clock::now()); c.AdvanceGameEffects(Clock::now());
    if(mode>=2) {
      // Simulate a completed, unsuccessful load (null or malformed dimensions),
      // otherwise EmbeddedBitmap would replace this fixture with the real art.
      c.resultTitleImages_[0].attempted=true;
      if(mode==2) c.resultTitleImages_[0].bitmap=std::make_unique<Gdiplus::Bitmap>(1,1,PixelFormat32bppPARGB);
    }
    HDC dc=CreateCompatibleDC(nullptr);
    const bool drew=c.DrawResultTitle(dc,MakeGameLayout(240,372),c.snapshot_); DeleteDC(dc);
    if(drew) return 153;
    if(!SaveFxFrame(c,folder/("result-fallback-"+std::to_string(mode)+".bmp"))) return 154;
  }
  std::puts("REAL_RESULT_TITLES_OK win=120_frames lost=static both_viewers=1 immediate_with_explosion=1 loop_hold_exact=1 scales_1_2_3_4=1 return_not_blocked=1 draw_abort_missing_fallback=1");
  return 0;
}

int VerifyWarning(const std::filesystem::path &folder) {
  using namespace plane_pet_feedback;
  for(unsigned viewer=0;viewer<2;++viewer) {
    PetClient c; c.slot_=viewer+1; c.currentRoundId_=800+viewer;
    c.haveSnapshot_=c.gameMode_=true; c.telemetryEnabled_=false; c.rttMs_=80;
    c.snapshot_.phase=plink::GamePhase::Playing; c.snapshot_.phaseRemainingMs=175000;
    c.snapshot_.players[0]={120,240,3,1}; c.snapshot_.players[1]={119,80,3,1};
    const auto base=Clock::now(); c.AdvanceGameEffects(base);
    const auto hit=base+std::chrono::milliseconds(100);
    c.snapshot_.players[viewer].health=2; c.AdvanceGameEffects(hit);
    c.fxTestNow_=PetClient::FxSeconds(hit)+.12;
    for(int scale : {1,2,3,4}) {
      std::vector<unsigned char> p;
      const auto name="warning-v"+std::to_string(viewer)+"-scale"+std::to_string(scale)+".bmp";
      if(!SaveFxFrame(c,folder/name,&p,scale,true)) return 120;
      const int width=240*scale, height=372*scale, edge=kWarningWidth*scale;
      const auto at=[&](int x,int y) { return &p[(y*width+x)*4]; };
      for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const auto *a=at(x,y), *b=at(width-1-x,y);
        if(!std::equal(a,a+3,b)) return 121; // Exact left/right mirror.
        if(y<kGameHudHeight*scale || (x>=edge && x<width-edge))
          if(a[0]!=19 || a[1]!=10 || a[2]!=7) return 122; // Includes top/bottom centre.
      }
      for(int x=1;x<edge;++x) if(at(x,100*scale)[2]>at(x-1,100*scale)[2]) return 123;
      if(at(0,100*scale)[2]<100) return 124;
    }
    // Native production frames covering one-shot, sustained breath and ending.
    for(unsigned frame=0;frame<200;++frame) {
      const auto now=hit+std::chrono::milliseconds(frame*20);
      if(frame==60) c.snapshot_.players[viewer].health=1;
      if(frame==185) {
        c.snapshot_.phase=plink::GamePhase::Finished;
        c.snapshot_.endReason=plink::MatchEndReason::Destroyed;
        c.snapshot_.players[viewer].health=0; c.snapshot_.winnerSlot=2-viewer;
      }
      c.fxTestNow_=PetClient::FxSeconds(now); c.AdvanceGameEffects(now);
      char name[80]; std::snprintf(name,sizeof(name),"warning-live-v%u-%03u.bmp",viewer,frame);
      if(!SaveFxFrame(c,folder/name)) return 125;
    }
    c.combatFeedback_.Reset();
    c.combatFeedback_.Observe(c.fxTestNow_-1.2,{{1,1}},true);
    if(c.combatFeedback_.WarningAlpha(viewer,c.fxTestNow_,true)<.3) return 128;
    for(unsigned mode=0;mode<5;++mode) {
      c.snapshot_.players[viewer].health=1;
      c.snapshot_.phase=mode==0?plink::GamePhase::Countdown:
          mode==1?plink::GamePhase::Finished:mode==2?plink::GamePhase::Menu:plink::GamePhase::Playing;
      c.syncFailed_=mode==3; c.haveSnapshot_=mode!=4;
      std::vector<unsigned char> p;
      if(!SaveFxFrame(c,folder/("warning-hidden-v"+std::to_string(viewer)+"-"+std::to_string(mode)+".bmp"),&p,1,true)) return 126;
      for(size_t i=0;i<p.size();i+=4) if(p[i]!=19 || p[i+1]!=10 || p[i+2]!=7) return 127;
    }
  }
  std::puts("REAL_WARNING_OK victim_both_views=1 mirrored_left_right_only=1 hud_centre_top_bottom_untouched=1 falloff_monotonic=1 scales_1_2_3_4=1 playing_only=1 native_frames=400 enlarged_hit_no_overlap=1");
  return 0;
}
}

int main(int argc, char **argv) {
  if (argc != 2 || !gGdiPlusSession.ready()) return 1;
  const auto folder = std::filesystem::u8path(argv[1]);
  std::filesystem::create_directories(folder);
  if(const int result=VerifyFeedback(folder)) { std::printf("FEEDBACK_FAIL code=%d\n",result); return result; }
  if(const int result=VerifyResultTitles(folder)) { std::printf("RESULT_TITLE_FAIL code=%d\n",result); return result; }
  if(const int result=VerifyWarning(folder)) { std::printf("WARNING_FAIL code=%d\n",result); return result; }
  PetClient assets;
  for (unsigned kind = 0; kind < 3; ++kind) for (unsigned frame = 0; frame < (kind == 2 ? 6U : 12U); ++frame) {
    auto *bitmap = assets.EffectBitmap(kind, frame);
    if (!bitmap) return 2;
    unsigned opaque = 0, clear = 0, magenta = 0;
    for (unsigned y = 0; y < bitmap->GetHeight(); ++y) for (unsigned x = 0; x < bitmap->GetWidth(); ++x) {
      Gdiplus::Color c; bitmap->GetPixel(x,y,&c);
      opaque += c.GetA() > 16; clear += c.GetA() == 0;
      magenta += c.GetA() > 32 && c.GetR() > 180 && c.GetB() > 150 && c.GetG() < 70;
    }
    if (!opaque || !clear || magenta) { std::printf("ASSET_FAIL kind=%u frame=%u opaque=%u clear=%u magenta=%u\n",kind,frame,opaque,clear,magenta); return 3; }
  }
  unsigned files = 0;
  const auto renderStart = Clock::now();
  DWORD warmedHandles = 0;
  for (unsigned side = 0; side < 2; ++side) {
    PetClient c; c.slot_ = uint8_t(side + 1); c.currentRoundId_ = 45;
    c.haveSnapshot_ = c.gameMode_ = true; c.telemetryEnabled_ = false; c.rttMs_ = 80;
    const auto base = Clock::now();
    for (unsigned frame = 0; frame < 100; ++frame) {
      const double t = frame * .04, flight = std::min(t, 2.4);
      c.snapshot_.phase = t < 2.4 ? plink::GamePhase::Playing : plink::GamePhase::Finished;
      const uint8_t hp = t < .8 ? 3 : t < 1.6 ? 2 : t < 2.4 ? 1 : 0;
      c.snapshot_.players[0] = {int16_t(std::lround(90 + 35 * std::sin(flight * 3))),
          int16_t(std::lround(231 + 15 * std::cos(flight * 2))), hp, 1};
      c.snapshot_.players[1] = {int16_t(std::lround(142 + 28 * std::cos(flight * 2))),
          int16_t(std::lround(95 + 18 * std::sin(flight * 3))), uint8_t(std::max(1,int(hp))), 1};
      c.snapshot_.onlineMask = 3; c.snapshot_.phaseRemainingMs = t < 2.4 ? 170000 : 0;
      c.snapshot_.matchElapsedMs = 10000; c.snapshot_.winnerSlot = t < 2.4 ? 0 : 2;
      c.snapshot_.endReason = t < 2.4 ? plink::MatchEndReason::None : plink::MatchEndReason::Destroyed;
      const auto clock = base + std::chrono::milliseconds(frame * 40);
      c.fxTestNow_ = PetClient::FxSeconds(clock); c.AdvanceGameEffects(clock);
      char name[80]; std::snprintf(name,sizeof(name),"side-%u-%03u.bmp",side,frame);
      if (!SaveFxFrame(c,folder/name)) return 4;
      ++files;
      if (side == 0 && frame == 50) warmedHandles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
      if (side == 0 && frame == 99 && GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) > warmedHandles + 2) return 16;
      if (frame == 60 && (!c.GameFinishedVisible() || !c.gameEffects_.Finishing(c.fxTestNow_))) return 5;
      if (frame == 62 && !c.gameEffects_.HidePlane(0,c.fxTestNow_ + .02)) return 6;
      if (frame == 88 && c.gameEffects_.Finishing(c.fxTestNow_)) return 7;
    }
  }
  const double renderMs = std::chrono::duration<double, std::milli>(Clock::now() - renderStart).count() / files;
  // Effects at the extreme map edges must leave the separate 52px HUD
  // byte-identical in either player's rotated view.
  for (unsigned side=0; side<2; ++side) {
    PetClient edge; edge.slot_=uint8_t(side+1); edge.haveSnapshot_=true;
    edge.telemetryEnabled_=false; edge.snapshot_.phase=plink::GamePhase::Playing;
    edge.snapshot_.players[0]={120,304,1,1}; edge.snapshot_.players[1]={119,15,1,1};
    std::vector<unsigned char> clean, effects;
    const auto base=Clock::now();
    edge.fxTestNow_=PetClient::FxSeconds(base)+.8;
    if (!SaveFxFrame(edge,folder/("edge-clean-"+std::to_string(side)+".bmp"),&clean)) return 20;
    for (unsigned step=0; step<=48; ++step)
      edge.AdvanceGameEffects(base+std::chrono::microseconds(step*1000000/60));
    if (!SaveFxFrame(edge,folder/("edge-effects-"+std::to_string(side)+".bmp"),&effects)) return 21;
    if (!std::equal(clean.begin(),clean.begin()+240*52*4,effects.begin())) return 22;
    if (clean==effects) return 23;
  }
  // Replay/confirmation does not duplicate an impact, or show it before its
  // swept-collision fraction. Exercise the exact client event -> VFX bridge.
  PetClient c; c.slot_=1; c.telemetryEnabled_=false; c.haveSnapshot_=c.gameMode_=true;
  c.currentRoundId_=99; c.battleSupported_=c.motionSupported_=true;
  c.snapshot_.phase=plink::GamePhase::Playing;
  pcbattle::World world; world.tick=61;
  world.impacts[0]={61,2,32767,0,2,{120*256,240*256},{120*256,248*256}}; world.impactCount=1;
  world.players[0]={120*256,248*256}; world.health[0]=2;
  c.battlePredictor_.Reset(world,0);
  c.battlePredictor_.previous.tick=60;
  c.battlePredictor_.previous.health[0]=3;
  const auto now=Clock::now();
  c.motionRemainder_=200000; c.AdvanceGameEffects(now);
  if (!c.gameEffects_.hits_.empty()) return 8;
  if(c.combatFeedback_.HitAge(1,PetClient::FxSeconds(now))>=0) return 24;
  if(c.combatFeedback_.WarningAlpha(0,PetClient::FxSeconds(now),true)>0) return 26;
  c.motionRemainder_=800000; c.AdvanceGameEffects(now+std::chrono::milliseconds(10));
  if (c.gameEffects_.hits_.size()!=1 || c.gameEffects_.hits_[0].offset.y!=-8) return 9;
  if(c.combatFeedback_.HitAge(1,PetClient::FxSeconds(now+std::chrono::milliseconds(10)))<0 ||
     c.combatFeedback_.HitAge(0,PetClient::FxSeconds(now+std::chrono::milliseconds(10)))>=0) return 25;
  c.AdvanceGameEffects(now+std::chrono::milliseconds(20));
  if (c.gameEffects_.hits_.size()!=1) return 10;
  if(c.combatFeedback_.WarningAlpha(0,PetClient::FxSeconds(now+std::chrono::milliseconds(20)),true)<=0 ||
     c.combatFeedback_.WarningAlpha(1,PetClient::FxSeconds(now+std::chrono::milliseconds(20)),true)>0) return 27;
  // Final result is immediately visible while animation uses wall-clock time.
  c.snapshot_.phase=plink::GamePhase::Finished; c.snapshot_.endReason=plink::MatchEndReason::Destroyed;
  c.snapshot_.players[0]={120,248,0,0}; c.snapshot_.players[1]={119,55,2,1}; c.snapshot_.winnerSlot=2;
  c.AdvanceGameEffects(now+std::chrono::milliseconds(30));
  const auto explosionStart=c.gameEffects_.explosions_[0].born;
  c.ReturnToPetFromFinished();
  if (!c.fxReturnPending_ || !c.gameMode_ || c.returnToPetRequested_) return 11;
  c.snapshot_.phase=plink::GamePhase::Menu;
  c.snapshot_.players[0].health=c.snapshot_.players[1].health=3; c.snapshot_.winnerSlot=0;
  c.ApplyServerMenuReturn(now+std::chrono::milliseconds(40));
  if (!c.fxDeferredMenu_ || !c.GameFinishedVisible() || c.GamePresentation().players[0].health!=0 || c.GamePresentation().winnerSlot!=2) return 12;
  c.AdvanceGameEffects(now+std::chrono::milliseconds(300));
  if (!c.gameMode_ || c.gameEffects_.explosions_[0].born != explosionStart) return 13;
  c.AdvanceGameEffects(now+std::chrono::milliseconds(1200));
  if (c.fxDeferredMenu_ || c.fxReturnPending_ || c.gameMode_) return 14;
  c.snapshot_.phase=plink::GamePhase::Countdown; c.currentRoundId_=100;
  c.AdvanceGameEffects(now+std::chrono::milliseconds(1300));
  if (c.gameEffects_.finished_ || c.gameEffects_.HidePlane(0,explosionStart+3)) return 15;
  // A local return without any server response still runs once after the
  // animation. Non-destruction endings never delay return or explode a plane.
  PetClient local; local.slot_=1; local.gameMode_=local.haveSnapshot_=true;
  local.telemetryEnabled_=false; local.snapshot_.phase=plink::GamePhase::Finished;
  local.snapshot_.endReason=plink::MatchEndReason::Destroyed;
  local.snapshot_.players[0]={120,248,0,0}; local.snapshot_.players[1]={119,55,2,1};
  local.snapshot_.winnerSlot=2;
  local.AdvanceGameEffects(Clock::now()-std::chrono::milliseconds(1200));
  local.fxReturnPending_=true; local.AdvanceGameEffects(Clock::now());
  if (local.gameMode_ || local.fxReturnPending_ || !local.returnToPetRequested_) return 17;
  for (const auto reason : {plink::MatchEndReason::TimeLimitDraw,
      plink::MatchEndReason::PlayerDisconnected, plink::MatchEndReason::ServerUnavailable}) {
    PetClient aborted; aborted.slot_=1; aborted.haveSnapshot_=aborted.gameMode_=true;
    aborted.telemetryEnabled_=false; aborted.snapshot_.phase=plink::GamePhase::Finished;
    aborted.snapshot_.endReason=reason;
    aborted.AdvanceGameEffects(Clock::now());
    if (aborted.gameEffects_.Finishing(PetClient::FxSeconds(Clock::now()))) return 18;
    aborted.ReturnToPetFromFinished();
    if (aborted.gameMode_ || aborted.fxReturnPending_) return 19;
  }
  std::printf("REAL_RENDER_VFX_OK files=%u assets=30 alpha=1 hud_pixel_equal_both_views=1 future_hit_gate=1 dedup=1 immediate_result=1 deferred_local_and_peer_return=1 round_reset=1 non_destruction=1 stable_gdi=1 render_and_save_ms=%.3f\n",files+4,renderMs);
}
