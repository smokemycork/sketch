#include "../Source/Engine.h"
#include <cstdio>
#include <random>
#include <chrono>
using namespace sd;

static float lvl[2][64]; static int pit[2][64];
static Shapes shapes() { Shapes s; s.level[0]=lvl[0]; s.level[1]=lvl[1]; s.pitch[0]=pit[0]; s.pitch[1]=pit[1]; return s; }
static void natural(){ for(int s=0;s<2;++s) for(int j=0;j<64;++j){ lvl[s][j]=0.88f*(1.f-j/63.f); pit[s][j]=0; } }
static int fails=0;
#define CHECK(c,msg) do{ if(!(c)){ printf("FAIL: %s\n",msg); ++fails; } else printf("ok:   %s\n",msg);}while(0)

int main(){
  const double sr=48000; natural();
  // 1) timing & drawn levels using a 1 kHz tone burst (digital mode, mix 1, 4 repeats @100 ms, linear shape)
  { Engine e; e.prepare(sr); EngineParams p; p.mode=2; p.mix=1; p.delaySec[0]=p.delaySec[1]=0.1; p.reps[0]=p.reps[1]=4;
    for(int j=0;j<64;++j){ lvl[0][j]=lvl[1][j]=1.f-j/63.f; }
    int N=48000; std::vector<float> L(N,0.f),R(N,0.f);
    for(int i=0;i<480;++i) L[i]=R[i]=0.5f*std::sin(2*kPi*1000*i/sr)*std::sin(kPi*i/480.0);
    auto sh=shapes();
    for(int o=0;o<N;o+=128) e.process(L.data()+o,R.data()+o,128,p,sh);
    int found=0;
    for(int k=1;k<=3;++k){ int c=(int)(k*0.1*sr)+240; float pk=0; int at=0; for(int i=c-300;i<c+300;++i) if(std::fabs(L[i])>pk){pk=std::fabs(L[i]);at=i;}
      float y=1.f-(k-1)/3.f; float g=std::pow(10.f,(y-1.f)*3.f); float exp_pk=0.5f*g;
      printf("  repeat %d: peak %.4f at %d (expected %.4f near %d)\n",k,pk,at,exp_pk,c);
      if(std::fabs(pk-exp_pk)<0.03f*exp_pk+0.001f && std::abs(at-c)<=40) ++found; }
    float tail=0; for(int i=(int)(0.4*sr)+600;i<N;++i) tail=std::max(tail,std::fabs(L[i]));
    printf("  after the last repeat: %.6f\n",tail);
    CHECK(found==3,"repeats land on time with the drawn levels"); CHECK(tail<1e-4f,"last repeat (drawn to silence) is silent");
  }
  // 1b) a sketch with exactly N bars maps 1:1 onto N repeats (1/8 mode = 8 bars): every bar's own level
  { Engine e; e.prepare(sr); EngineParams p; p.mode=2; p.mix=1; p.delaySec[0]=p.delaySec[1]=0.125; p.reps[0]=p.reps[1]=8;
    float want[8]={1.0f,0.5f,0.8f,0.2f,0.9f,0.1f,0.6f,0.3f};
    for(int s=0;s<2;++s) for(int j=0;j<64;++j){ lvl[s][j]=(j<8)?want[j]:0.f; pit[s][j]=0; }
    int N=(int)(sr*1.6); std::vector<float> L(N,0.f),R(N,0.f);
    for(int i=0;i<480;++i) L[i]=R[i]=0.5f*std::sin(2*kPi*1000*i/sr)*std::sin(kPi*i/480.0);
    auto sh=shapes(); sh.len=8; for(int o=0;o<N;o+=128) e.process(L.data()+o,R.data()+o,std::min(128,N-o),p,sh);
    int ok=0; for(int k=1;k<=8;++k){ int c=(int)(k*0.125*sr)+240; float pk=0; for(int i=c-300;i<c+300;++i) pk=std::max(pk,std::fabs(L[i]));
      float g=std::pow(10.f,(want[k-1]-1.f)*3.f); if(std::fabs(pk-0.5f*g)<0.04f*0.5f*g+0.0008f) ++ok; else printf("  bar %d: got %.4f want %.4f\n",k,pk,0.5f*g); }
    CHECK(ok==8,"1/8 mode: 8 bars, each repeat plays exactly its own drawn level");
    for(int j=0;j<64;++j){ lvl[0][j]=lvl[1][j]=1.f-j/63.f; }
  }
  // 2) pitch: sine 440, +12 semitones on repeat 1 -> ~880 Hz
  { Engine e; e.prepare(sr); EngineParams p; p.mode=2; p.mix=1; p.delaySec[0]=p.delaySec[1]=0.5; p.reps[0]=p.reps[1]=1;
    for(int s=0;s<2;++s) for(int j=0;j<64;++j){ lvl[s][j]=1.f; pit[s][j]=0; } pit[0][0]=pit[1][0]=12;
    int N=(int)(sr*1.2); std::vector<float> L(N,0.f),R(N,0.f);
    for(int i=0;i<(int)(0.3*sr);++i) L[i]=R[i]=0.5f*std::sin(2*kPi*440*i/sr);
    auto sh=shapes(); for(int o=0;o<N;o+=128) e.process(L.data()+o,R.data()+o,std::min(128,N-o),p,sh);
    int a=(int)(0.62*sr), b=(int)(0.78*sr); int zc=0; for(int i=a+1;i<b;++i) if(L[i-1]<=0&&L[i]>0) ++zc;
    double f=zc/((b-a)/sr); printf("  measured %.1f Hz (expect ~880)\n",f);
    CHECK(std::fabs(f-880)<25,"+12 semitone repeat plays an octave up");
    for(int s=0;s<2;++s) pit[s][0]=0;
  }
  // 3) EQ low cut attenuates 100 Hz, passes 5 kHz (post EQ on echoes)
  { EqBandParams b; b.on=true; b.type=LowCut; b.freq=1000; b.slope=2; EqBand eb; designBand(b,sr,eb);
    double a=bandMagnitudeDb(eb,sr,100), c=bandMagnitudeDb(eb,sr,5000);
    printf("  lowcut 1k/24dB: 100Hz %.1f dB, 5k %.1f dB\n",a,c);
    CHECK(a<-35 && std::fabs(c)<0.5,"24 dB/oct low cut response");
    EqBandParams t; t.on=true; t.type=Tilt; t.freq=1000; t.gain=6; EqBand et; designBand(t,sr,et);
    double lo=bandMagnitudeDb(et,sr,60), hi=bandMagnitudeDb(et,sr,16000);
    printf("  tilt +6: 60Hz %.1f dB, 16k %.1f dB\n",lo,hi); CHECK(lo<-1.5 && hi>1.5,"tilt shelf tilts brighter");
  }
  // 4) stress: random params + noise, check finite and bounded; also timing at max load
  { std::mt19937 rng(7); std::uniform_real_distribution<float> U(0,1); Engine e; e.prepare(sr); bool ok=true; float mx=0;
    for(int it=0;it<400 && ok;++it){ EngineParams p; p.mode=rng()%3; p.color=U(rng); p.drive=U(rng); p.width=2*U(rng); p.duck=U(rng); p.mix=U(rng);
      p.spread=rng()%3; p.imager=rng()&1; p.pingpong=rng()&1; p.stack=rng()&1; p.delaySec[0]=0.002+U(rng)*3; p.delaySec[1]=0.002+U(rng)*3;
      p.reps[0]=1+rng()%64; p.reps[1]=1+rng()%64;
      for(int b=0;b<6;++b){ p.pre[b].on=rng()&1; p.pre[b].type=rng()%8; p.pre[b].freq=20+U(rng)*15000; p.pre[b].gain=(U(rng)-.5f)*36; p.pre[b].q=0.1+U(rng)*9; p.pre[b].slope=rng()%4;
        p.post[b]=p.pre[b]; p.post[b].on=rng()&1; }
      for(int s=0;s<2;++s) for(int j=0;j<64;++j){ lvl[s][j]=U(rng); pit[s][j]=(int)(rng()%49)-24; }
      auto sh=shapes(); std::vector<float> L(512),R(512); for(auto&x:L) x=(U(rng)-.5f); for(auto&x:R) x=(U(rng)-.5f);
      bool mono=(it%7==0); e.process(L.data(),mono?nullptr:R.data(),512,p,sh);
      for(int i=0;i<512;++i){ if(!std::isfinite(L[i])||!std::isfinite(R[i])){ok=false;break;} mx=std::max(mx,std::max(std::fabs(L[i]),std::fabs(R[i]))); } }
    printf("  max |out| %.2f\n",mx); CHECK(ok,"400 random-parameter blocks stay finite"); CHECK(mx<200,"output bounded");
  }
  { Engine e; e.prepare(sr); EngineParams p; p.mode=0; p.mix=.5f; p.drive=.5f; p.delaySec[0]=p.delaySec[1]=0.1; p.reps[0]=p.reps[1]=64; p.stack=true;
    for(int s=0;s<2;++s) for(int j=0;j<64;++j){ lvl[s][j]=0.9f; pit[s][j]=(j%2)?3:-2; }
    for(int b=0;b<6;++b){ p.pre[b].on=p.post[b].on=true; p.pre[b].type=p.post[b].type=Bell; p.pre[b].freq=p.post[b].freq=300.0*(b+1); p.pre[b].gain=p.post[b].gain=3; }
    auto sh=shapes(); std::vector<float> L(512),R(512); std::mt19937 rng(3); std::uniform_real_distribution<float> U(-.3f,.3f);
    int blocks=(int)(sr*5/512); auto t0=std::chrono::steady_clock::now();
    for(int i=0;i<blocks;++i){ for(auto&x:L)x=U(rng); for(auto&x:R)x=U(rng); e.process(L.data(),R.data(),512,p,sh); }
    double secs=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
    printf("  WORST CASE (64 pitched repeats, drive, 12 EQ bands): %.2fx realtime CPU\n", secs/5.0);
    CHECK(secs/5.0<0.9,"worst case runs faster than real time on one core");
  }
  printf("\n%s (%d failures)\n",fails?"SOME TESTS FAILED":"ALL TESTS PASSED",fails); return fails;
}
