// Element Genie - OpenGL renderer: PBR materials, studio reflections, shadows, shadow catcher, glow, motion blur
#include "render.h"
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <cstdio>
#include <map>
#include <algorithm>
#include <sstream>

using glm::vec2; using glm::vec3; using glm::vec4; using glm::mat3; using glm::mat4;

// ============================================================ GPU resources
Texture::~Texture() { if (id) glDeleteTextures(1, &id); }

std::shared_ptr<Texture> uploadTexture(const ImageData& img, bool srgb, bool mips, bool clampEdge) {
    auto t = std::make_shared<Texture>();
    glGenTextures(1, &t->id);
    glBindTexture(GL_TEXTURE_2D, t->id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (!img.rgbf.empty()) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, img.w, img.h, 0, GL_RGB, GL_FLOAT, img.rgbf.data());
    else glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    if (mips) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLint wrap = clampEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    if (GLAD_GL_EXT_texture_filter_anisotropic || GLAD_GL_ARB_texture_filter_anisotropic) glTexParameterf(GL_TEXTURE_2D, 0x84FE, 16.f);
    t->w = img.w; t->h = img.h;
    return t;
}

Geometry::~Geometry() {
    for (auto& p : parts) { glDeleteVertexArrays(1, &p.vao); glDeleteBuffers(1, &p.vbo); glDeleteBuffers(1, &p.ibo); }
}

std::shared_ptr<Geometry> uploadGeometry(BuildResult& r) {
    auto g = std::make_shared<Geometry>();
    computeBounds(r.meshes, g->bmin, g->bmax);
    g->glyphCenters = r.glyphCenters;
    std::map<const ImageData*, std::shared_ptr<Texture>> texCache;
    auto tex = [&](const std::shared_ptr<ImageData>& im, bool srgb) -> std::shared_ptr<Texture> {
        if (!im) return nullptr;
        auto it = texCache.find(im.get());
        if (it != texCache.end()) return it->second;
        auto t = uploadTexture(*im, srgb, true, false);
        texCache[im.get()] = t;
        return t;
    };
    for (auto& m : r.meshes) {
        if (m.v.empty() || m.i.empty()) continue;
        Part p;
        glGenVertexArrays(1, &p.vao); glBindVertexArray(p.vao);
        glGenBuffers(1, &p.vbo); glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
        glBufferData(GL_ARRAY_BUFFER, m.v.size() * sizeof(Vertex), m.v.data(), GL_STATIC_DRAW);
        glGenBuffers(1, &p.ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m.i.size() * 4, m.i.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)12);
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)24);
        glBindVertexArray(0);
        p.count = (int)m.i.size(); p.slot = m.slot; p.glyph = m.glyph; p.mat = m.mat;
        p.mat.baseTex = tex(m.mat.baseImg, true);
        p.mat.mrTex = tex(m.mat.mrImg, false);
        p.mat.normalTex = tex(m.mat.normalImg, false);
        p.mat.emisTex = tex(m.mat.emisImg, true);
        p.mat.baseImg = p.mat.mrImg = p.mat.normalImg = p.mat.emisImg = nullptr;  // free CPU copies
        g->parts.push_back(p);
    }
    return g;
}

// ============================================================ shaders
static const char* VS_MESH = R"(#version 330 core
layout(location=0) in vec3 aP; layout(location=1) in vec3 aN; layout(location=2) in vec2 aUV;
uniform mat4 uM, uVP; uniform mat3 uNM;
uniform float uTwist; uniform int uTwistAxis; uniform vec3 uTwistC; uniform vec2 uTwistRange;
out vec3 vP; out vec3 vN; out vec2 vUV;
vec3 twistV(vec3 v, float a){
  float c = cos(a), s = sin(a);
  if (uTwistAxis == 0) return vec3(v.x, c*v.y - s*v.z, s*v.y + c*v.z);
  if (uTwistAxis == 1) return vec3(c*v.x + s*v.z, v.y, -s*v.x + c*v.z);
  return vec3(c*v.x - s*v.y, s*v.x + c*v.y, v.z);
}
void main(){
  vec3 p = aP, n = aN;
  if (uTwist != 0.0) {
    float along = uTwistAxis == 0 ? p.x : uTwistAxis == 1 ? p.y : p.z;
    float a = uTwist * ((along - uTwistRange.x) / max(uTwistRange.y, 1e-5) - 0.5);
    p = twistV(p - uTwistC, a) + uTwistC;
    n = twistV(n, a);
  }
  vec4 w = uM*vec4(p,1.0); vP = w.xyz; vN = uNM*n; vUV = aUV; gl_Position = uVP*w;
}
)";

static const char* FS_PBR = R"(#version 330 core
in vec3 vP; in vec3 vN; in vec2 vUV;
out vec4 oCol;
uniform vec3 uCam;
uniform vec4 uBase; uniform float uMetal, uRough, uCoat; uniform vec3 uEmis;
uniform sampler2D tBase, tMR, tNormal, tEmis;
uniform int uHasNormal, uHasMR;
uniform samplerCube tEnv; uniform float uEnvMax, uEnvInt; uniform mat3 uEnvRot;
uniform int uNL; uniform vec4 uLP[4]; uniform vec3 uLC[4];
uniform sampler2D tShadow; uniform mat4 uShadowVP; uniform int uShadowL; uniform float uShadowSoft;
uniform float uOpacity; uniform int uAlphaMode; uniform float uCutoff;
uniform sampler2D tAO; uniform int uHasAO; uniform float uAOStrength; uniform vec2 uScreen;
uniform int uReflect; uniform float uGroundY, uReflFade, uReflStrength;
const float PI = 3.14159265;
vec3 envL(vec3 d, float lod){ return textureLod(tEnv, uEnvRot*d, lod).rgb*uEnvInt; }
mat3 cotangent(vec3 N, vec3 p, vec2 uv){
  vec3 dp1 = dFdx(p), dp2 = dFdy(p); vec2 du1 = dFdx(uv), du2 = dFdy(uv);
  vec3 a = cross(dp2, N), b = cross(N, dp1);
  vec3 T = a*du1.x + b*du2.x, B = a*du1.y + b*du2.y;
  float m = max(dot(T,T), dot(B,B)); if (m < 1e-20) return mat3(1.0);
  float inv = inversesqrt(m); return mat3(T*inv, B*inv, N);
}
vec3 envBRDF(vec3 F0, float r, float NV){
  const vec4 c0 = vec4(-1.0,-0.0275,-0.572,0.022); const vec4 c1 = vec4(1.0,0.0425,1.04,-0.04);
  vec4 rr = r*c0 + c1; float a004 = min(rr.x*rr.x, exp2(-9.28*NV))*rr.x + rr.y;
  vec2 AB = vec2(-1.04,1.04)*a004 + rr.zw; return F0*AB.x + AB.y;
}
const vec2 PD[16] = vec2[](vec2(-0.94201624,-0.39906216),vec2(0.94558609,-0.76890725),vec2(-0.094184101,-0.92938870),vec2(0.34495938,0.29387760),
 vec2(-0.91588581,0.45771432),vec2(-0.81544232,-0.87912464),vec2(-0.38277543,0.27676845),vec2(0.97484398,0.75648379),
 vec2(0.44323325,-0.97511554),vec2(0.53742981,-0.47373420),vec2(-0.26496911,-0.41893023),vec2(0.79197514,0.19090188),
 vec2(-0.24188840,0.99706507),vec2(-0.81409955,0.91437590),vec2(0.19984126,0.78641367),vec2(0.14383161,-0.14100790));
