// engine3d.h : Acacia 3D layer (header-only): vectors/matrices, camera, lighting,
// software triangle rasterizer, and a procedural terrain generator.
// Conventions: right-handed, +Y up, camera looks down -Z at yaw 0, CCW triangles are front faces.
#pragma once

#include "engine.h"
#include <cmath>

namespace acacia {

// ---------------------------------------------------------------- math
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(Vec3 o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
    Vec3 operator*(Vec3 o) const { return { x * o.x, y * o.y, z * o.z }; }
    Vec3 operator-() const { return { -x, -y, -z }; }
    Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
};
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) { float l = length(a); return l > 1e-8f ? a * (1.f / l) : Vec3{ 0, 1, 0 }; }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float smoothstep(float a, float b, float x) { float t = clamp01((x - a) / (b - a)); return t * t * (3 - 2 * t); }

struct Vec4 { float x = 0, y = 0, z = 0, w = 1; };

// Row-major 4x4; transforms column vectors (v' = M * v).
struct Mat4 {
    float m[4][4] = {};
    static Mat4 identity() { Mat4 r; for (int i = 0; i < 4; ++i) r.m[i][i] = 1; return r; }
    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }
    Vec4 operator*(Vec4 v) const {
        return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w,
                 m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w,
                 m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w,
                 m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w };
    }
    static Mat4 perspective(float fovY, float aspect, float zn, float zf) {
        float f = 1.f / std::tan(fovY * 0.5f);
        Mat4 r;
        r.m[0][0] = f / aspect;
        r.m[1][1] = f;
        r.m[2][2] = (zf + zn) / (zn - zf);
        r.m[2][3] = 2 * zf * zn / (zn - zf);
        r.m[3][2] = -1;
        return r;
    }
    static Mat4 lookAt(Vec3 eye, Vec3 fwd, Vec3 upHint = { 0, 1, 0 }) {
        Vec3 r = normalize(cross(fwd, upHint)), u = cross(r, fwd);
        Mat4 v = identity();
        v.m[0][0] = r.x;    v.m[0][1] = r.y;    v.m[0][2] = r.z;    v.m[0][3] = -dot(r, eye);
        v.m[1][0] = u.x;    v.m[1][1] = u.y;    v.m[1][2] = u.z;    v.m[1][3] = -dot(u, eye);
        v.m[2][0] = -fwd.x; v.m[2][1] = -fwd.y; v.m[2][2] = -fwd.z; v.m[2][3] = dot(fwd, eye);
        return v;
    }
};

// ---------------------------------------------------------------- camera
struct Camera {
    Vec3 pos{ 0, 10, 0 };
    float yaw = 0, pitch = 0;           // radians; yaw 0 looks down -Z, positive yaw turns right
    float fovY = 1.15f, zNear = 0.1f, zFar = 600.f;

    Vec3 forward() const {
        float cp = std::cos(pitch);
        return { std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp };
    }
    Vec3 right() const { return normalize(cross(forward(), { 0, 1, 0 })); }
    Mat4 view() const { return Mat4::lookAt(pos, forward()); }
    Mat4 proj(float aspect) const { return Mat4::perspective(fovY, aspect, zNear, zFar); }
};

// ---------------------------------------------------------------- lighting
struct PointLight {
    Vec3 pos;
    Vec3 color{ 1.0f, 0.8f, 0.55f };
    float radius = 30.f;                // light reaches zero at this distance
    float intensity = 1.5f;
};

struct Lighting {
    Vec3 sunDir{ -0.5f, -0.7f, -0.4f };  // direction the light travels (normalised on use)
    Vec3 sunColor{ 1.0f, 0.95f, 0.85f };
    float sunIntensity = 1.1f;
    Vec3 skyAmbient{ 0.32f, 0.40f, 0.55f };    // ambient from above
    Vec3 groundAmbient{ 0.18f, 0.16f, 0.12f }; // bounce from below
    std::vector<PointLight> points;
    Vec3 fogColor{ 0.62f, 0.72f, 0.85f };
    float fogNear = 80.f, fogFar = 420.f;

