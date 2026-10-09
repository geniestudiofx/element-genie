// Element Genie - export: QuickTime Animation (.mov with alpha) and PNG sequence
#include "export.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <stb_image_write.h>

std::wstring widen(const std::string& s);
static FILE* openW(const std::string& p) {
#ifdef _WIN32
    return _wfopen(widen(p).c_str(), L"wb");
#else
    return fopen(p.c_str(), "wb");
#endif
}

// ------------------------------------------------------------------ big-endian box writer
struct Buf {
    std::vector<uint8_t> d;
    void u8(uint8_t v) { d.push_back(v); }
    void u16(uint16_t v) { u8(v >> 8); u8(v & 255); }
    void u32(uint32_t v) { u16(v >> 16); u16(v & 65535); }
    void u64(uint64_t v) { u32((uint32_t)(v >> 32)); u32((uint32_t)v); }
    void tag(const char* t) { d.insert(d.end(), t, t + 4); }
    void raw(const void* p, size_t n) { d.insert(d.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
    void zero(size_t n) { d.insert(d.end(), n, 0); }
    size_t open(const char* t) { size_t at = d.size(); u32(0); tag(t); return at; }
    void close(size_t at) { uint32_t n = (uint32_t)(d.size() - at); d[at] = n >> 24; d[at + 1] = (n >> 16) & 255; d[at + 2] = (n >> 8) & 255; d[at + 3] = n & 255; }
    void matrix() { uint32_t m[9] = {0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000}; for (auto v : m) u32(v); }
};

// QuickTime Animation ("rle "), 32-bit ARGB, full frames.
static void encodeRle32(const uint8_t* argb, int w, int h, std::vector<uint8_t>& out) {
    out.clear();
    out.resize(4); // size placeholder
    out.push_back(0); out.push_back(0); // header: whole frame
    auto px = [&](int x, int y) { return *(const uint32_t*)(argb + ((size_t)y * w + x) * 4); };
    for (int y = 0; y < h; y++) {
        out.push_back(1);  // skip 0 pixels
        int x = 0;
        while (x < w) {
            // count repeat run
            int run = 1;
            while (x + run < w && run < 128 && px(x + run, y) == px(x, y)) run++;
            if (run >= 2) {
                out.push_back((uint8_t)(int8_t)(-run));
                const uint8_t* p = argb + ((size_t)y * w + x) * 4; out.insert(out.end(), p, p + 4);
                x += run;
            } else {
                // literal run until a repeat of >=3 starts
                int start = x, n = 0;
                while (x < w && n < 127) {
                    if (x + 2 < w && px(x, y) == px(x + 1, y) && px(x, y) == px(x + 2, y)) break;
                    x++; n++;
                }
                if (n == 0) { x++; n = 1; }
                out.push_back((uint8_t)n);
                const uint8_t* p = argb + ((size_t)y * w + start) * 4; out.insert(out.end(), p, p + (size_t)n * 4);
            }
        }
        out.push_back(0xFF);  // end of line
    }
    uint32_t n = (uint32_t)out.size();
    out[0] = n >> 24; out[1] = (n >> 16) & 255; out[2] = (n >> 8) & 255; out[3] = n & 255;
}

static void fpsTiming(float fps, uint32_t& timescale, uint32_t& delta) {
    float r = std::round(fps);
    if (std::fabs(fps - r) < 0.005f) { timescale = (uint32_t)r * 100; delta = 100; }
    else { timescale = (uint32_t)std::round(fps * 1001.f / 1000.f) * 1000; delta = 1001; }
}

bool Exporter::begin(const std::string& p, int fmt, int W, int H, float F, int frames, std::string& err) {
    path = p; format = fmt; w = W; h = H; fps = F; total = frames; written = 0;
    sizes.clear(); offsets.clear();
    if (format == 0) {
        f = openW(path);
        if (!f) { err = "Couldn't create the file. Is it open in another program?"; return false; }
        Buf b;
        size_t ft = b.open("ftyp"); b.tag("qt  "); b.u32(0x20050300); b.tag("qt  "); b.close(ft);
        b.u32(1); b.tag("mdat"); b.u64(0);  // 64-bit size, patched later
        fwrite(b.d.data(), 1, b.d.size(), (FILE*)f);
        mdatStart = b.d.size() - 16;
        pos = b.d.size();
    }
    return true;
}

// premul: RGBA8 premultiplied, bottom-up rows (straight from glReadPixels)
bool Exporter::addFrame(const uint8_t* premul, std::string& err) {
    size_t np = (size_t)w * h;
    std::vector<uint8_t> straight(np * 4);
    for (int y = 0; y < h; y++) {
        const uint8_t* src = premul + (size_t)(h - 1 - y) * w * 4;
        uint8_t* dst = straight.data() + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            uint8_t a = src[x * 4 + 3];
            for (int k = 0; k < 3; k++) dst[x * 4 + k] = a ? (uint8_t)std::min(255, (src[x * 4 + k] * 255 + a / 2) / a) : 0;
            dst[x * 4 + 3] = a;
        }
    }
    if (format == 1) {
        char num[32]; snprintf(num, sizeof num, "_%05d.png", written);
        std::string base = path;
        size_t dot = base.find_last_of('.'), slash = base.find_last_of("/\\");
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) base = base.substr(0, dot);
        std::string fn = base + num;
        FILE* pf = openW(fn);
        if (!pf) { err = "Couldn't write " + fn; return false; }
        stbi_write_png_compression_level = 3;
        stbi_write_png_to_func([](void* ctx, void* data, int size) { fwrite(data, 1, size, (FILE*)ctx); }, pf, w, h, 4, straight.data(), w * 4);
        fclose(pf);
    } else {
        std::vector<uint8_t> argb(np * 4);
        for (size_t i = 0; i < np; i++) {
            argb[i * 4 + 0] = straight[i * 4 + 3]; argb[i * 4 + 1] = straight[i * 4 + 0];
            argb[i * 4 + 2] = straight[i * 4 + 1]; argb[i * 4 + 3] = straight[i * 4 + 2];
        }
        std::vector<uint8_t> enc; encodeRle32(argb.data(), w, h, enc);
        if (fwrite(enc.data(), 1, enc.size(), (FILE*)f) != enc.size()) { err = "Disk full?"; return false; }
        offsets.push_back(pos); sizes.push_back((uint32_t)enc.size()); pos += enc.size();
    }
    written++;
    return true;
}