uniform float uLightSize, uDepthRange, uTexPerWorld;
float hashN(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233)))*43758.5453); }
// Soft shadows that are sharp where objects touch and spread out further away (PCSS)
float shadowLit(vec3 sc, float bias){
  if (sc.x<0.0||sc.x>1.0||sc.y<0.0||sc.y>1.0||sc.z>1.0) return 1.0;
  vec2 ts = 1.0/vec2(textureSize(tShadow,0));
  float a = hashN(gl_FragCoord.xy)*6.2831853; mat2 rot = mat2(cos(a), sin(a), -sin(a), cos(a));
  float tanA = 0.004 + uLightSize*0.16;
  float searchT = clamp(1.0 + (0.25*uDepthRange)*tanA*uTexPerWorld, 2.0, 80.0);
  float sum = 0.0, cnt = 0.0;
  for (int i=0;i<16;i++){ float d = texture(tShadow, sc.xy + rot*PD[i]*ts*searchT).r; if (d < sc.z - bias){ sum += d; cnt += 1.0; } }
  if (cnt < 0.5) return 1.0;
  float blocker = sum/cnt;
  float pen = (sc.z - blocker)*uDepthRange*tanA*uTexPerWorld;
  float r = clamp(pen, 1.0, 90.0);
  float lit = 0.0;
  for (int i=0;i<16;i++){
    vec2 o = rot*PD[i]*ts*r;
    lit += (sc.z - bias > texture(tShadow, sc.xy + o).r) ? 0.0 : 1.0;
    lit += (sc.z - bias > texture(tShadow, sc.xy - o*0.5).r) ? 0.0 : 1.0;
  }
  return lit/32.0;
}
float shadowF(vec3 p, vec3 n, vec3 L){
  vec4 s = uShadowVP*vec4(p + n*0.004, 1.0); s.xyz = s.xyz/s.w*0.5 + 0.5;
  float bias = 0.0006 + 0.002*(1.0 - max(dot(n, L), 0.0));
  return shadowLit(s.xyz, bias);
}
void main(){
  vec4 base = uBase*texture(tBase, vUV);
  if (uAlphaMode==1 && base.a < uCutoff) discard;
  float a = (uAlphaMode==2 ? base.a : 1.0)*uOpacity;
  if (uReflect==1){
    if (vP.y > uGroundY + 0.002) discard;
    a *= uReflStrength*(1.0 - smoothstep(0.0, max(uReflFade, 0.01), uGroundY - vP.y));
  }
  vec3 N = normalize(vN); if (!gl_FrontFacing) N = -N;
  if (uHasNormal==1){ vec3 tn = texture(tNormal, vUV).xyz*2.0 - 1.0; N = normalize(cotangent(N, vP, vUV)*tn); }
  vec4 mr = uHasMR==1 ? texture(tMR, vUV) : vec4(1.0);
  float rough = clamp(uRough*mr.g, 0.035, 1.0), metal = clamp(uMetal*mr.b, 0.0, 1.0);
  vec3 V = normalize(uCam - vP); float NV = max(dot(N,V), 1e-4);
  vec3 F0 = mix(vec3(0.04), base.rgb, metal); vec3 diff = base.rgb*(1.0-metal);
  vec3 col = vec3(0.0);
  float a2 = rough*rough*rough*rough;
  for (int i=0;i<4;i++){
    if (i>=uNL) break;
    vec3 L; float att = 1.0;
    if (uLP[i].w < 0.5) L = normalize(uLP[i].xyz);
    else { vec3 d = uLP[i].xyz - vP; float dd = dot(d,d); L = d*inversesqrt(dd); att = 25.0/(dd + 1.0); }
    float NL = max(dot(N,L), 0.0); if (NL <= 0.0) continue;
    vec3 H = normalize(V+L); float NH = max(dot(N,H),0.0), VH = max(dot(V,H),0.0);
    float D = a2/(PI*pow(NH*NH*(a2-1.0)+1.0, 2.0));
    float k = (rough+1.0)*(rough+1.0)/8.0;
    float G = (NL/(NL*(1.0-k)+k))*(NV/(NV*(1.0-k)+k));
    vec3 F = F0 + (1.0-F0)*pow(1.0-VH, 5.0);
    vec3 spec = D*G*F/(4.0*NL*NV + 1e-4);
    float sh = (i==uShadowL) ? shadowF(vP, N, L) : 1.0;
    col += (diff*(1.0-F)/PI + spec)*uLC[i]*NL*att*sh;
  }
  vec3 R = reflect(-V, N);
  float ao = 1.0;
  if (uHasAO==1 && uReflect==0) ao = pow(clamp(texture(tAO, gl_FragCoord.xy/uScreen).r, 0.0, 1.0), uAOStrength);
  col += (diff*envL(N, uEnvMax) + envL(R, rough*uEnvMax)*envBRDF(F0, rough, NV)*mix(1.0, ao, 0.6))*ao;
  if (uCoat > 0.0){ float Fc = (0.04 + 0.96*pow(1.0-NV,5.0))*uCoat; col = col*(1.0-Fc) + envL(R, 0.04*uEnvMax)*Fc; }
  col += uEmis*texture(tEmis, vUV).rgb;
  oCol = vec4(col*a, a);
}
)";

static const char* FS_SHADOW = R"(#version 330 core
void main(){}
)";

static const char* VS_FULL = R"(#version 330 core
out vec2 vUV;
void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2); vUV = p; gl_Position = vec4(p*2.0-1.0, 0.0, 1.0); }
)";

static const char* FACE_DIR = R"(
uniform int uFace; uniform vec2 uSize;
vec3 faceDir(){
  vec2 uv = gl_FragCoord.xy/uSize*2.0 - 1.0; float u = uv.x, v = uv.y;
  vec3 d;
  if (uFace==0) d = vec3(1.0,-v,-u); else if (uFace==1) d = vec3(-1.0,-v,u);
  else if (uFace==2) d = vec3(u,1.0,v); else if (uFace==3) d = vec3(u,-1.0,-v);
  else if (uFace==4) d = vec3(u,-v,1.0); else d = vec3(-u,-v,-1.0);
  return normalize(d);
}
)";

static const char* FS_ENV = R"(
out vec4 oCol; uniform int uPreset;
float box(vec3 d, vec3 c, vec2 size, float soft){
  c = normalize(c); float k = dot(d,c); if (k <= 0.0) return 0.0;
  vec3 up = abs(c.y) > 0.99 ? vec3(1,0,0) : vec3(0,1,0);
  vec3 t = normalize(cross(up,c)), b = cross(c,t);
  vec3 p = d/k - c; vec2 q = vec2(dot(p,t), dot(p,b));
  vec2 e = abs(q) - size; float dd = length(max(e,0.0)) + min(max(e.x,e.y),0.0);
  return 1.0 - smoothstep(-soft, soft, dd);
}
vec3 env(vec3 d){
  vec3 c;
  float y = d.y;
  if (uPreset==0){ // Studio softboxes
    c = mix(vec3(0.03), vec3(0.12,0.125,0.13), smoothstep(-0.25,0.9,y));
    c += vec3(11.0)*box(d, vec3(0,1,0.25), vec2(0.7,0.45), 0.06);
    c += vec3(3.2)*box(d, vec3(0.15,0.3,1), vec2(1.1,0.28), 0.25);
    c += vec3(1.4)*box(d, vec3(-0.3,-0.15,1), vec2(1.4,0.12), 0.2);
    c += vec3(6.0)*box(d, vec3(-1,0.25,0.55), vec2(0.13,0.75), 0.03);
    c += vec3(5.0)*box(d, vec3(1,0.2,0.35), vec2(0.13,0.75), 0.03);
    c += vec3(2.2)*box(d, vec3(0.2,0.35,-1), vec2(0.6,0.3), 0.1);
    c += vec3(0.03)*smoothstep(0.0,-0.4,y);
  } else if (uPreset==1){ // Golden hour
    vec3 hz = vec3(1.0,0.55,0.25)*1.3, zen = vec3(0.18,0.32,0.65)*0.7, gr = vec3(0.07,0.045,0.03);
    c = y > 0.0 ? mix(hz, zen, pow(y, 0.55)) : mix(hz*0.35, gr, smoothstep(0.0,0.25,-y));
    vec3 sd = normalize(vec3(-0.6,0.12,-0.8)); float s = max(dot(d,sd),0.0);
    c += vec3(1.0,0.7,0.4)*(pow(s,900.0)*90.0 + pow(s,10.0)*0.9);
  } else if (uPreset==2){ // Neon night
    c = vec3(0.004,0.003,0.008) + vec3(0.03,0.01,0.05)*smoothstep(-0.3,1.0,y);
    c += vec3(1.0,0.08,0.55)*9.0*box(d, vec3(-1,0.15,0.3), vec2(0.05,0.9), 0.02);
    c += vec3(0.08,0.75,1.0)*9.0*box(d, vec3(1,0.1,0.5), vec2(0.05,0.9), 0.02);
    c += vec3(0.5,0.2,1.0)*4.0*box(d, vec3(0,1,0), vec2(0.9,0.06), 0.03);
    c += vec3(1.0,0.4,0.9)*2.0*box(d, vec3(0.3,0.2,-1), vec2(0.7,0.04), 0.02);
  } else if (uPreset==3){ // Chrome bands - high contrast for metal text
    c = vec3(0.004);
    float e = asin(clamp(y,-1.0,1.0));
    c += vec3(7.0)*(smoothstep(0.08,0.1,e)-smoothstep(0.2,0.22,e));
    c += vec3(3.5)*(smoothstep(0.38,0.4,e)-smoothstep(0.46,0.48,e));
    c += vec3(10.0)*smoothstep(1.0,1.15,e);
    c += vec3(1.5)*(smoothstep(-0.12,-0.1,e)-smoothstep(-0.04,-0.02,e));
    c += vec3(5.0)*box(d, vec3(-1,0.3,0.3), vec2(0.1,0.5), 0.03);
    c += vec3(2.5)*box(d, vec3(0.2,0.12,1), vec2(1.2,0.05), 0.03);
  } else if (uPreset==4){ // Soft daylight
    c = y > 0.0 ? mix(vec3(0.75,0.8,0.85), vec3(1.15,1.2,1.3), y) : mix(vec3(0.5), vec3(0.22,0.21,0.2), smoothstep(0.0,0.3,-y));
    c += vec3(1.6,1.5,1.3)*box(d, vec3(0.4,1,0.3), vec2(0.5,0.5), 0.4);
  } else if (uPreset==6){ // Classic chrome - sky, bright horizon, dark ground (the classic 3D title look)
    if (y > 0.0){
      c = mix(vec3(1.6,1.65,1.75), vec3(0.25,0.42,0.85), pow(y, 0.45));
      c += vec3(2.2)*exp(-y*28.0);
      c += vec3(6.0)*box(d, vec3(0.3,1,0.2), vec2(0.35,0.35), 0.25);
    } else {
      c = mix(vec3(0.35,0.3,0.26), vec3(0.03,0.025,0.02), smoothstep(0.0, 0.35, -y));
      c += vec3(0.6)*exp(y*40.0);
    }
  } else { // Gold studio - warm boxes
    c = mix(vec3(0.025,0.016,0.008), vec3(0.12,0.08,0.04), smoothstep(-0.25,0.9,y));
    c += vec3(1.0,0.78,0.45)*10.0*box(d, vec3(0,1,0.3), vec2(0.6,0.4), 0.06);
    c += vec3(1.0,0.85,0.6)*3.0*box(d, vec3(0.15,0.3,1), vec2(1.1,0.28), 0.25);
    c += vec3(1.0,0.65,0.3)*6.0*box(d, vec3(-1,0.2,0.5), vec2(0.13,0.75), 0.03);
    c += vec3(1.0,0.9,0.7)*4.0*box(d, vec3(1,0.3,0.3), vec2(0.13,0.75), 0.03);
  }
  return c;
}
void main(){ oCol = vec4(env(faceDir()), 1.0); }
)";

