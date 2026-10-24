#include "../Source/Engine.h"
#include <cstdio>
using namespace sd;
static float lvl[2][64]; static int pit[2][64];
int main(){ const double sr=48000; for(int s=0;s<2;++s)for(int j=0;j<64;++j){lvl[s][j]=1.f;pit[s][j]=0;}
 Shapes sh; sh.level[0]=lvl[0];sh.level[1]=lvl[1];sh.pitch[0]=pit[0];sh.pitch[1]=pit[1];
 for(double f: {100.,1000.,5000.,10000.,15000.,18000.,20000.,22000.}){
  Engine e; e.prepare(sr); EngineParams p; p.mode=2;p.mix=1;p.delaySec[0]=p.delaySec[1]=0.2;p.reps[0]=p.reps[1]=1;
  int N=(int)(sr*0.8); std::vector<float> L(N),R(N); for(int i=0;i<N;++i) L[i]=R[i]=(i<(int)(0.4*sr))?0.5f*std::sin(2*kPi*f*i/sr):0.f;
  for(int o=0;o<N;o+=128) e.process(L.data()+o,R.data()+o,std::min(128,N-o),p,sh);
  float pk=0; for(int i=(int)(0.3*sr)+(int)(0.2*sr);i<(int)(0.55*sr)+(int)(0.2*sr)-4800;++i) pk=std::max(pk,std::fabs(L[i]));
  printf("%6.0f Hz: echo gain %.2f dB\n",f,20*std::log10(pk/0.5));
 } }
