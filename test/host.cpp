// Minimal fake AE/Premiere host to exercise ElementGenie.aex under Wine
#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include <windows.h>
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
  HMODULE h = LoadLibraryA("ElementGenie.aex"); if(!h){ printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
  Main m = (Main)GetProcAddress(h, "EffectMain");
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
    if(d.param_type==PF_Param_FLOAT_SLIDER) d.u.fs_d.value=val; else if(d.param_type==PF_Param_ANGLE) d.u.ad.value=FLOAT2FIX(val); else if(d.param_type==PF_Param_CHECKBOX) d.u.bd.value=(PF_Boolean)val; }
  std::vector<unsigned char> inpx(W*H*4), outpx(W*H*4);
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){ unsigned char* p=&inpx[(y*W+x)*4]; p[0]=255; p[1]=(unsigned char)(40+x*80/W); p[2]=(unsigned char)(60+y*100/H); p[3]=((x/80+y/80)&1)?120:90; }
  PF_LayerDef inw; memset(&inw,0,sizeof inw); inw.data=(PF_PixelPtr)inpx.data(); inw.width=W; inw.height=H; inw.rowbytes=W*4;
  PF_LayerDef outw=inw; outw.data=(PF_PixelPtr)outpx.data();
  cur[0].u.ld=inw;
  std::vector<PF_ParamDef*> pp; for(auto& d:cur) pp.push_back(&d);
  DWORD t0=GetTickCount();
  e = m(PF_Cmd_RENDER,&in,&od,pp.data(),&outw,nullptr); printf("render %d %lums msg='%s'\n", e, GetTickCount()-t0, od.return_msg);
  t0=GetTickCount(); e = m(PF_Cmd_RENDER,&in,&od,pp.data(),&outw,nullptr); printf("render2 %d %lums\n", e, GetTickCount()-t0);
  if (getenv("EG_SETUP")) {
    PF_UserChangedParamExtra ex; ex.param_index=1; od.out_flags=0;
    e = m(PF_Cmd_USER_CHANGED_PARAM,&in,&od,pp.data(),nullptr,&ex);
    printf("setup %d flags %x change %x\n", e, od.out_flags, cur[2].uu.change_flags);
    HB* hb=(HB*)cur[2].u.arb_d.value; printf("scene now %llu bytes: %.120s\n", hb->size, (char*)hb->data+8);
    e = m(PF_Cmd_RENDER,&in,&od,pp.data(),&outw,nullptr); printf("render after setup %d\n", e);
  }
  std::vector<unsigned char> rgba(W*H*4);
  for(int i=0;i<W*H;i++){ rgba[i*4]=outpx[i*4+1]; rgba[i*4+1]=outpx[i*4+2]; rgba[i*4+2]=outpx[i*4+3]; rgba[i*4+3]=outpx[i*4]; }
  stbi_write_png(out,W,H,4,rgba.data(),W*4);
  printf("wrote %s\n", out);
  return 0;
}