static const char* FS_EQUI = R"(
out vec4 oCol; uniform sampler2D tSrc;
void main(){ vec3 d = faceDir();
  vec2 uv = vec2(atan(d.z, d.x)/(2.0*3.14159265) + 0.5, acos(clamp(d.y,-1.0,1.0))/3.14159265);
  oCol = vec4(textureLod(tSrc, uv, 0.0).rgb, 1.0); }
)";

static const char* FS_PREFILTER = R"(
out vec4 oCol; uniform samplerCube tSrc; uniform float uRough; uniform float uSrcRes;
const float PI = 3.14159265;
vec2 ham(uint i, uint n){ uint b = i; b = (b<<16u)|(b>>16u); b = ((b&0x55555555u)<<1u)|((b&0xAAAAAAAAu)>>1u);
  b = ((b&0x33333333u)<<2u)|((b&0xCCCCCCCCu)>>2u); b = ((b&0x0F0F0F0Fu)<<4u)|((b&0xF0F0F0F0u)>>4u);
  b = ((b&0x00FF00FFu)<<8u)|((b&0xFF00FF00u)>>8u); return vec2(float(i)/float(n), float(b)*2.3283064365386963e-10); }
void main(){
  vec3 N = faceDir();
  if (uRough < 0.01){ oCol = vec4(textureLod(tSrc, N, 0.0).rgb, 1.0); return; }
  vec3 up = abs(N.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0);
  vec3 T = normalize(cross(up,N)), B = cross(N,T);
  float a = uRough*uRough; vec3 acc = vec3(0.0); float w = 0.0;
  const uint S = 96u;
  for (uint i=0u;i<S;i++){
    vec2 X = ham(i,S);
    float phi = 2.0*PI*X.x; float ct = sqrt((1.0-X.y)/(1.0+(a*a-1.0)*X.y)); float st = sqrt(1.0-ct*ct);
    vec3 H = normalize(T*(st*cos(phi)) + B*(st*sin(phi)) + N*ct);
    vec3 L = normalize(2.0*dot(N,H)*H - N);
    float NL = dot(N,L);
    if (NL > 0.0){
      float NH = max(dot(N,H),0.0); float a2 = a*a; float D = a2/(PI*pow(NH*NH*(a2-1.0)+1.0,2.0));
      float pdf = D/4.0 + 1e-4; float saS = 1.0/(float(S)*pdf); float saT = 4.0*PI/(6.0*uSrcRes*uSrcRes);
      float lod = max(0.5*log2(saS/saT) + 1.0, 0.0);
      acc += textureLod(tSrc, L, lod).rgb*NL; w += NL;
    }
  }
  oCol = vec4(acc/max(w,1e-4), 1.0);
}
)";

static const char* FS_CATCHER = R"(#version 330 core
in vec3 vP; in vec3 vN; in vec2 vUV; out vec4 oCol;
uniform sampler2D tShadow; uniform mat4 uShadowVP; uniform float uShadowSoft, uOpacity; uniform vec3 uCentre; uniform float uRadius;
uniform vec3 uL;
const vec2 PD[16] = vec2[](vec2(-0.94201624,-0.39906216),vec2(0.94558609,-0.76890725),vec2(-0.094184101,-0.92938870),vec2(0.34495938,0.29387760),
 vec2(-0.91588581,0.45771432),vec2(-0.81544232,-0.87912464),vec2(-0.38277543,0.27676845),vec2(0.97484398,0.75648379),
 vec2(0.44323325,-0.97511554),vec2(0.53742981,-0.47373420),vec2(-0.26496911,-0.41893023),vec2(0.79197514,0.19090188),
 vec2(-0.24188840,0.99706507),vec2(-0.81409955,0.91437590),vec2(0.19984126,0.78641367),vec2(0.14383161,-0.14100790));
uniform float uLightSize, uDepthRange, uTexPerWorld;
float hashN(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233)))*43758.5453); }
// Soft shadows that are sharp where objects touch and spread out further away (PCSS)
float shadowLit(vec3 sc, float bias){
  if (sc.x<0.0||sc.x>1.0||sc.y<0.0||sc.y>1.0||sc.z>1.0) return 1.0;
  vec2 ts = 1.0/vec2(textureSize(tShadow,0));
  float a = hashN(gl_FragCoord.xy)*6.2831853; mat2 rot = mat2(cos(a), sin(a), -sin(a), cos(a));
  float tanA = 0.004 + uLightSize*0.16;
  float searchT = clamp(1.0 + (0.25*uDepthRange)*tanA*uTexPerWorld, 2.0, 80.0);
  float sum = 0.0, cnt = 0.0;
  for (int i=0;i<16;i++){ float d = texture(tShadow, sc.xy + rot*PD[i]*ts*searchT).r; if (d < sc.z - bias){ sum += d; cnt += 1.0; } }
  if (cnt < 0.5) return 1.0;
  float blocker = sum/cnt;
  float pen = (sc.z - blocker)*uDepthRange*tanA*uTexPerWorld;
  float r = clamp(pen, 1.0, 90.0);
  float lit = 0.0;
  for (int i=0;i<16;i++){
    vec2 o = rot*PD[i]*ts*r;
    lit += (sc.z - bias > texture(tShadow, sc.xy + o).r) ? 0.0 : 1.0;
    lit += (sc.z - bias > texture(tShadow, sc.xy - o*0.5).r) ? 0.0 : 1.0;
  }
  return lit/32.0;
}
uniform sampler2D tAO; uniform int uHasAO; uniform float uAOStrength; uniform vec2 uScreen; uniform int uHasShadow;
void main(){
  float sh = 0.0;
  if (uHasShadow==1){
    vec4 s = uShadowVP*vec4(vP,1.0); s.xyz = s.xyz/s.w*0.5+0.5;
    sh = (1.0 - shadowLit(s.xyz, 0.0012))*clamp(uL.y*3.0, 0.0, 1.0);
  }
  float contact = 0.0;
  if (uHasAO==1) contact = clamp((1.0 - texture(tAO, gl_FragCoord.xy/uScreen).r)*1.3*uAOStrength, 0.0, 1.0);
  float fade = 1.0 - smoothstep(uRadius*0.55, uRadius, length(vP.xz - uCentre.xz));
  float a = 1.0 - (1.0 - sh*uOpacity)*(1.0 - contact*min(1.0, uOpacity*1.6));
  a *= fade;
  oCol = vec4(0.0, 0.0, 0.0, a);
}
)";

static const char* FS_PREPASS = R"(#version 330 core
in vec3 vP; in vec3 vN; in vec2 vUV; out vec4 oCol; uniform mat4 uV;
void main(){ vec3 N = normalize(vN); vec3 vpos = (uV*vec4(vP,1.0)).xyz; vec3 vn = normalize(mat3(uV)*N); if (dot(vn, vpos) > 0.0) vn = -vn;
  oCol = vec4(vn, vpos.z); }
)";
static const char* FS_SSAO = R"(#version 330 core
in vec2 vUV; out vec4 oCol; uniform sampler2D tND; uniform mat4 uP; uniform float uRadius; uniform vec3 uK[16];
float hashN(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233)))*43758.5453); }
void main(){
  vec4 nd = texture(tND, vUV);
  if (nd.a >= 0.0) { oCol = vec4(1.0); return; }
  vec3 N = normalize(nd.xyz); float z = nd.a;
  vec3 P = vec3((vUV.x*2.0-1.0)*(-z)/uP[0][0], (vUV.y*2.0-1.0)*(-z)/uP[1][1], z);
  float a = hashN(gl_FragCoord.xy)*6.2831853;
  vec3 rv = vec3(cos(a), sin(a), 0.0);
  vec3 T = normalize(rv - N*dot(rv, N)); vec3 B = cross(N, T);
  mat3 TBN = mat3(T, B, N);
  float occ = 0.0;
  for (int i=0;i<16;i++){
    vec3 sp = P + TBN*uK[i]*uRadius;
    vec4 c = uP*vec4(sp, 1.0); c.xy /= c.w;
    vec2 suv = c.xy*0.5 + 0.5;
    if (suv.x<0.0||suv.x>1.0||suv.y<0.0||suv.y>1.0) continue;
    float sz = texture(tND, suv).a;
    if (sz >= 0.0) continue;
    float range = smoothstep(0.0, 1.0, uRadius/abs(P.z - sz));
    occ += (sz >= sp.z + 0.015*uRadius ? 1.0 : 0.0)*range;
  }
  oCol = vec4(vec3(1.0 - occ/16.0), 1.0);
}
)";
static const char* FS_AOBLUR = R"(#version 330 core
in vec2 vUV; out vec4 oCol; uniform sampler2D tSrc; uniform sampler2D tND;
void main(){ vec2 t = 1.0/vec2(textureSize(tSrc,0)); float zc = texture(tND, vUV).a; float s = 0.0, w = 0.0;
  for (int x=-2;x<=2;x++) for (int y=-2;y<=2;y++){ vec2 o = vec2(x,y)*t; float z = texture(tND, vUV+o).a;
    float wt = 1.0/(1.0 + abs(z - zc)*20.0); s += texture(tSrc, vUV+o).r*wt; w += wt; }
  oCol = vec4(vec3(s/max(w,1e-4)), 1.0); }
)";
static const char* FS_LINES = R"(#version 330 core
out vec4 oCol; uniform vec4 uCol; void main(){ oCol = vec4(uCol.rgb*uCol.a, uCol.a); }
)";
static const char* VS_LINES = R"(#version 330 core
layout(location=0) in vec3 aP; uniform mat4 uVP; void main(){ gl_Position = uVP*vec4(aP,1.0); }
)";