    // Sun direction from a time-of-day angle (0 = sunrise east, pi/2 = noon, pi = sunset west).
    void setSunAngle(float a) {
        sunDir = normalize(Vec3{ -std::cos(a), -std::sin(a), -0.35f });
    }
    // Sun elevation in [-1,1] (1 = overhead).
    float sunElevation() const { return -normalize(sunDir).y; }

    // Adapts sun/ambient/fog colours to the sun's elevation (dawn/dusk warm, night dark).
    void updateAtmosphere() {
        float e = sunElevation();
        float day = smoothstep(-0.10f, 0.25f, e);          // 0 at night, 1 in daylight
        float warm = 1.f - smoothstep(0.05f, 0.55f, e);    // strongest near horizon
        sunColor = lerp(Vec3{ 1.0f, 0.96f, 0.88f }, Vec3{ 1.0f, 0.55f, 0.28f }, warm * day);
        sunIntensity = 1.15f * day;
        skyAmbient = lerp(Vec3{ 0.07f, 0.09f, 0.18f }, Vec3{ 0.32f, 0.40f, 0.55f }, day);
        groundAmbient = lerp(Vec3{ 0.03f, 0.03f, 0.05f }, Vec3{ 0.18f, 0.16f, 0.12f }, day);
        Vec3 dayFog{ 0.62f, 0.72f, 0.85f }, duskFog{ 0.85f, 0.55f, 0.40f }, nightFog{ 0.02f, 0.03f, 0.07f };
        fogColor = lerp(nightFog, lerp(dayFog, duskFog, warm), day);
    }

    Vec3 skyTop() const { return fogColor * Vec3{ 0.55f, 0.65f, 0.95f }; }

    // Lit colour for a surface point. albedo is 0..1; sunVis in [0,1] is baked shadow visibility.
    Vec3 shade(Vec3 pos, Vec3 n, Vec3 albedo, float sunVis) const {
        Vec3 L = normalize(-sunDir);
        Vec3 light = lerp(groundAmbient, skyAmbient, n.y * 0.5f + 0.5f);
        light += sunColor * (sunIntensity * sunVis * std::max(0.f, dot(n, L)));
        for (const PointLight& p : points) {
            Vec3 d = p.pos - pos;
            float dist = length(d);
            if (dist >= p.radius) continue;
            float att = 1.f - dist / p.radius;
            light += p.color * (p.intensity * att * att * std::max(0.f, dot(n, d * (1.f / dist))));
        }
        return albedo * light;
    }

    Vec3 applyFog(Vec3 c, float dist) const {
        return lerp(c, fogColor, smoothstep(fogNear, fogFar, dist));
    }
};

// ---------------------------------------------------------------- mesh
struct Vertex {
    Vec3 pos, normal, albedo{ 1, 1, 1 };
    float sunVis = 1.f;                 // baked sun visibility (shadows)
};

struct Mesh {
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;      // triples, CCW = front face
};

// ---------------------------------------------------------------- rasterizer
namespace detail {
struct ClipVert { Vec4 p; Vec3 c; };
struct ScreenVert { float x, y, z, invW; Vec3 c; };

inline Color toColor(Vec3 c) {
    auto q = [](float v) { return uint8_t(clamp01(v) * 255.f + 0.5f); };
    return { q(c.x), q(c.y), q(c.z) };
}

// Fills one screen-space triangle with perspective-correct colour interpolation and depth testing.
inline void rasterTriangle(Renderer& r, const ScreenVert& a, const ScreenVert& b, const ScreenVert& c) {
    auto edge = [](const ScreenVert& p, const ScreenVert& q, float x, float y) {
        return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x);
    };
    float area = edge(a, b, c.x, c.y);
    if (area >= 0.f) return;            // back-facing (front faces are negative in y-down pixels) or degenerate
    float inv = 1.f / area;

    int x0 = std::max(0, int(std::floor(std::min({ a.x, b.x, c.x }))));
    int x1 = std::min(r.width() - 1, int(std::ceil(std::max({ a.x, b.x, c.x }))));
    int y0 = std::max(0, int(std::floor(std::min({ a.y, b.y, c.y }))));
    int y1 = std::min(r.height() - 1, int(std::ceil(std::max({ a.y, b.y, c.y }))));