bool Exporter::finish(std::string& err) {
    if (format == 1) return true;
    FILE* fp = (FILE*)f;
    uint32_t ts, dl; fpsTiming(fps, ts, dl);
    uint32_t n = (uint32_t)sizes.size();
    uint32_t dur = n * dl;
    // patch mdat size
    uint64_t mdatSize = pos - mdatStart;
    Buf m; m.u64(mdatSize);
    fseek(fp, (long)(mdatStart + 8), SEEK_SET); fwrite(m.d.data(), 1, 8, fp);
    fseek(fp, 0, SEEK_END);
#ifdef _WIN32
    _fseeki64(fp, 0, SEEK_END);
#endif
    Buf b;
    size_t moov = b.open("moov");
    size_t mvhd = b.open("mvhd"); b.u32(0); b.u32(0); b.u32(0); b.u32(ts); b.u32(dur); b.u32(0x10000); b.u16(0x100); b.zero(10);
    b.matrix(); b.zero(24); b.u32(2); b.close(mvhd);
    size_t trak = b.open("trak");
    size_t tkhd = b.open("tkhd"); b.u32(0x0000000F); b.u32(0); b.u32(0); b.u32(1); b.u32(0); b.u32(dur); b.zero(8); b.u16(0); b.u16(0); b.u16(0); b.u16(0);
    b.matrix(); b.u32((uint32_t)w << 16); b.u32((uint32_t)h << 16); b.close(tkhd);
    size_t mdia = b.open("mdia");
    size_t mdhd = b.open("mdhd"); b.u32(0); b.u32(0); b.u32(0); b.u32(ts); b.u32(dur); b.u16(0x7FFF); b.u16(0); b.close(mdhd);
    size_t hd = b.open("hdlr"); b.u32(0); b.tag("mhlr"); b.tag("vide"); b.tag("appl"); b.u32(0); b.u32(0);
    const char* nm = "Video Media Handler"; b.u8((uint8_t)strlen(nm)); b.raw(nm, strlen(nm)); b.close(hd);
    size_t minf = b.open("minf");
    size_t vmhd = b.open("vmhd"); b.u32(1); b.u16(0x40); b.u16(0x8000); b.u16(0x8000); b.u16(0x8000); b.close(vmhd);
    size_t hd2 = b.open("hdlr"); b.u32(0); b.tag("dhlr"); b.tag("alis"); b.tag("appl"); b.u32(0); b.u32(0);
    const char* nm2 = "Data Handler"; b.u8((uint8_t)strlen(nm2)); b.raw(nm2, strlen(nm2)); b.close(hd2);
    size_t dinf = b.open("dinf"); size_t dref = b.open("dref"); b.u32(0); b.u32(1);
    size_t al = b.open("alis"); b.u32(1); b.close(al); b.close(dref); b.close(dinf);
    size_t stbl = b.open("stbl");
    size_t stsd = b.open("stsd"); b.u32(0); b.u32(1);
    size_t rle = b.open("rle "); b.zero(6); b.u16(1); b.u16(0); b.u16(0); b.tag("appl"); b.u32(0); b.u32(1023);
    b.u16((uint16_t)w); b.u16((uint16_t)h); b.u32(72 << 16); b.u32(72 << 16); b.u32(0); b.u16(1);
    char cname[32] = {0}; const char* cn = "Animation"; cname[0] = (char)strlen(cn); memcpy(cname + 1, cn, strlen(cn)); b.raw(cname, 32);
    b.u16(32); b.u16(0xFFFF); b.close(rle); b.close(stsd);
    size_t stts = b.open("stts"); b.u32(0); b.u32(1); b.u32(n); b.u32(dl); b.close(stts);
    size_t stsc = b.open("stsc"); b.u32(0); b.u32(1); b.u32(1); b.u32(1); b.u32(1); b.close(stsc);
    size_t stsz = b.open("stsz"); b.u32(0); b.u32(0); b.u32(n); for (auto s : sizes) b.u32(s); b.close(stsz);
    size_t co = b.open("co64"); b.u32(0); b.u32(n); for (auto o : offsets) b.u64(o); b.close(co);
    b.close(stbl); b.close(minf); b.close(mdia); b.close(trak); b.close(moov);
    bool ok = fwrite(b.d.data(), 1, b.d.size(), fp) == b.d.size();
    fclose(fp); f = nullptr;
    if (!ok) err = "Couldn't finish writing the file (disk full?)";
    return ok;
}

void Exporter::abort() { if (f) { fclose((FILE*)f); f = nullptr; } }

void writePngFlip(const std::string& path, int w, int h, const uint8_t* rgbaBottomUp) {
    std::vector<uint8_t> f((size_t)w * h * 4);
    for (int y = 0; y < h; y++) memcpy(&f[(size_t)y * w * 4], rgbaBottomUp + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    for (size_t i = 3; i < f.size(); i += 4) f[i] = 255;
    FILE* fp = openW(path); if (!fp) return;
    stbi_write_png_to_func([](void* c, void* d, int n) { fwrite(d, 1, n, (FILE*)c); }, fp, w, h, 4, f.data(), w * 4);
    fclose(fp);
}