static const char* FS_BRIGHT = R"(#version 330 core
in vec2 vUV; out vec4 oCol; uniform sampler2D tSrc; uniform float uThr;
void main(){ vec2 t = 1.0/vec2(textureSize(tSrc,0));
  vec3 c = (texture(tSrc, vUV+t*vec2(-0.5,-0.5)).rgb + texture(tSrc, vUV+t*vec2(0.5,-0.5)).rgb + texture(tSrc, vUV+t*vec2(-0.5,0.5)).rgb + texture(tSrc, vUV+t*vec2(0.5,0.5)).rgb)*0.25;
  float br = max(c.r, max(c.g, c.b)); float k = uThr*0.5;
  float soft = clamp(br - uThr + k, 0.0, 2.0*k); soft = soft*soft/(4.0*k + 1e-4);
  float contrib = max(soft, br - uThr)/max(br, 1e-4);
  oCol = vec4(c*contrib, 1.0); }
)";
static const char* FS_DOWN = R"(#version 330 core
in vec2 vUV; out vec4 oCol; uniform sampler2D tSrc;
void main(){ vec2 t = 1.0/vec2(textureSize(tSrc,0));
  vec3 a = texture(tSrc, vUV + t*vec2(-1,-1)).rgb, b = texture(tSrc, vUV + t*vec2(1,-1)).rgb, c = texture(tSrc, vUV + t*vec2(-1,1)).rgb, d = texture(tSrc, vUV + t*vec2(1,1)).rgb;
  vec3 e = texture(tSrc, vUV).rgb;
  oCol = vec4((a+b+c+d)*0.125 + e*0.5, 1.0); }
)";
static const char* FS_UP = R"(#version 330 core
in vec2 vUV; out vec4 oCol; uniform sampler2D tSrc;
void main(){ vec2 t = 1.0/vec2(textureSize(tSrc,0));
  vec3 s = texture(tSrc, vUV).rgb*4.0;
  s += (texture(tSrc, vUV+t*vec2(-1,0)).rgb + texture(tSrc, vUV+t*vec2(1,0)).rgb + texture(tSrc, vUV+t*vec2(0,-1)).rgb + texture(tSrc, vUV+t*vec2(0,1)).rgb)*2.0;
  s += texture(tSrc, vUV+t*vec2(-1,-1)).rgb + texture(tSrc, vUV+t*vec2(1,-1)).rgb + texture(tSrc, vUV+t*vec2(-1,1)).rgb + texture(tSrc, vUV+t*vec2(1,1)).rgb;
  oCol = vec4(s/16.0, 1.0); }
)";
// Tone-aware MSAA resolve: bright HDR samples are weighted down so edges against dark
// backgrounds stay smooth after tonemapping (a plain average leaves hard, stair-stepped edges).
static const char* FS_RESOLVE = R"(#version 330 core
out vec4 oCol; uniform sampler2DMS tMS; uniform int uS;
void main(){
  ivec2 p = ivec2(gl_FragCoord.xy);
  vec3 acc = vec3(0.0); float ws = 0.0, a = 0.0;
  for (int i = 0; i < uS; i++) {
    vec4 c = texelFetch(tMS, p, i);
    float w = 1.0 / (1.0 + max(max(c.r, c.g), c.b));
    acc += c.rgb * w; ws += w; a += c.a;
  }
  oCol = vec4(acc / max(ws, 1e-6), a / float(uS));
}
)";
static const char* FS_ACCUM = R"(#version 330 core
in vec2 vUV; out vec4 oCol; uniform sampler2D tSrc; uniform float uW;
void main(){ oCol = texture(tSrc, vUV)*uW; }
)";
static const char* FS_TONEMAP = R"(#version 330 core
in vec2 vUV; out vec4 oCol; uniform sampler2D tHdr, tBloom; uniform float uExposure, uGlow;
vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }
vec3 toSrgb(vec3 c){ return mix(c*12.92, 1.055*pow(c, vec3(1.0/2.4)) - 0.055, step(0.0031308, c)); }
void main(){
  vec4 h = texture(tHdr, vUV);
  float a = clamp(h.a, 0.0, 1.0);
  float ex = exp2(uExposure);
  vec3 c = a > 1e-5 ? aces(h.rgb/h.a*ex) : vec3(0.0);
  vec3 pc = c*a;
  if (uGlow > 0.0){
    vec3 b = aces(texture(tBloom, vUV).rgb*uGlow*ex);
    float ba = clamp(max(b.r, max(b.g, b.b)), 0.0, 1.0);
    pc += b; a = clamp(a + ba*(1.0-a), 0.0, 1.0);
  }
  vec3 st = a > 1e-5 ? clamp(pc/a, 0.0, 1.0) : vec3(0.0);
  oCol = vec4(toSrgb(st)*a, a);
}
)";
static const char* FS_BLIT = R"(#version 330 core
in vec2 vUV; out vec4 oCol; uniform sampler2D tSrc; void main(){ oCol = texture(tSrc, vUV); }
)";

static unsigned compile(const char* vs, const std::string& fs, std::string& err, const char* name) {
    auto sh = [&](GLenum type, const char* src) {
        GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
        GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[4096]; glGetShaderInfoLog(s, sizeof log, nullptr, log); err += std::string(name) + ": " + log + "\n"; }
        return s;
    };
    GLuint p = glCreateProgram();
    GLuint a = sh(GL_VERTEX_SHADER, vs), b = sh(GL_FRAGMENT_SHADER, fs.c_str());
    glAttachShader(p, a); glAttachShader(p, b); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, nullptr, log); err += std::string(name) + " link: " + log + "\n"; }
    glDeleteShader(a); glDeleteShader(b);
    return p;
}
static GLint U(unsigned p, const char* n) { return glGetUniformLocation(p, n); }

bool Renderer::init(std::string& err) {
    std::string hdr = "#version 330 core\n";
    progPBR = compile(VS_MESH, FS_PBR, err, "pbr");
    progShadow = compile(VS_MESH, FS_SHADOW, err, "shadow");
    progCatcher = compile(VS_MESH, FS_CATCHER, err, "catcher");
    progEnv = compile(VS_FULL, hdr + FACE_DIR + FS_ENV, err, "env");
    progEquirect = compile(VS_FULL, hdr + FACE_DIR + FS_EQUI, err, "equirect");
    progPrefilter = compile(VS_FULL, hdr + FACE_DIR + FS_PREFILTER, err, "prefilter");
    progLines = compile(VS_LINES, FS_LINES, err, "lines");
    progPrepass = compile(VS_MESH, FS_PREPASS, err, "prepass");
    progSSAO = compile(VS_FULL, FS_SSAO, err, "ssao");
    progAOBlur = compile(VS_FULL, FS_AOBLUR, err, "aoblur");
    progBright = compile(VS_FULL, FS_BRIGHT, err, "bright");
    progDown = compile(VS_FULL, FS_DOWN, err, "down");
    progUp = compile(VS_FULL, FS_UP, err, "up");
    progAccum = compile(VS_FULL, FS_ACCUM, err, "accum");
    progResolve = compile(VS_FULL, FS_RESOLVE, err, "resolve");
    progTonemap = compile(VS_FULL, FS_TONEMAP, err, "tonemap");
    progBlitPremul = compile(VS_FULL, FS_BLIT, err, "blit");
    glGenVertexArrays(1, &emptyVao);
    glGenVertexArrays(1, &lineVao); glGenBuffers(1, &lineVbo);
    glBindVertexArray(lineVao); glBindBuffer(GL_ARRAY_BUFFER, lineVbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, (void*)0);
    glBindVertexArray(0);
    // default textures
    ImageData w; w.w = w.h = 1; w.rgba = {255, 255, 255, 255};
    auto wt = uploadTexture(w, false, false); whiteTex = wt->id; wt->id = 0;
    ensureShadowMap(2048);
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    return err.empty();
}

void Renderer::ensureShadowMap(int res) {
    GLint maxT = 4096; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxT);
    res = std::max(512, std::min(res, (int)maxT));
    if (res == shadowResCur && shadowTex) return;
    if (shadowTex) glDeleteTextures(1, &shadowTex);
    if (!shadowFbo) glGenFramebuffers(1, &shadowFbo);
    glGenTextures(1, &shadowTex); glBindTexture(GL_TEXTURE_2D, shadowTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, res, res, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex, 0);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    shadowResCur = res; shadowRes = res;
}

void Renderer::drawCatcherQuad(unsigned prog, const vec3& c, float R, float y) {
    std::vector<vec3> q = {{c.x - R, y, c.z - R}, {c.x + R, y, c.z - R}, {c.x + R, y, c.z + R}, {c.x - R, y, c.z - R}, {c.x + R, y, c.z + R}, {c.x - R, y, c.z + R}};
    std::vector<Vertex> vv; for (auto& p : q) vv.push_back({p, {0, 1, 0}, {0, 0}});
    GLuint vao, vbo; glGenVertexArrays(1, &vao); glBindVertexArray(vao); glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vv.size() * sizeof(Vertex), vv.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)24);
    mat4 I(1); mat3 I3(1);
    glUniformMatrix4fv(U(prog, "uM"), 1, GL_FALSE, glm::value_ptr(I));
    glUniformMatrix3fv(U(prog, "uNM"), 1, GL_FALSE, glm::value_ptr(I3));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0); glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
}