    for (int y = y0; y <= y1; ++y) {
        float py = y + 0.5f;
        for (int x = x0; x <= x1; ++x) {
            float px = x + 0.5f;
            float w0 = edge(b, c, px, py) * inv;
            float w1 = edge(c, a, px, py) * inv;
            float w2 = edge(a, b, px, py) * inv;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            float z = w0 * a.z + w1 * b.z + w2 * c.z;
            if (z >= r.depthAt(x, y)) continue;   // hidden: skip colour work
            float p0 = w0 * a.invW, p1 = w1 * b.invW, p2 = w2 * c.invW;
            float k = 1.f / (p0 + p1 + p2);
            Vec3 col = (a.c * p0 + b.c * p1 + c.c * p2) * k;
            r.plotDepth(x, y, z, toColor(col));
        }
    }
}

inline ScreenVert toScreen(const ClipVert& v, int w, int h) {
    float iw = 1.f / v.p.w;
    return { (v.p.x * iw * 0.5f + 0.5f) * w, (1.f - (v.p.y * iw * 0.5f + 0.5f)) * h, v.p.z * iw, iw, v.c };
}

inline ClipVert lerpClip(const ClipVert& a, const ClipVert& b, float t) {
    return { { a.p.x + (b.p.x - a.p.x) * t, a.p.y + (b.p.y - a.p.y) * t,
               a.p.z + (b.p.z - a.p.z) * t, a.p.w + (b.p.w - a.p.w) * t }, lerp(a.c, b.c, t) };
}

// Clips against the near plane (z > -w), then rasterizes the resulting 1-2 triangles.
inline void drawClipTriangle(Renderer& r, const ClipVert& a, const ClipVert& b, const ClipVert& c) {
    const ClipVert in[3] = { a, b, c };
    ClipVert out[4];
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const ClipVert& cur = in[i];
        const ClipVert& nxt = in[(i + 1) % 3];
        float dc = cur.p.z + cur.p.w, dn = nxt.p.z + nxt.p.w;
        if (dc >= 0) out[n++] = cur;
        if ((dc >= 0) != (dn >= 0)) out[n++] = lerpClip(cur, nxt, dc / (dc - dn));
    }
    if (n < 3) return;
    ScreenVert s[4];
    for (int i = 0; i < n; ++i) s[i] = toScreen(out[i], r.width(), r.height());
    rasterTriangle(r, s[0], s[1], s[2]);
    if (n == 4) rasterTriangle(r, s[0], s[2], s[3]);
}
}  // namespace detail

// Lights, fogs, transforms and draws a world-space mesh.
inline void drawMesh(Renderer& r, const Mesh& mesh, const Camera& cam, const Lighting& lights) {
    float aspect = float(r.width()) / float(r.height());
    Mat4 vp = cam.proj(aspect) * cam.view();

    std::vector<detail::ClipVert> cv(mesh.verts.size());
    for (size_t i = 0; i < mesh.verts.size(); ++i) {
        const Vertex& v = mesh.verts[i];
        Vec3 lit = lights.shade(v.pos, v.normal, v.albedo, v.sunVis);
        cv[i].c = lights.applyFog(lit, length(v.pos - cam.pos));
        cv[i].p = vp * Vec4{ v.pos.x, v.pos.y, v.pos.z, 1.f };
    }
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        detail::drawClipTriangle(r, cv[mesh.indices[i]], cv[mesh.indices[i + 1]], cv[mesh.indices[i + 2]]);
}

// Vertical sky gradient; call after Renderer::clear() and before drawing geometry.
inline void drawSky(Renderer& r, const Camera& cam, const Lighting& lights) {
    // Horizon row shifts with pitch so the gradient stays anchored to the world.
    float horizon = r.height() * (0.5f + std::tan(cam.pitch) / (2.f * std::tan(cam.fovY * 0.5f)));
    Vec3 top = lights.skyTop(), bottom = lights.fogColor;
    for (int y = 0; y < r.height(); ++y) {
        float t = clamp01((horizon - y) / std::max(1.f, r.height() * 0.6f));
        r.fillRect(0, float(y), float(r.width()), 1.f, detail::toColor(lerp(bottom, top, t)));
    }
}

