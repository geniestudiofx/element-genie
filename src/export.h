#pragma once
#include <string>
#include <vector>
#include <cstdint>
struct Exporter {
    std::string path;
    int format = 0;  // 0 = QuickTime Animation .mov (alpha), 1 = PNG sequence
    int w = 0, h = 0, total = 0, written = 0;
    float fps = 25;
    void* f = nullptr;
    uint64_t pos = 0, mdatStart = 0;
    std::vector<uint32_t> sizes;
    std::vector<uint64_t> offsets;
    bool begin(const std::string& path, int format, int w, int h, float fps, int frames, std::string& err);
    bool addFrame(const uint8_t* premulBottomUp, std::string& err);
    bool finish(std::string& err);
    void abort();
};
