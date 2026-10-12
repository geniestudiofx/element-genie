// Minimal fake AE/Premiere host to exercise ElementGenie.aex under Wine
#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#ifdef _WIN32
#include <windows.h>
static unsigned long ticks(){ return GetTickCount(); }
#else
#include <dlfcn.h>
#include <sys/time.h>
static unsigned long ticks(){ timeval tv; gettimeofday(&tv,nullptr); return (unsigned long)(tv.tv_sec*1000+tv.tv_usec/1000); }
#endif
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <cmath>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
typedef PF_Err (*Main)(PF_Cmd, PF_InData*, PF_OutData*, PF_ParamDef**, PF_LayerDef*, void*);
struct HB { void* data; A_u_longlong size; };
static PF_Handle hnew(A_u_longlong n){ HB* h=new HB; h->data=calloc(1,n?n:1); h->size=n; return (PF_Handle)h; }
static void* hlock(PF_Handle h){ return ((HB*)h)->data; }
static void hunlock(PF_Handle){}
static void hdispose(PF_Handle h){ free(((HB*)h)->data); delete (HB*)h; }
static A_u_longlong hsize(PF_Handle h){ return ((HB*)h)->size; }
static PF_Err hresize(A_u_longlong n, PF_Handle* hp){ HB* h=(HB*)*hp; h->data=realloc(h->data,n); h->size=n; return PF_Err_NONE; }
static std::vector<PF_ParamDef> defs;
static PF_Err addParam(PF_ProgPtr, PF_ParamIndex, PF_ParamDefPtr d){ defs.push_back(*d); return PF_Err_NONE; }
static std::vector<PF_ParamDef> cur;
static int animIndex=-1; static double animDegPerFrame=0; static A_long baseTime=0, step=1;
static PF_Err checkout(PF_ProgPtr, PF_ParamIndex i, A_long t, A_long, A_u_long, PF_ParamDef* p){
  *p = cur[i];
  if (i==animIndex) p->u.ad.value = FLOAT2FIX(FIX_2_FLOAT(cur[i].u.ad.value) + animDegPerFrame*(double)(t-baseTime)/step);
  return PF_Err_NONE; }