// ---------------------------------------------------------------- terrain
struct TerrainParams {
    int size = 128;                     // vertices per side
    float cell = 2.f;                   // world units between vertices
    float amplitude = 55.f;             // peak height
    float frequency = 0.012f;           // base noise frequency (per world unit)
    int octaves = 6;
    float seaLevel = 10.f;
    uint32_t seed = 1337;
    float islandFalloff = 0.65f;        // 0 = none, 1 = strongly pulls edges below sea
};

class Terrain {
public:
    explicit Terrain(const TerrainParams& p = {}) { generate(p); }

    const TerrainParams& params() const { return p_; }
    const Mesh& mesh() const { return mesh_; }
    const Mesh& water() const { return water_; }
    const Mesh& trees() const { return trees_; }

    // World-space extent along one axis.
    float extent() const { return (p_.size - 1) * p_.cell; }

    // Bilinear ground height at world (x,z); returns seaLevel outside the map.
    float heightAt(float x, float z) const {
        float gx = x / p_.cell, gz = z / p_.cell;
        if (gx < 0 || gz < 0 || gx >= p_.size - 1 || gz >= p_.size - 1) return p_.seaLevel;
        int ix = int(gx), iz = int(gz);
        float fx = gx - ix, fz = gz - iz;
        float h00 = h_[idx(ix, iz)], h10 = h_[idx(ix + 1, iz)];
        float h01 = h_[idx(ix, iz + 1)], h11 = h_[idx(ix + 1, iz + 1)];
        return (h00 * (1 - fx) + h10 * fx) * (1 - fz) + (h01 * (1 - fx) + h11 * fx) * fz;
    }

    void generate(const TerrainParams& p) {
        p_ = p;
        const int N = p_.size;
        h_.assign(size_t(N) * N, 0.f);
        const float half = extent() * 0.5f;

        for (int z = 0; z < N; ++z) for (int x = 0; x < N; ++x) {
            float wx = x * p_.cell, wz = z * p_.cell;
            float n = fbm(wx * p_.frequency, wz * p_.frequency);            // 0..1
            float ridge = 1.f - std::fabs(fbm(wx * p_.frequency * 0.6f + 91.f, wz * p_.frequency * 0.6f + 17.f) * 2.f - 1.f);
            float h = std::pow(n, 1.7f) * 0.75f + ridge * ridge * 0.35f * n;  // mountains where n is high
            // Radial falloff so the map reads as an island surrounded by sea.
            float dx = (wx - half) / half, dz = (wz - half) / half;
            float d = clamp01(std::sqrt(dx * dx + dz * dz));
            h -= p_.islandFalloff * smoothstep(0.55f, 1.05f, d);
            h_[idx(x, z)] = h * p_.amplitude;
        }
        buildMesh();
        buildWater();
        buildTrees();
    }

    // Re-bakes per-vertex sun visibility by marching each vertex toward the sun over the heightfield.
    void bakeShadows(Vec3 sunDir) {
        Vec3 toSun = normalize(-sunDir);
        for (size_t i = 0; i < mesh_.verts.size(); ++i) {
            Vertex& v = mesh_.verts[i];
            if (toSun.y <= 0.02f) { v.sunVis = 0.f; continue; }         // sun below horizon
            float vis = 1.f;
            float step = p_.cell * 1.5f;
            Vec3 p = v.pos + Vec3{ 0, 0.5f, 0 };
            for (float t = step; t < extent(); t += step) {
                Vec3 q = p + toSun * t;
                if (q.x < 0 || q.z < 0 || q.x > extent() || q.z > extent() || q.y > p_.amplitude * 1.2f) break;
                float clearance = q.y - heightAt(q.x, q.z);
                if (clearance < 0.f) { vis = 0.f; break; }
                vis = std::min(vis, clamp01(clearance / (0.06f * t + 1.f) * 1.5f));  // soft penumbra
            }
            v.sunVis = vis;
        }
        // Trees inherit the sun visibility of the terrain vertex nearest to them.
        for (Vertex& v : trees_.verts) {
            int gx = std::clamp(int(v.pos.x / p_.cell + 0.5f), 0, p_.size - 1);
            int gz = std::clamp(int(v.pos.z / p_.cell + 0.5f), 0, p_.size - 1);
            v.sunVis = mesh_.verts[idx(gx, gz)].sunVis;
        }
    }

private:
    TerrainParams p_;
    std::vector<float> h_;
    Mesh mesh_, water_, trees_;

