// Element Genie - keyframe animation
#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

// Interpolation leaving / arriving at a key. Smooth on both sides = "easy ease".
struct KeyEase { bool in = true, out = true, hold = false; };

template <class T> struct Key {
    int frame = 0;
    T v{};
    KeyEase ease;
};

inline float lerpT(float a, float b, float t) { return a + (b - a) * t; }
inline glm::vec3 lerpT(const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; }

// Cubic bezier timing curve (like CSS cubic-bezier with y1=0,y2=1).
inline float bezierTiming(float t, float x1, float x2) {
    // solve x(s)=t, return y(s) with y control points 0,1
    auto bx = [&](float s) { float i = 1 - s; return 3 * i * i * s * x1 + 3 * i * s * s * x2 + s * s * s; };
    auto by = [&](float s) { float i = 1 - s; return 3 * i * s * s + s * s * s; };
    float lo = 0, hi = 1, s = t;
    for (int k = 0; k < 30; k++) { s = (lo + hi) * .5f; if (bx(s) < t) lo = s; else hi = s; }
    return by(s);
}

template <class T> struct Track {
    T value{};
    bool animated = false;
    std::vector<Key<T>> keys;

    Track() {}
    Track(T v) : value(v) {}

    T eval(float f) const {
        if (!animated || keys.empty()) return value;
        if (f <= keys.front().frame) return keys.front().v;
        if (f >= keys.back().frame) return keys.back().v;
        for (size_t i = 0; i + 1 < keys.size(); i++) {
            const auto& a = keys[i];
            const auto& b = keys[i + 1];
            if (f >= a.frame && f <= b.frame) {
                if (a.ease.hold) return a.v;
                float t = (f - a.frame) / float(std::max(1, b.frame - a.frame));
                float x1 = a.ease.out ? .42f : .0f, x2 = b.ease.in ? .58f : 1.f;
                if (x1 != 0.f || x2 != 1.f) t = bezierTiming(t, x1, x2);
                return lerpT(a.v, b.v, t);
            }
        }
        return keys.back().v;
    }
    int keyIndexAt(int frame) const {
        for (size_t i = 0; i < keys.size(); i++) if (keys[i].frame == frame) return (int)i;
        return -1;
    }
    void sortKeys() { std::sort(keys.begin(), keys.end(), [](auto& a, auto& b) { return a.frame < b.frame; }); }
    // AE-style: once a channel is animated, any change writes a key at the current frame.
    void set(int frame, T v) {
        if (!animated) { value = v; return; }
        int i = keyIndexAt(frame);
        if (i >= 0) keys[i].v = v;
        else { Key<T> k; k.frame = frame; k.v = v; keys.push_back(k); sortKeys(); }
    }
    void addKey(int frame, T v) { bool a = animated; animated = true; set(frame, v); (void)a; }
    void setAnimated(bool on, int frame) {
        if (on && !animated) { animated = true; keys.clear(); Key<T> k; k.frame = frame; k.v = value; keys.push_back(k); }
        else if (!on && animated) { value = eval((float)frame); animated = false; keys.clear(); }
    }
    void removeKey(int frame) {
        int i = keyIndexAt(frame);
        if (i >= 0) keys.erase(keys.begin() + i);
        if (keys.empty()) animated = false;
    }
    bool hasKeys() const { return animated && !keys.empty(); }
};