static PF_Err checkin(PF_ProgPtr, PF_ParamDef*){ return PF_Err_NONE; }
static PF_Err platData(PF_ProgPtr, PF_PlatDataID, void* d){ *(void**)d = nullptr; return PF_Err_NONE; }
int main(int argc, char** argv){
  const char* scene = argc>1? argv[1]:nullptr;
  const char* out = argc>2? argv[2]:"out.png";
  const char* plug = getenv("EG_PLUGIN") ? getenv("EG_PLUGIN") : "ElementGenie.aex";
#ifdef _WIN32
  HMODULE h = LoadLibraryA(plug); if(!h){ printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
  Main m = (Main)GetProcAddress(h, "EffectMain");
#else
  void* h = dlopen(plug, RTLD_NOW|RTLD_LOCAL); if(!h){ printf("dlopen failed %s\n", dlerror()); return 1; }
  Main m = (Main)dlsym(h, "EffectMain"); if(!m){ printf("no EffectMain\n"); return 1; }
#endif
  static PF_UtilCallbacks utils; memset(&utils,0,sizeof utils);
  utils.host_new_handle=hnew; utils.host_lock_handle=hlock; utils.host_unlock_handle=hunlock; utils.host_dispose_handle=hdispose; utils.host_get_handle_size=hsize; utils.host_resize_handle=hresize; utils.get_platform_data=platData;
  PF_InData in; memset(&in,0,sizeof in); PF_OutData od; memset(&od,0,sizeof od);
  in.utils=&utils; in.inter.add_param=addParam; in.inter.checkout_param=checkout; in.inter.checkin_param=checkin;
  int W=1280,H=720; in.width=W; in.height=H; in.time_scale=2500; in.time_step=100; in.current_time=1000; baseTime=1000; step=100;
  in.pixel_aspect_ratio.num=1; in.pixel_aspect_ratio.den=1; in.downsample_x.num=in.downsample_x.den=1; in.downsample_y.num=in.downsample_y.den=1;
  PF_Err e = m(PF_Cmd_GLOBAL_SETUP,&in,&od,nullptr,nullptr,nullptr); printf("global %d flags %x %x\n", e, od.out_flags, od.out_flags2);
  PF_ParamDef layer; memset(&layer,0,sizeof layer); defs.push_back(layer);
  e = m(PF_Cmd_PARAMS_SETUP,&in,&od,nullptr,nullptr,nullptr); printf("params %d n=%d added=%zu\n", e, od.num_params, defs.size());
  cur = defs;
  for (auto& d: cur){ switch(d.param_type){ case PF_Param_ARBITRARY_DATA: d.u.arb_d.value=d.u.arb_d.dephault; break; default: break; } }
  for (size_t i=0;i<cur.size();i++) printf("  %zu type %d '%s'\n", i, cur[i].param_type, cur[i].PF_DEF_NAME);
  if (scene && scene[0]){ FILE* f=fopen(scene,"rb"); std::string s; char b[4096]; size_t n; while((n=fread(b,1,sizeof b,f))>0) s.append(b,n); fclose(f);
    PF_Handle hh=hnew(8+s.size()+1); char* p=(char*)hlock(hh); unsigned mg=0x45473344, ln=(unsigned)s.size(); memcpy(p,&mg,4); memcpy(p+4,&ln,4); memcpy(p+8,s.c_str(),s.size()+1); cur[2].u.arb_d.value=(PF_ArbitraryH)hh; }
  // tweak params via env-ish args: name=value
  for (int a=3;a<argc;a++){ char nm[64]; double v; if(sscanf(argv[a],"%d=%lf",(int*)nm,&v)==2){} 
    std::string arg=argv[a]; size_t eq=arg.find('='); int idx=atoi(arg.substr(0,eq).c_str()); double val=atof(arg.substr(eq+1).c_str());
    if (arg[0]=='A'){ animIndex=atoi(arg.substr(1,eq-1).c_str()); animDegPerFrame=val; continue; }
    auto& d=cur[idx];
    if(d.param_type==PF_Param_FLOAT_SLIDER) d.u.fs_d.value=val; else if(d.param_type==PF_Param_ANGLE) d.u.ad.value=FLOAT2FIX(val); else if(d.param_type==PF_Param_CHECKBOX) d.u.bd.value=(PF_Boolean)val; else if(d.param_type==PF_Param_POPUP) d.u.pd.value=(A_long)val; }
  std::vector<unsigned char> inpx(W*H*4), outpx(W*H*4);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){ unsigned char* p=&inpx[(y*W+x)*4]; p[0]=255; p[1]=(unsigned char)(40+x*80/W); p[2]=(unsigned char)(60+y*100/H); p[3]=((x/80+y/80)&1)?120:90; }
  PF_LayerDef inw; memset(&inw,0,sizeof inw); inw.data=(PF_PixelPtr)inpx.data(); inw.width=W; inw.height=H; inw.rowbytes=W*4;
  PF_LayerDef outw=inw; outw.data=(PF_PixelPtr)outpx.data();
  cur[0].u.ld=inw;
  std::vector<PF_ParamDef*> pp; for(auto& d:cur) pp.push_back(&d);
  unsigned long t0=ticks();
  e = m(PF_Cmd_RENDER,&in,&od,pp.data(),&outw,nullptr); printf("render %d %lums msg='%s'\n", e, ticks()-t0, od.return_msg);
  t0=ticks(); e = m(PF_Cmd_RENDER,&in,&od,pp.data(),&outw,nullptr); printf("render2 %d %lums\n", e, ticks()-t0);
  if (getenv("EG_SETUP")) {
    PF_UserChangedParamExtra ex; ex.param_index=1; od.out_flags=0;
    e = m(PF_Cmd_USER_CHANGED_PARAM,&in,&od,pp.data(),nullptr,&ex);
    printf("setup %d flags %x change %x\n", e, od.out_flags, cur[2].uu.change_flags);
    HB* hb=(HB*)cur[2].u.arb_d.value; printf("scene now %llu bytes: %.120s\n", hb->size, (char*)hb->data+8);
    e = m(PF_Cmd_RENDER,&in,&od,pp.data(),&outw,nullptr); printf("render after setup %d\n", e);
  }
  if (getenv("EG_SWEEP")) {
    std::vector<unsigned char> base = outpx;
    auto stats = [&](const char* tag){
      double diff=0; long changed=0; double mean=0; long zeroA=0;
      for (int i=0;i<W*H;i++){ int d=0; for(int c=0;c<4;c++){ int v=abs((int)outpx[i*4+c]-(int)base[i*4+c]); d+=v; } diff+=d; if(d>12) changed++; mean+=outpx[i*4+1]+outpx[i*4+2]+outpx[i*4+3]; }
      printf("SW %s changed=%ld meandiff=%.3f mean=%.1f\n", tag, changed, diff/(W*H), mean/(W*H*3));
      fflush(stdout);
    };
    for (size_t k=3;k<cur.size();k++){
      PF_ParamDef save=cur[k]; char tag[160];
      std::vector<double> vals; int ty=cur[k].param_type;
      if (ty==PF_Param_FLOAT_SLIDER) { vals={cur[k].u.fs_d.slider_min, cur[k].u.fs_d.slider_max}; }
      else if (ty==PF_Param_ANGLE) { vals={-90,90}; }
      else if (ty==PF_Param_POPUP) { for(int c=1;c<=cur[k].u.pd.num_choices;c++) if(c!=cur[k].u.pd.value) vals.push_back(c); }
      else if (ty==PF_Param_CHECKBOX) { vals={(double)!cur[k].u.bd.value}; }
      else continue;
      for (double v: vals){
        if(ty==PF_Param_FLOAT_SLIDER) cur[k].u.fs_d.value=v; else if(ty==PF_Param_ANGLE) cur[k].u.ad.value=FLOAT2FIX(v); else if(ty==PF_Param_POPUP) cur[k].u.pd.value=(A_long)v; else cur[k].u.bd.value=(PF_Boolean)v;
        od.return_msg[0]=0;
        unsigned long t1=ticks(); PF_Err er = m(PF_Cmd_RENDER,&in,&od,pp.data(),&outw,nullptr);
        snprintf(tag,sizeof tag,"%zu '%s' =%g err=%d %lums msg='%s'", k, cur[k].PF_DEF_NAME, v, er, ticks()-t1, od.return_msg);
        stats(tag);
        if (getenv("EG_SWEEP_PNG")) { int w4=W/4,h4=H/4; std::vector<unsigned char> rg(w4*h4*3); for(int y=0;y<h4;y++)for(int x=0;x<w4;x++){ const unsigned char* q=&outpx[((y*4)*W+x*4)*4]; unsigned char* d=&rg[(y*w4+x)*3]; d[0]=q[1]; d[1]=q[2]; d[2]=q[3]; }
          char fn[64]; snprintf(fn,sizeof fn,"sw/%03zu_%g.png",k,v); stbi_write_png(fn,w4,h4,3,rg.data(),w4*3); }
      }
      cur[k]=save;
    }
    return 0;
  }
  std::vector<unsigned char> rgba(W*H*4);
  for(int i=0;i<W*H;i++){ rgba[i*4]=outpx[i*4+1]; rgba[i*4+1]=outpx[i*4+2]; rgba[i*4+2]=outpx[i*4+3]; rgba[i*4+3]=outpx[i*4]; }
  stbi_write_png(out,W,H,4,rgba.data(),W*4);
  printf("wrote %s\n", out);
  return 0;
}