void Renderer::drawFullscreen() { glBindVertexArray(emptyVao); glDrawArrays(GL_TRIANGLES, 0, 3); }

// ============================================================ environment
void Renderer::buildEnvCube(int preset, const ImageData* img) {
    const int res = 256;
    envMips = 6;
    if (!envBase) glGenTextures(1, &envBase);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envBase);
    for (int f = 0; f < 6; f++) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA16F, res, res, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (int k : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) glTexParameteri(GL_TEXTURE_CUBE_MAP, k, GL_CLAMP_TO_EDGE);
    GLuint fbo; glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    GLuint srcTex = 0;
    unsigned prog = progEnv;
    if (img) {
        prog = progEquirect;
        ImageData copy = *img;
        auto t = uploadTexture(copy, false, false, false);
        srcTex = t->id; t->id = 0;
        glBindTexture(GL_TEXTURE_2D, srcTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glUseProgram(prog);
    glUniform2f(U(prog, "uSize"), (float)res, (float)res);
    glUniform1i(U(prog, "uPreset"), preset);
    glUniform1i(U(prog, "tSrc"), 0);
    glActiveTexture(GL_TEXTURE0); if (srcTex) glBindTexture(GL_TEXTURE_2D, srcTex);
    glViewport(0, 0, res, res);
    for (int f = 0; f < 6; f++) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, envBase, 0);
        glUniform1i(U(prog, "uFace"), f);
        drawFullscreen();
    }
    if (srcTex) glDeleteTextures(1, &srcTex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envBase);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    // prefiltered (GGX) cube
    if (!envCube) glGenTextures(1, &envCube);
    glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
    for (int m = 0; m < envMips; m++)
        for (int f = 0; f < 6; f++) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, m, GL_RGBA16F, res >> m, res >> m, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, envMips - 1);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (int k : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R}) glTexParameteri(GL_TEXTURE_CUBE_MAP, k, GL_CLAMP_TO_EDGE);
    glUseProgram(progPrefilter);
    glUniform1i(U(progPrefilter, "tSrc"), 0); glUniform1f(U(progPrefilter, "uSrcRes"), (float)res);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_CUBE_MAP, envBase);
    for (int m = 0; m < envMips; m++) {
        int s = res >> m; glViewport(0, 0, s, s);
        glUniform2f(U(progPrefilter, "uSize"), (float)s, (float)s);
        glUniform1f(U(progPrefilter, "uRough"), m / float(envMips - 1));
        for (int f = 0; f < 6; f++) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, envCube, m);
            glUniform1i(U(progPrefilter, "uFace"), f);
            drawFullscreen();
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
}

void Renderer::updateEnvironment(const Environment& e) {
    std::string key = e.preset >= 0 ? "p" + std::to_string(e.preset) : "f" + e.path;
    if (key == envKey) return;
    envKey = key; envError.clear();
    if (e.preset < 0) {
        ImageData img;
        if (loadImageFile(e.path, img)) {
            // keep it manageable
            if (img.rgbf.empty() && !img.rgba.empty()) {  // LDR jpg/png: convert to linear float
                img.rgbf.resize((size_t)img.w * img.h * 3);
                for (size_t i = 0; i < (size_t)img.w * img.h; i++)
                    for (int k = 0; k < 3; k++) { float v = img.rgba[i * 4 + k] / 255.f; img.rgbf[i * 3 + k] = std::pow(v, 2.2f) * (v > 0.95f ? 4.f : 1.f); }
                img.rgba.clear();
            }
            buildEnvCube(0, &img);
            return;
        }
        envError = "Couldn't load that environment image - using Studio.";
    }
    buildEnvCube(std::max(0, e.preset), nullptr);
}

// ============================================================ geometry building
static std::string geoKeyFor(const Object& o) {
    std::ostringstream k;
    k << (int)o.type << '|' << o.path;
    switch (o.type) {
    case ObjType::Text: k << '|' << o.text << '|' << o.font << '|' << o.depth << '|' << o.bevel << '|' << o.bevelSegs << '|' << o.bevelStyle << '|' << o.bevelDepth
                          << '|' << o.letterSpacing << '|' << o.lineSpacing << '|' << o.align; break;
    case ObjType::Logo: k << '|' << o.depth << '|' << o.bevel << '|' << o.bevelSegs << '|' << o.bevelStyle << '|' << o.bevelDepth << '|' << o.alphaThreshold; break;
    case ObjType::Primitive: k << '|' << o.prim; break;
    default: break;
    }
    return k.str();
}

bool Renderer::ensureGeometry(Object& o) {
    std::string key = geoKeyFor(o);
    if (key == o.geoKey) return (bool)o.geo;
    static std::map<std::string, std::weak_ptr<Geometry>> shared;  // model files shared between copies
    o.geoKey = key; o.geoError.clear();
    if (o.type == ObjType::Model) {
        auto it = shared.find(key);
        if (it != shared.end()) if (auto g = it->second.lock()) { o.geo = g; return true; }
    }
    BuildResult r;
    switch (o.type) {
    case ObjType::Model: r = importModel(o.path); break;
    case ObjType::Text: r = buildText(o); break;
    case ObjType::Logo: {
        std::string p = o.path; for (auto& c : p) c = (char)tolower((unsigned char)c);
        bool svg = (p.size() > 4 && p.substr(p.size() - 4) == ".svg") || p.rfind("builtin:icon:", 0) == 0;
        r = svg ? buildSvgLogo(o) : buildPngLogo(o);
        break;
    }
    case ObjType::ImageCard: r = buildImageCard(o); break;
    case ObjType::Primitive: r = buildPrimitive(o); break;
    }
    o.geoError = r.error;
    if (r.meshes.empty()) { o.geo = nullptr; return false; }
    o.geo = uploadGeometry(r);
    if (o.type == ObjType::Model) shared[key] = o.geo;
    return true;
}

// ============================================================ evaluation
mat4 objectMatrix(const Object& o, float f) {
    vec3 p = o.pos.eval(f), r = glm::radians(o.rot.eval(f)), s = o.scl.eval(f);
    return o.parentM * glm::translate(mat4(1), p) * glm::eulerAngleYXZ(r.y, r.x, r.z) * glm::scale(mat4(1), s);
}

static float hash01(int i) { unsigned x = (unsigned)i * 2654435761u; x ^= x >> 13; x *= 0x5bd1e995; x ^= x >> 15; return (x & 0xffff) / 65535.f; }

static mat4 letterAnimMatrix(const Object& o, int g, int n, float f, float& opacity) {
    opacity = 1.f;
    const LetterAnim& la = o.letters;
    if (!la.enabled) return mat4(1);
    float k;
    switch (la.order) {
    case 1: k = (float)(n - 1 - g); break;
    case 2: k = std::fabs(g - (n - 1) * .5f); break;
    case 3: k = hash01(g + 17) * (n - 1); break;
    default: k = (float)g;
    }
    if (o.letterFrame >= 0) f = o.letterFrame;
    float t0 = la.start + k * la.stagger;
    float p = std::clamp((f - t0) / std::max(1, la.duration), 0.f, 1.f);
    p = bezierTiming(p, .3f, .2f);  // fast start, gentle landing
    float amt = la.mode == 0 ? 1.f - p : bezierTiming(std::clamp((f - t0) / std::max(1, la.duration), 0.f, 1.f), .6f, 1.f);
    vec3 c = o.geo->glyphCenters[g];
    vec3 r = glm::radians(la.offRot * amt);
    float sc = glm::mix(1.f, la.offScale, amt);
    if (la.fade) opacity = 1.f - amt;
    return glm::translate(mat4(1), c + la.offPos * amt) * glm::eulerAngleYXZ(r.y, r.x, r.z) * glm::scale(mat4(1), vec3(sc)) * glm::translate(mat4(1), -c);
}

// Per-letter transform from Effect Controls: each letter turns / moves / scales around its own centre.
static mat4 letterFxMatrix(const Object& o, int g, int n) {
    if (!o.lfOn) return mat4(1);
    float u = n > 1 ? (float)g / (n - 1) : 0.f;
    float w;
    switch (o.lfSpread) {
    case 1: w = u; break;                                              // ramp left -> right
    case 2: w = 1.f - u; break;                                        // ramp right -> left
    case 3: w = 1.f - std::fabs(u * 2.f - 1.f); break;                 // centre out
    case 4: w = std::sin(glm::radians(o.lfPhase) + u * 6.2831853f); break;  // wave
    case 5: w = hash01(g * 7 + 3) * 2.f - 1.f; break;                  // random
    default: w = 1.f;
    }
    vec3 rnd(hash01(g * 13 + 1) * 2.f - 1.f, hash01(g * 29 + 5) * 2.f - 1.f, hash01(g * 41 + 9) * 2.f - 1.f);
    vec3 wr = glm::mix(vec3(w), rnd, o.lfRandom);
    vec3 c = o.geo->glyphCenters[g];
    vec3 r = glm::radians(o.lfRot * wr);
    vec3 t = o.lfPos * wr;
    float sc = std::max(0.f, glm::mix(1.f, o.lfScale, glm::mix(w, rnd.x * .5f + .5f, o.lfRandom)));
    return glm::translate(mat4(1), c + t) * glm::eulerAngleYXZ(r.y, r.x, r.z) * glm::scale(mat4(1), vec3(sc)) * glm::translate(mat4(1), -c);
}