    size_t idx(int x, int z) const { return size_t(z) * p_.size + x; }

    // -- noise
    static uint32_t hash(int x, int z, uint32_t seed) {
        uint32_t h = uint32_t(x) * 374761393u + uint32_t(z) * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return h ^ (h >> 16);
    }
    float rnd(int x, int z) const { return (hash(x, z, p_.seed) & 0xFFFFFF) / float(0x1000000); }
    float valueNoise(float x, float z) const {
        int ix = int(std::floor(x)), iz = int(std::floor(z));
        float fx = x - ix, fz = z - iz;
        float ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10), uz = fz * fz * fz * (fz * (fz * 6 - 15) + 10);
        float a = rnd(ix, iz), b = rnd(ix + 1, iz), c = rnd(ix, iz + 1), d = rnd(ix + 1, iz + 1);
        return (a * (1 - ux) + b * ux) * (1 - uz) + (c * (1 - ux) + d * ux) * uz;
    }
    float fbm(float x, float z) const {
        float sum = 0, amp = 0.5f, norm = 0;
        for (int o = 0; o < p_.octaves; ++o) {
            sum += valueNoise(x, z) * amp;
            norm += amp;
            x = x * 2.03f + 31.7f; z = z * 2.03f - 17.3f; amp *= 0.5f;
        }
        return sum / norm;
    }

    // -- meshing
    Vec3 albedoFor(float h, float slope, int x, int z) const {
        float rel = (h - p_.seaLevel) / std::max(1.f, p_.amplitude - p_.seaLevel);
        float jitter = 0.9f + 0.2f * rnd(x + 7919, z + 104729);
        Vec3 sand{ 0.76f, 0.70f, 0.50f }, grass{ 0.24f, 0.46f, 0.16f }, forest{ 0.13f, 0.32f, 0.12f };
        Vec3 rock{ 0.42f, 0.40f, 0.38f }, snow{ 0.95f, 0.96f, 1.0f }, seabed{ 0.30f, 0.30f, 0.26f };
        Vec3 c;
        if (h < p_.seaLevel) c = seabed;
        else if (rel < 0.04f) c = sand;
        else {
            c = lerp(grass, forest, smoothstep(0.08f, 0.35f, rel));
            c = lerp(c, rock, smoothstep(0.45f, 0.65f, rel));
            c = lerp(c, snow, smoothstep(0.75f, 0.9f, rel));
        }
        c = lerp(c, rock, smoothstep(0.35f, 0.6f, slope) * (h < p_.seaLevel ? 0.f : 1.f));  // steep faces show rock
        return c * jitter;
    }

    void buildMesh() {
        const int N = p_.size;
        mesh_.verts.resize(size_t(N) * N);
        mesh_.indices.clear();
        mesh_.indices.reserve(size_t(N - 1) * (N - 1) * 6);
        for (int z = 0; z < N; ++z) for (int x = 0; x < N; ++x) {
            auto H = [&](int cx, int cz) { return h_[idx(std::clamp(cx, 0, N - 1), std::clamp(cz, 0, N - 1))]; };
            float dhdx = (H(x + 1, z) - H(x - 1, z)) / (2.f * p_.cell);
            float dhdz = (H(x, z + 1) - H(x, z - 1)) / (2.f * p_.cell);
            Vertex& v = mesh_.verts[idx(x, z)];
            v.pos = { x * p_.cell, h_[idx(x, z)], z * p_.cell };
            v.normal = normalize(Vec3{ -dhdx, 1.f, -dhdz });
            v.albedo = albedoFor(v.pos.y, 1.f - v.normal.y, x, z);
        }
        for (int z = 0; z < N - 1; ++z) for (int x = 0; x < N - 1; ++x) {
            uint32_t i00 = uint32_t(idx(x, z)), i10 = uint32_t(idx(x + 1, z));
            uint32_t i01 = uint32_t(idx(x, z + 1)), i11 = uint32_t(idx(x + 1, z + 1));
            mesh_.indices.insert(mesh_.indices.end(), { i00, i01, i10, i10, i01, i11 });
        }
    }


    // -- trees: conifers (trunk prism + two stacked cones) scattered on gentle, mid-altitude ground.
    void addCone(Mesh& m, Vec3 base, float radius, float height, Vec3 albedo, int sides) {
        for (int i = 0; i < sides; ++i) {
            float a0 = 6.2831853f * i / sides, a1 = 6.2831853f * (i + 1) / sides, am = (a0 + a1) * 0.5f;
            auto ring = [&](float a) { return Vec3{ base.x + std::cos(a) * radius, base.y, base.z + std::sin(a) * radius }; };
            auto nrm = [&](float a) { return normalize(Vec3{ std::cos(a) * height, radius, std::sin(a) * height }); };
            uint32_t k = uint32_t(m.verts.size());
            m.verts.push_back({ ring(a1), nrm(a1), albedo });
            m.verts.push_back({ ring(a0), nrm(a0), albedo });
            m.verts.push_back({ base + Vec3{ 0, height, 0 }, nrm(am), albedo });
            m.indices.insert(m.indices.end(), { k, k + 1, k + 2 });
        }
    }

    void buildTrees() {
        trees_.verts.clear(); trees_.indices.clear();
        const float spacing = 5.f;
        for (float wz = spacing; wz < extent() - spacing; wz += spacing)
            for (float wx = spacing; wx < extent() - spacing; wx += spacing) {
                int gx = int(wx / spacing), gz = int(wz / spacing);
                float jx = wx + (rnd(gx, gz + 9001) - 0.5f) * spacing * 0.8f;
                float jz = wz + (rnd(gx + 5003, gz) - 0.5f) * spacing * 0.8f;
                float h = heightAt(jx, jz);
                float rel = (h - p_.seaLevel) / std::max(1.f, p_.amplitude - p_.seaLevel);
                // Slope from the local gradient.
                float sx = heightAt(jx + 1.f, jz) - heightAt(jx - 1.f, jz), sz = heightAt(jx, jz + 1.f) - heightAt(jx, jz - 1.f);
                float slope = std::sqrt(sx * sx + sz * sz) * 0.5f;
                if (rel < 0.07f || rel > 0.55f || slope > 0.7f) continue;
                // Clumping: trees only where a low-frequency noise field is high.
                if (fbm(jx * 0.035f + 400.f, jz * 0.035f) < 0.45f) continue;
                float s = 0.7f + 0.8f * rnd(gx + 77, gz + 191);
                float g = 0.85f + 0.3f * rnd(gx + 313, gz + 17);
                Vec3 leaf = Vec3{ 0.10f, 0.30f, 0.12f } * g, bark{ 0.30f, 0.20f, 0.12f };
                Vec3 base{ jx, h - 0.3f, jz };
                addCone(trees_, base, 0.35f * s, 2.0f * s, bark, 4);
                addCone(trees_, base + Vec3{ 0, 1.2f * s, 0 }, 1.9f * s, 3.4f * s, leaf, 7);
                addCone(trees_, base + Vec3{ 0, 3.0f * s, 0 }, 1.3f * s, 3.0f * s, leaf * 1.15f, 7);
            }
    }

    // Sea plane: a coarse grid (not one quad) so per-vertex lighting and fog behave sensibly.
    void buildWater() {
        const int G = 16;
        const float margin = extent() * 0.5f, lo = -margin, hi = extent() + margin;
        water_.verts.clear(); water_.indices.clear();
        for (int z = 0; z <= G; ++z) for (int x = 0; x <= G; ++x) {
            Vertex v;
            v.pos = { lo + (hi - lo) * x / G, p_.seaLevel, lo + (hi - lo) * z / G };
            v.normal = { 0, 1, 0 };
            v.albedo = { 0.06f, 0.22f, 0.36f };
            water_.verts.push_back(v);
        }
        for (int z = 0; z < G; ++z) for (int x = 0; x < G; ++x) {
            uint32_t i00 = z * (G + 1) + x, i10 = i00 + 1, i01 = i00 + (G + 1), i11 = i01 + 1;
            water_.indices.insert(water_.indices.end(), { i00, i01, i10, i10, i01, i11 });
        }
    }
};

}  // namespace acacia
