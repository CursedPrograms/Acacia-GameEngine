// engine.h : Acacia engine core (header-only): math, input, timing, software renderer, scene.
#pragma once

#include "framework.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace acacia {

// ---------------------------------------------------------------- math
struct Vec2 {
    float x = 0, y = 0;
    Vec2 operator+(Vec2 o) const { return { x + o.x, y + o.y }; }
    Vec2 operator-(Vec2 o) const { return { x - o.x, y - o.y }; }
    Vec2 operator*(float s) const { return { x * s, y * s }; }
    Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
};

struct Color {
    uint8_t r = 0, g = 0, b = 0;
    uint32_t packed() const { return (uint32_t(r) << 16) | (uint32_t(g) << 8) | b; }  // 0x00RRGGBB
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool intersects(const Rect& o) const {
        return x < o.x + o.w && x + w > o.x && y < o.y + o.h && y + h > o.y;
    }
};

// ---------------------------------------------------------------- input
class Input {
public:
    void keyEvent(WPARAM vk, bool down) {
        if (vk >= 256) return;
        if (down && !cur_[vk]) pressed_[vk] = true;
        cur_[vk] = down;
    }
    bool down(int vk) const { return cur_[vk & 255]; }
    bool pressed(int vk) const { return pressed_[vk & 255]; }  // true for one tick after key-down
    void endTick() { std::memset(pressed_, 0, sizeof(pressed_)); }
    void releaseAll() { std::memset(cur_, 0, sizeof(cur_)); }
private:
    bool cur_[256] = {};
    bool pressed_[256] = {};
};

// ---------------------------------------------------------------- timing
class Clock {
public:
    Clock() { QueryPerformanceFrequency(&freq_); QueryPerformanceCounter(&last_); }
    // Seconds since the previous call.
    double lap() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double dt = double(now.QuadPart - last_.QuadPart) / double(freq_.QuadPart);
        last_ = now;
        return dt;
    }
private:
    LARGE_INTEGER freq_{}, last_{};
};

// ---------------------------------------------------------------- renderer
// Software framebuffer of fixed logical size, scaled to the window on present().
class Renderer {
public:
    Renderer(int w, int h) : w_(w), h_(h), pixels_(size_t(w) * h), depth_(size_t(w) * h, kFar) {
        std::memset(&bmi_, 0, sizeof(bmi_));
        bmi_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi_.bmiHeader.biWidth = w;
        bmi_.bmiHeader.biHeight = -h;  // top-down
        bmi_.bmiHeader.biPlanes = 1;
        bmi_.bmiHeader.biBitCount = 32;
        bmi_.bmiHeader.biCompression = BI_RGB;
    }
    int width() const { return w_; }
    int height() const { return h_; }

    static constexpr float kFar = 1e30f;

    // Clears colour and depth.
    void clear(Color c) {
        std::fill(pixels_.begin(), pixels_.end(), c.packed());
        std::fill(depth_.begin(), depth_.end(), kFar);
    }

    // Depth-tested write (smaller z is nearer). Caller guarantees x,y in range.
    void plotDepth(int x, int y, float z, Color c) {
        size_t i = size_t(y) * w_ + x;
        if (z < depth_[i]) { depth_[i] = z; pixels_[i] = c.packed(); }
    }
    float depthAt(int x, int y) const { return depth_[size_t(y) * w_ + x]; }
    const uint32_t* data() const { return pixels_.data(); }

    void setPixel(int x, int y, Color c) {
        if (x >= 0 && y >= 0 && x < w_ && y < h_) pixels_[size_t(y) * w_ + x] = c.packed();
    }

    void fillRect(float fx, float fy, float fw, float fh, Color c) {
        int x0 = std::max(0, int(fx)), y0 = std::max(0, int(fy));
        int x1 = std::min(w_, int(fx + fw)), y1 = std::min(h_, int(fy + fh));
        uint32_t p = c.packed();
        for (int y = y0; y < y1; ++y)
            std::fill(pixels_.begin() + size_t(y) * w_ + x0, pixels_.begin() + size_t(y) * w_ + std::max(x0, x1), p);
    }