mat4 glyphMatrix(const Object& o, int g, int n, float f, float& opacity) {
    opacity = 1.f;
    if (g < 0 || !o.geo || g >= (int)o.geo->glyphCenters.size()) return mat4(1);
    return letterAnimMatrix(o, g, n, f, opacity) * letterFxMatrix(o, g, n);
}

CamEval evalCamera(const Scene& s, float f, float aspect) {
    CamEval c;
    if (s.camOv) {
        c.pos = s.camOvPos;
        c.view = glm::lookAt(c.pos, c.pos + s.camOvFwd, s.camOvUp);
        c.proj = glm::perspective(glm::radians(std::clamp(s.camOvFov, 1.f, 170.f)), aspect, 0.03f, 2000.f);
        return c;
    }
    c.pos = s.cam.pos.eval(f);
    vec3 tg = s.cam.target.eval(f);
    if (glm::length(tg - c.pos) < 1e-4f) tg = c.pos + vec3(0, 0, -1);
    vec3 fwd = glm::normalize(tg - c.pos);
    vec3 up(0, 1, 0);
    if (std::fabs(glm::dot(fwd, up)) > 0.999f) up = vec3(0, 0, -1);
    float roll = glm::radians(s.cam.roll.eval(f));
    up = glm::mat3(glm::rotate(mat4(1), roll, fwd)) * up;
    c.view = glm::lookAt(c.pos, tg, up);
    c.proj = glm::perspective(glm::radians(std::clamp(s.cam.fov.eval(f), 2.f, 150.f)), aspect, 0.03f, 800.f);
    return c;
}

bool worldBounds(const Scene& s, float f, vec3& mn, vec3& mx) {
    mn = vec3(1e30f); mx = vec3(-1e30f); bool any = false;
    for (auto& o : s.objects) {
        if (!o.visible || !o.geo) continue;
        mat4 M = objectMatrix(o, f);
        for (int i = 0; i < 8; i++) {
            vec3 p((i & 1) ? o.geo->bmax.x : o.geo->bmin.x, (i & 2) ? o.geo->bmax.y : o.geo->bmin.y, (i & 4) ? o.geo->bmax.z : o.geo->bmin.z);
            vec3 w = vec3(M * vec4(p, 1)); mn = glm::min(mn, w); mx = glm::max(mx, w); any = true;
        }
    }
    if (!any) { mn = vec3(-1); mx = vec3(1); }
    return any;
}

// ============================================================ render targets
void RenderTarget::release() {
    GLuint fb[] = {msFbo, hdrFbo, outFbo, accFbo, ndFbo, aoFbo, ao2Fbo}; glDeleteFramebuffers(7, fb);
    if (msColor) glDeleteTextures(1, &msColor);
    GLuint rb[] = {msDepth, ndDepth}; glDeleteRenderbuffers(2, rb);
    GLuint tx[] = {hdrTex, outTex, accTex, ndTex, aoTex, ao2Tex}; glDeleteTextures(6, tx);
    glDeleteFramebuffers(6, bloomFbo); glDeleteTextures(6, bloomTex);
    *this = RenderTarget();
}
static void makeTex(GLuint& t, GLenum fmt, int w, int h, GLenum type) {
    glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, GL_RGBA, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
static void fboFor(GLuint& f, GLuint tex) {
    glGenFramebuffers(1, &f); glBindFramebuffer(GL_FRAMEBUFFER, f);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
}
void RenderTarget::ensure(int W, int H, int S) {
    GLint maxS = 4; glGetIntegerv(GL_MAX_SAMPLES, &maxS);
    S = std::max(0, std::min(S, (int)maxS));
    if (W == w && H == h && S == samples && outFbo) return;
    release();
    w = W; h = H; samples = S;
    int TS = std::max(1, S);
    glGenTextures(1, &msColor); glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, msColor);
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, TS, GL_RGBA16F, w, h, GL_TRUE);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    glGenRenderbuffers(1, &msDepth); glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, TS, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &msFbo); glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, msColor, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
    makeTex(hdrTex, GL_RGBA16F, w, h, GL_FLOAT); fboFor(hdrFbo, hdrTex);
    makeTex(accTex, GL_RGBA16F, w, h, GL_FLOAT); fboFor(accFbo, accTex);
    makeTex(outTex, GL_RGBA8, w, h, GL_UNSIGNED_BYTE); fboFor(outFbo, outTex);
    makeTex(ndTex, GL_RGBA16F, w, h, GL_FLOAT); fboFor(ndFbo, ndTex);
    glGenRenderbuffers(1, &ndDepth); glBindRenderbuffer(GL_RENDERBUFFER, ndDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ndDepth);
    makeTex(aoTex, GL_RGBA8, w, h, GL_UNSIGNED_BYTE); fboFor(aoFbo, aoTex);
    makeTex(ao2Tex, GL_RGBA8, w, h, GL_UNSIGNED_BYTE); fboFor(ao2Fbo, ao2Tex);
    int bw = w, bh = h;
    for (int i = 0; i < 6; i++) {
        bw = std::max(1, bw / 2); bh = std::max(1, bh / 2);
        bloomW[i] = bw; bloomH[i] = bh;
        makeTex(bloomTex[i], GL_RGBA16F, bw, bh, GL_FLOAT); fboFor(bloomFbo[i], bloomTex[i]);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================ drawing
struct MatParams { vec4 base; float metal, rough, coat; vec3 emis; GLuint tb, tmr, tn, te; int alphaMode; float cutoff; };

void Renderer::drawObjects(Scene& s, float f, const mat4& V, const mat4& P, const vec3& camPos, bool shadowPass,
                           const mat4& shadowVP, int shadowOn, bool transparentPass) {
    unsigned prog = prepassMode ? progPrepass : shadowPass ? progShadow : progPBR;
    glUseProgram(prog);
    mat4 VP = P * V;
    if (prepassMode) glUniformMatrix4fv(U(prog, "uV"), 1, GL_FALSE, glm::value_ptr(V));
    glUniformMatrix4fv(U(prog, "uVP"), 1, GL_FALSE, glm::value_ptr(VP));
    struct Item { const Object* o; const Part* p; mat4 M; float op; float dist; };
    std::vector<Item> items;
    for (auto& o : s.objects) {
        if (!o.visible || !o.geo) continue;
        if (shadowPass && !prepassMode && !o.castShadow) continue;
        float oop = std::clamp(o.opacity.eval(f) * o.opacityMul, 0.f, 1.f);
        if (oop <= 0.001f) continue;
        mat4 M = objectMatrix(o, f);
        int ng = (int)o.geo->glyphCenters.size();
        for (auto& p : o.geo->parts) {
            float gop = 1.f;
            mat4 GM = glyphMatrix(o, p.glyph, ng, f, gop);
            float op = oop * gop;
            if (op <= 0.001f) continue;
            if (shadowPass && op < 0.3f) continue;
            bool modelMat = o.type == ObjType::Model && o.keepModelMaterials && p.mat.has;
            bool transp = op < 0.999f || (modelMat && p.mat.alphaMode == 2) || (o.type == ObjType::ImageCard);
            if (!shadowPass && transp != transparentPass) continue;
            mat4 MM = extraM * M * GM;
            vec3 c = vec3(MM * vec4((o.geo->bmin + o.geo->bmax) * .5f, 1));
            items.push_back({&o, &p, MM, op, glm::length(c - camPos)});
        }
    }
    if (transparentPass) std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.dist > b.dist; });
    if (!shadowPass && !prepassMode) {
        glUniform1i(U(prog, "uReflect"), reflectPass ? 1 : 0);
        glUniform1f(U(prog, "uGroundY"), groundYCur);
        glUniform1f(U(prog, "uReflFade"), s.reflection.fade);
        glUniform1f(U(prog, "uReflStrength"), s.reflection.strength);
        glUniform1i(U(prog, "uHasAO"), aoTexCur ? 1 : 0);
        glUniform1f(U(prog, "uAOStrength"), s.quality.aoStrength);
        glUniform2f(U(prog, "uScreen"), screenCur.x, screenCur.y);
        glUniform1i(U(prog, "tAO"), 6);
        glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, aoTexCur ? aoTexCur : whiteTex);
        glUniform1f(U(prog, "uLightSize"), s.catcher.softness);
        glUniform1f(U(prog, "uDepthRange"), depthRangeCur);
        glUniform1f(U(prog, "uTexPerWorld"), texPerWorldCur);
        // lights
        vec4 lp[4]; vec3 lc[4]; int nl = 0;
        for (auto& l : s.lights) {
            if (!l.enabled || nl >= 4) continue;
            vec3 p = l.pos.eval(f);
            lp[nl] = vec4(l.type == 0 ? glm::normalize(p + vec3(1e-6f)) : p, (float)l.type);
            lc[nl] = l.color * std::max(0.f, l.intensity.eval(f));
            nl++;
        }
        glUniform1i(U(prog, "uNL"), nl);
        glUniform4fv(U(prog, "uLP"), 4, glm::value_ptr(lp[0]));
        glUniform3fv(U(prog, "uLC"), 4, glm::value_ptr(lc[0]));
        glUniform3fv(U(prog, "uCam"), 1, glm::value_ptr(camPos));
        glUniform1i(U(prog, "uShadowL"), shadowOn);
        glUniformMatrix4fv(U(prog, "uShadowVP"), 1, GL_FALSE, glm::value_ptr(shadowVP));
        glUniform1f(U(prog, "uShadowSoft"), s.catcher.softness);
        float er = glm::radians(s.env.rotation.eval(f));
        mat3 envRot = mat3(glm::rotate(mat4(1), er, vec3(0, 1, 0)));
        glUniformMatrix3fv(U(prog, "uEnvRot"), 1, GL_FALSE, glm::value_ptr(envRot));
        glUniform1f(U(prog, "uEnvMax"), (float)(envMips - 1));
        glUniform1f(U(prog, "uEnvInt"), s.env.intensity);
        glUniform1i(U(prog, "tBase"), 0); glUniform1i(U(prog, "tMR"), 1); glUniform1i(U(prog, "tNormal"), 2);
        glUniform1i(U(prog, "tEmis"), 3); glUniform1i(U(prog, "tEnv"), 4); glUniform1i(U(prog, "tShadow"), 5);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_CUBE_MAP, envCube);
        glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, shadowTex);
    }
    for (auto& it : items) {
        const Object& o = *it.o; const Part& p = *it.p;
        glUniformMatrix4fv(U(prog, "uM"), 1, GL_FALSE, glm::value_ptr(it.M));
        {
            int ax = std::clamp(o.twistAxis, 0, 2);
            vec3 bmn = o.geo->bmin, bmx = o.geo->bmax, ctr = (bmn + bmx) * .5f;
            float len = bmx[ax] - bmn[ax];
            float start = bmn[ax] - o.twistOffset * len;
            glUniform1f(U(prog, "uTwist"), glm::radians(o.twist));
            glUniform1i(U(prog, "uTwistAxis"), ax);
            glUniform3fv(U(prog, "uTwistC"), 1, glm::value_ptr(ctr));
            glUniform2f(U(prog, "uTwistRange"), start, len);
        }
        if (prepassMode) {
            mat3 NM = glm::transpose(glm::inverse(mat3(it.M)));
            glUniformMatrix3fv(U(prog, "uNM"), 1, GL_FALSE, glm::value_ptr(NM));
        }
        if (!shadowPass && !prepassMode) {
            mat3 NM = glm::transpose(glm::inverse(mat3(it.M)));
            glUniformMatrix3fv(U(prog, "uNM"), 1, GL_FALSE, glm::value_ptr(NM));
            const Material& om = (p.slot == 1 && o.edgeMat) ? o.mat2 : o.mat;
            MatParams m;
            m.base = vec4(glm::pow(om.color, vec3(2.2f)), 1); m.metal = om.metallic; m.rough = om.roughness; m.coat = om.clearcoat;
            m.emis = glm::pow(om.color, vec3(2.2f)) * om.emissiveStrength + om.emissive * 0.f;
            m.tb = whiteTex; m.tmr = whiteTex; m.tn = 0; m.te = whiteTex; m.alphaMode = 0; m.cutoff = .5f;
            bool modelMat = o.type == ObjType::Model && o.keepModelMaterials && p.mat.has;
            bool imageMat = (o.type == ObjType::Logo || o.type == ObjType::ImageCard) && o.useImageColours && p.mat.has && !(p.slot == 1 && o.edgeMat);
            if (o.type == ObjType::ImageCard && p.mat.baseTex) { m.tb = p.mat.baseTex->id; m.alphaMode = 2; if (!o.useImageColours) m.alphaMode = 2; }
            if (modelMat) {
                m.base = p.mat.base; m.metal = p.mat.metallic; m.rough = p.mat.roughness; m.coat = 0; m.emis = p.mat.emissive;
                if (p.mat.baseTex) m.tb = p.mat.baseTex->id;
                if (p.mat.mrTex) m.tmr = p.mat.mrTex->id;
                if (p.mat.normalTex) m.tn = p.mat.normalTex->id;
                if (p.mat.emisTex) { m.te = p.mat.emisTex->id; if (glm::length(m.emis) < 1e-4f) m.emis = vec3(1); }
                m.alphaMode = p.mat.alphaMode; m.cutoff = p.mat.alphaCutoff;
            } else if (imageMat) {
                m.base = p.mat.base;
                if (p.mat.baseTex) m.tb = p.mat.baseTex->id;
                m.emis = vec3(m.base) * om.emissiveStrength;
                if (p.mat.baseTex) m.te = p.mat.baseTex->id;
            }
            glUniform4fv(U(prog, "uBase"), 1, glm::value_ptr(m.base));
            glUniform1f(U(prog, "uMetal"), m.metal); glUniform1f(U(prog, "uRough"), m.rough); glUniform1f(U(prog, "uCoat"), m.coat);
            glUniform3fv(U(prog, "uEmis"), 1, glm::value_ptr(m.emis));
            glUniform1i(U(prog, "uHasNormal"), m.tn ? 1 : 0);
            glUniform1i(U(prog, "uHasMR"), m.tmr != whiteTex ? 1 : 0);
            glUniform1f(U(prog, "uOpacity"), it.op);
            glUniform1i(U(prog, "uAlphaMode"), m.alphaMode); glUniform1f(U(prog, "uCutoff"), m.cutoff);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.tb);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m.tmr);
            glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, m.tn ? m.tn : whiteTex);
            glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, m.te);
        }
        glBindVertexArray(p.vao);
        glDrawElements(GL_TRIANGLES, p.count, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
}

void Renderer::drawLines(const std::vector<vec3>& pts, const vec4& col, const mat4& VP) {
    if (pts.empty()) return;
    glUseProgram(progLines);
    glUniformMatrix4fv(U(progLines, "uVP"), 1, GL_FALSE, glm::value_ptr(VP));
    glUniform4fv(U(progLines, "uCol"), 1, glm::value_ptr(col));
    glBindVertexArray(lineVao); glBindBuffer(GL_ARRAY_BUFFER, lineVbo);
    glBufferData(GL_ARRAY_BUFFER, pts.size() * 12, pts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, (GLsizei)pts.size());
    glBindVertexArray(0);
}

void Renderer::renderPass(Scene& s, float f, RenderTarget& rt, const ViewOptions& vo) {
    for (auto& o : s.objects) if (o.visible) ensureGeometry(o);
    float aspect = vo.aspect > 0 ? vo.aspect : rt.w / float(rt.h);
    CamEval cam = evalCamera(s, f, aspect);
    vec3 mn, mx; bool any = worldBounds(s, f, mn, mx);
    vec3 centre = (mn + mx) * .5f; float radius = std::max(0.5f, glm::length(mx - mn) * .5f);
    // shadow light
    int shadowL = -1, li = 0; vec3 L(0, 1, 0);
    for (auto& l : s.lights) {
        if (!l.enabled) continue;
        if (li >= 4) break;
        if (shadowL < 0 && l.shadow && l.type == 0) { shadowL = li; L = glm::normalize(l.pos.eval(f) + vec3(1e-6f)); }
        if (shadowL < 0 && l.shadow && l.type == 1) { shadowL = li; L = glm::normalize(l.pos.eval(f) - centre + vec3(1e-6f)); }
        li++;
    }
    float groundY = s.catcher.autoHeight ? mn.y : s.catcher.height;
    if (s.catcher.autoHeight) groundY += s.catcher.height;
    groundYCur = groundY;
    bool floorUsed = s.catcher.enabled || s.reflection.enabled;
    vec3 sceneC = centre; float sceneR = radius;
    if (floorUsed) { sceneC.y = (mx.y + groundY) * .5f; sceneR = std::max(radius, (mx.y - groundY) * .5f) * 1.6f; }
    mat4 shadowVP(1);
    glEnable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    extraM = mat4(1); reflectPass = false; prepassMode = false;
    if (shadowL >= 0) {
        ensureShadowMap(s.quality.shadowRes);
        vec3 up = std::fabs(L.y) > 0.99f ? vec3(1, 0, 0) : vec3(0, 1, 0);
        mat4 LV = glm::lookAt(sceneC + L * sceneR * 3.f, sceneC, up);
        mat4 LP = glm::ortho(-sceneR, sceneR, -sceneR, sceneR, 0.01f, sceneR * 6.f);
        shadowVP = LP * LV;
        depthRangeCur = sceneR * 6.f;
        texPerWorldCur = shadowResCur / (2.f * sceneR);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo);
        glViewport(0, 0, shadowResCur, shadowResCur);
        glClear(GL_DEPTH_BUFFER_BIT);
        drawObjects(s, f, LV, LP, vec3(0), true, mat4(1), -1, false);
    }
    float R = sceneR * 3.f;
    // ambient occlusion: normals + depth, then occlusion, then blur
    aoTexCur = 0;
    screenCur = glm::vec2(rt.w, rt.h);
    if (s.quality.ao && any) {
        glBindFramebuffer(GL_FRAMEBUFFER, rt.ndFbo);
        glViewport(0, 0, rt.w, rt.h);
        glClearColor(0, 0, 0, 1);  // alpha >= 0 means "no surface" (surfaces have negative view z)
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        prepassMode = true;
        drawObjects(s, f, cam.view, cam.proj, cam.pos, false, mat4(1), -1, false);
        if (s.catcher.enabled) {
            glUseProgram(progPrepass);
            mat4 VP = cam.proj * cam.view;
            glUniformMatrix4fv(U(progPrepass, "uVP"), 1, GL_FALSE, glm::value_ptr(VP));
            glUniformMatrix4fv(U(progPrepass, "uV"), 1, GL_FALSE, glm::value_ptr(cam.view));
            drawCatcherQuad(progPrepass, sceneC, R, groundY);
        }
        prepassMode = false;
        glDisable(GL_DEPTH_TEST);
        static vec3 kernel[16]; static bool kInit = false;
        if (!kInit) {
            for (int i = 0; i < 16; i++) {
                float a = i * 2.39996f, z = 0.15f + 0.85f * ((i * 7) % 16) / 15.f;
                vec3 v(std::cos(a) * std::sqrt(1 - z * z), std::sin(a) * std::sqrt(1 - z * z), z);
                float sc = (i + 1) / 16.f; sc = 0.1f + 0.9f * sc * sc;
                kernel[i] = v * sc;
            }
            kInit = true;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, rt.aoFbo);
        glUseProgram(progSSAO);
        glUniform1i(U(progSSAO, "tND"), 0);
        glUniformMatrix4fv(U(progSSAO, "uP"), 1, GL_FALSE, glm::value_ptr(cam.proj));
        glUniform1f(U(progSSAO, "uRadius"), s.quality.aoRadius * std::max(0.3f, radius / 2.f));
        glUniform3fv(U(progSSAO, "uK"), 16, glm::value_ptr(kernel[0]));
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, rt.ndTex);
        drawFullscreen();
        glBindFramebuffer(GL_FRAMEBUFFER, rt.ao2Fbo);
        glUseProgram(progAOBlur);
        glUniform1i(U(progAOBlur, "tSrc"), 0); glUniform1i(U(progAOBlur, "tND"), 1);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, rt.ndTex);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, rt.aoTex);
        drawFullscreen();
        aoTexCur = rt.ao2Tex;
        glEnable(GL_DEPTH_TEST);
    }
    // main pass
    glBindFramebuffer(GL_FRAMEBUFFER, rt.msFbo);
    glViewport(0, 0, rt.w, rt.h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (rt.samples > 0) glEnable(GL_MULTISAMPLE);
    // floor reflection: the scene mirrored under the floor, fading out
    if (s.reflection.enabled && s.reflection.strength > 0.001f && any) {
        unsigned keepAO = aoTexCur; aoTexCur = 0;
        extraM = glm::translate(mat4(1), vec3(0, groundY, 0)) * glm::scale(mat4(1), vec3(1, -1, 1)) * glm::translate(mat4(1), vec3(0, -groundY, 0));
        reflectPass = true;
        glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        drawObjects(s, f, cam.view, cam.proj, cam.pos, false, shadowVP, -1, false);
        glDepthMask(GL_FALSE);
        drawObjects(s, f, cam.view, cam.proj, cam.pos, false, shadowVP, -1, true);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        reflectPass = false; extraM = mat4(1); aoTexCur = keepAO;
        glClear(GL_DEPTH_BUFFER_BIT);
    }
    drawObjects(s, f, cam.view, cam.proj, cam.pos, false, shadowVP, shadowL, false);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (vo.grid) {
        std::vector<vec3> g;
        float ext = 6.f;
        for (int i = -12; i <= 12; i++) { float t = i * .5f; g.push_back({t, groundY, -ext}); g.push_back({t, groundY, ext}); g.push_back({-ext, groundY, t}); g.push_back({ext, groundY, t}); }
        glDepthMask(GL_FALSE);
        drawLines(g, vec4(0.6f, 0.62f, 0.7f, 0.18f), cam.proj * cam.view);
        glDepthMask(GL_TRUE);
    }
    if (s.catcher.enabled && (shadowL >= 0 || aoTexCur)) {
        glDepthMask(GL_FALSE);
        glUseProgram(progCatcher);
        mat4 VP = cam.proj * cam.view;
        glUniformMatrix4fv(U(progCatcher, "uVP"), 1, GL_FALSE, glm::value_ptr(VP));
        glUniformMatrix4fv(U(progCatcher, "uShadowVP"), 1, GL_FALSE, glm::value_ptr(shadowVP));
        glUniform1f(U(progCatcher, "uOpacity"), s.catcher.opacity);
        glUniform3fv(U(progCatcher, "uCentre"), 1, glm::value_ptr(sceneC));
        glUniform1f(U(progCatcher, "uRadius"), R);
        glUniform3fv(U(progCatcher, "uL"), 1, glm::value_ptr(L));
        glUniform1i(U(progCatcher, "uHasShadow"), shadowL >= 0 ? 1 : 0);
        glUniform1f(U(progCatcher, "uLightSize"), s.catcher.softness);
        glUniform1f(U(progCatcher, "uDepthRange"), depthRangeCur);
        glUniform1f(U(progCatcher, "uTexPerWorld"), texPerWorldCur);
        glUniform1i(U(progCatcher, "uHasAO"), aoTexCur ? 1 : 0);
        glUniform1f(U(progCatcher, "uAOStrength"), s.quality.aoStrength);
        glUniform2f(U(progCatcher, "uScreen"), (float)rt.w, (float)rt.h);
        glUniform1i(U(progCatcher, "tShadow"), 5); glUniform1i(U(progCatcher, "tAO"), 6);
        glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, shadowTex);
        glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, aoTexCur ? aoTexCur : whiteTex);
        drawCatcherQuad(progCatcher, sceneC, R, groundY);
        glDepthMask(GL_TRUE);
    }
    // transparent objects
    glDepthMask(GL_FALSE);
    drawObjects(s, f, cam.view, cam.proj, cam.pos, false, shadowVP, shadowL, true);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    // resolve
    glBindFramebuffer(GL_FRAMEBUFFER, rt.hdrFbo); glViewport(0, 0, rt.w, rt.h);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(progResolve);
    glUniform1i(U(progResolve, "tMS"), 0); glUniform1i(U(progResolve, "uS"), std::max(1, rt.samples));
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, rt.msColor);
    drawFullscreen();
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned Renderer::render(Scene& s, float f, RenderTarget& rt, const ViewOptions& vo) {
    updateEnvironment(s.env);
    GLuint hdrSrc = rt.hdrTex;
    bool mb = s.motionBlur && s.mbSamples > 1 && !vo.preview;
    if (mb) {
        int n = std::clamp(s.mbSamples, 2, 64);
        float span = s.shutter / 360.f;
        glBindFramebuffer(GL_FRAMEBUFFER, rt.accFbo); glViewport(0, 0, rt.w, rt.h);
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        for (int i = 0; i < n; i++) {
            float sf = f - span * .5f + span * (i + .5f) / n;
            renderPass(s, sf, rt, vo);
            glBindFramebuffer(GL_FRAMEBUFFER, rt.accFbo); glViewport(0, 0, rt.w, rt.h);
            glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
            glUseProgram(progAccum); glUniform1i(U(progAccum, "tSrc"), 0); glUniform1f(U(progAccum, "uW"), 1.f / n);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, rt.hdrTex);
            drawFullscreen();
            glDisable(GL_BLEND);
        }
        hdrSrc = rt.accTex;
    } else renderPass(s, f, rt, vo);

    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    // glow
    if (s.glow > 0.001f) {
        glUseProgram(progBright); glUniform1i(U(progBright, "tSrc"), 0); glUniform1f(U(progBright, "uThr"), s.glowThreshold);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[0]); glViewport(0, 0, rt.bloomW[0], rt.bloomH[0]);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hdrSrc); drawFullscreen();
        glUseProgram(progDown); glUniform1i(U(progDown, "tSrc"), 0);
        for (int i = 1; i < 6; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[i]); glViewport(0, 0, rt.bloomW[i], rt.bloomH[i]);
            glBindTexture(GL_TEXTURE_2D, rt.bloomTex[i - 1]); drawFullscreen();
        }
        glUseProgram(progUp); glUniform1i(U(progUp, "tSrc"), 0);
        glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
        for (int i = 5; i > 0; i--) {
            glBindFramebuffer(GL_FRAMEBUFFER, rt.bloomFbo[i - 1]); glViewport(0, 0, rt.bloomW[i - 1], rt.bloomH[i - 1]);
            glBindTexture(GL_TEXTURE_2D, rt.bloomTex[i]); drawFullscreen();
        }
        glDisable(GL_BLEND);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, rt.outFbo); glViewport(0, 0, rt.w, rt.h);
    glUseProgram(progTonemap);
    glUniform1i(U(progTonemap, "tHdr"), 0); glUniform1i(U(progTonemap, "tBloom"), 1);
    glUniform1f(U(progTonemap, "uExposure"), s.env.exposure);
    glUniform1f(U(progTonemap, "uGlow"), s.glow);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, rt.bloomTex[0]);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hdrSrc);
    drawFullscreen();
    // selection box (viewport only)
    if (vo.preview && vo.selected >= 0) {
        for (auto& o : s.objects) if (o.id == vo.selected && o.geo && o.visible) {
            CamEval cam = evalCamera(s, f, vo.aspect > 0 ? vo.aspect : rt.w / float(rt.h));
            mat4 M = objectMatrix(o, f);
            vec3 a = o.geo->bmin, b = o.geo->bmax;
            vec3 c[8]; for (int i = 0; i < 8; i++) c[i] = vec3(M * vec4((i & 1) ? b.x : a.x, (i & 2) ? b.y : a.y, (i & 4) ? b.z : a.z, 1));
            int e[24] = {0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4, 0, 4, 1, 5, 2, 6, 3, 7};
            std::vector<vec3> pts; for (int k : e) pts.push_back(c[k]);
            glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            drawLines(pts, vec4(1.f, 0.72f, 0.2f, 0.9f), cam.proj * cam.view);
            glDisable(GL_BLEND);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return rt.outTex;
}