    void line(int x0, int y0, int x1, int y1, Color c) {  // Bresenham
        int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            setPixel(x0, y0, c);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    // Draws the framebuffer into hdc, letterboxed to preserve aspect ratio.
    void present(HDC hdc, int winW, int winH) const {
        if (winW <= 0 || winH <= 0) return;
        float s = std::min(float(winW) / w_, float(winH) / h_);
        int dw = int(w_ * s), dh = int(h_ * s);
        int ox = (winW - dw) / 2, oy = (winH - dh) / 2;
        PatBlt(hdc, 0, 0, winW, oy, BLACKNESS);
        PatBlt(hdc, 0, oy + dh, winW, winH - oy - dh, BLACKNESS);
        PatBlt(hdc, 0, oy, ox, dh, BLACKNESS);
        PatBlt(hdc, ox + dw, oy, winW - ox - dw, dh, BLACKNESS);
        SetStretchBltMode(hdc, COLORONCOLOR);
        StretchDIBits(hdc, ox, oy, dw, dh, 0, 0, w_, h_, pixels_.data(), &bmi_, DIB_RGB_COLORS, SRCCOPY);
    }

private:
    int w_, h_;
    std::vector<uint32_t> pixels_;
    std::vector<float> depth_;
    BITMAPINFO bmi_;
};

// ---------------------------------------------------------------- scene
struct Entity {
    Vec2 pos, vel, size{ 16, 16 };
    Color color{ 255, 255, 255 };
    bool alive = true;
    // Optional per-tick behaviour; runs before velocity is integrated.
    std::function<void(Entity&, const Input&, float dt)> onUpdate;

    Rect bounds() const { return { pos.x, pos.y, size.x, size.y }; }
};

class Scene {
public:
    Entity& spawn(Vec2 pos, Vec2 size, Color color) {
        entities_.push_back(std::make_unique<Entity>());
        Entity& e = *entities_.back();
        e.pos = pos; e.size = size; e.color = color;
        return e;
    }

    void update(const Input& in, float dt) {
        // Index loop: onUpdate may spawn, which can reallocate the vector (entities themselves stay put).
        for (size_t i = 0; i < entities_.size(); ++i) {
            Entity& e = *entities_[i];
            if (!e.alive) continue;
            if (e.onUpdate) e.onUpdate(e, in, dt);
            e.pos += e.vel * dt;
        }
        entities_.erase(std::remove_if(entities_.begin(), entities_.end(),
            [](const std::unique_ptr<Entity>& e) { return !e->alive; }), entities_.end());
    }

    void draw(Renderer& r) const {
        for (auto& e : entities_) r.fillRect(e->pos.x, e->pos.y, e->size.x, e->size.y, e->color);
    }

    const std::vector<std::unique_ptr<Entity>>& entities() const { return entities_; }
    void clear() { entities_.clear(); }

private:
    std::vector<std::unique_ptr<Entity>> entities_;
};

// ---------------------------------------------------------------- game loop
// Fixed-timestep accumulator: call advance() once per frame with the wall-clock delta;
// it invokes `tick` zero or more times at a constant step.
class FixedStep {
public:
    explicit FixedStep(double hz = 60.0, int maxTicksPerFrame = 5) : step_(1.0 / hz), maxTicks_(maxTicksPerFrame) {}
    double step() const { return step_; }
    template <class F> void advance(double frameDt, F&& tick) {
        acc_ += std::min(frameDt, step_ * maxTicks_);  // clamp to avoid spiral of death
        while (acc_ >= step_) { tick(float(step_)); acc_ -= step_; }
    }
private:
    double step_, acc_ = 0;
    int maxTicks_;
};

}  // namespace acacia
