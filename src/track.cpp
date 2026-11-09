#include "track.h"
#include "pix.h"
#include <algorithm>
#include <limits>
#include <random>

using namespace pix;

// ---------------------------------------------------------------- geometry

Track::Track(const TrackDef& def) : def_(&def) {
    buildCenterline();
    buildJumps();
    paint();
    buildRacingLine();
}

float Track::routeS(float at) const {
    int n = (int)def_->points.size();
    int i = ((int)std::floor(at)) % n;
    float f = at - std::floor(at);
    return lerpf(segStart_[i], segStart_[i + 1], f);
}

void Track::buildCenterline() {
    const auto& p = def_->points;
    int n = (int)p.size();
    const int SUB = 64;
    pts_.clear();
    segStart_.assign(n + 1, 0);
    for (int i = 0; i < n; ++i) {
        Vec2 p0 = p[(i + n - 1) % n], p1 = p[i], p2 = p[(i + 1) % n], p3 = p[(i + 2) % n];
        for (int k = 0; k < SUB; ++k) {
            float t = (float)k / SUB, t2 = t * t, t3 = t2 * t;
            Vec2 v = (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 +
                      (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
            pts_.push_back(v);
        }
    }

    // Shift everything so the route sits inside the world, close to the edge of the table.
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (Vec2 v : pts_) {
        minx = std::min(minx, v.x); miny = std::min(miny, v.y);
        maxx = std::max(maxx, v.x); maxy = std::max(maxy, v.y);
    }
    const float margin = EDGE + 40 + def_->halfWidth;
    shift_ = {margin - minx, margin - miny};
    for (Vec2& v : pts_) v += shift_;
    w_ = (int)(maxx - minx + margin * 2);
    h_ = (int)(maxy - miny + margin * 2);

    // Arc-length resample at a fixed step.
    float total = 0;
    int m = (int)pts_.size();
    std::vector<float> cum(m + 1, 0);
    for (int i = 0; i < m; ++i) {
        total += len(pts_[(i + 1) % m] - pts_[i]);
        cum[i + 1] = total;
    }
    for (int i = 0; i <= n; ++i) segStart_[i] = cum[i * SUB];

    int count = (int)(total / STEP);
    float realStep = total / count;
    samples_.assign(count, Sample{});
    int seg = 0;
    for (int i = 0; i < count; ++i) {
        float s = i * realStep;
        while (seg + 1 < m && cum[seg + 1] < s) ++seg;
        float f = (s - cum[seg]) / std::max(1e-4f, cum[seg + 1] - cum[seg]);
        samples_[i].pos = pts_[seg] + (pts_[(seg + 1) % m] - pts_[seg]) * f;
    }
    // Keep route positions consistent with the sample grid.
    for (float& s : segStart_) s *= STEP / realStep;

    for (int i = 0; i < count; ++i) {
        Vec2 d = samples_[(i + 3) % count].pos - samples_[(i + count - 3) % count].pos;
        samples_[i].tan = norm(d);
        samples_[i].normal = {-samples_[i].tan.y, samples_[i].tan.x};
    }
    for (int i = 0; i < count; ++i) {
        const Vec2 a = samples_[(i + count - 6) % count].tan, b = samples_[(i + 6) % count].tan;
        float da = angleDiff(std::atan2(b.y, b.x), std::atan2(a.y, a.x));
        samples_[i].curvature = da / (12 * STEP);
    }
}

void Track::buildJumps() {
    int n = count();
    respawn_.resize(n);
    for (int i = 0; i < n; ++i) respawn_[i] = i;
    jumps_.clear();
    for (const JumpDef& j : def_->jumps) {
        JumpSpan span;
        span.rampStart = (int)(routeS(j.at) / STEP);
        span.gapStart = span.rampStart + (int)(28.0f / STEP);
        span.gapEnd = span.gapStart + (int)(j.gap / STEP);
        jumps_.push_back(span);
        for (int k = span.rampStart - RUNUP_SAMPLES; k <= span.gapEnd + 4; ++k)
            respawn_[wrap(k)] = wrap(span.rampStart - RUNUP_SAMPLES);
    }
}

void Track::buildRacingLine() {
    const int n = count();
    const float hw = def_->halfWidth;

    // The ideal: cut toward the inside of bends, smoothed so the car turns in and out early.
    std::vector<float> ideal(n), tmp(n);
    for (int i = 0; i < n; ++i) {
        float k = samples_[i].curvature;
        float amt = std::min(std::abs(k) * 90.0f, 0.55f);
        ideal[i] = (k > 0 ? amt : -amt) * hw;
    }
    auto blur = [&](std::vector<float>& a, int R, int passes) {
        for (int pass = 0; pass < passes; ++pass) {
            for (int i = 0; i < n; ++i) {
                float sum = 0;
                for (int j = -R; j <= R; ++j) sum += a[wrap(i + j)];
                tmp[i] = sum / (2 * R + 1);
            }
            a.swap(tmp);
        }
    };
    blur(ideal, 9, 3);

    // Then find the cheapest path across the route, wherever the props happen to be.
    const float LAT = 4.0f, CLEAR = 20.0f;
    const int half = (int)((hw - 14.0f) / LAT);
    const int K = 2 * half + 1;
    auto hazard = [](Surface s) { return s == Surface::Wall || s == Surface::Void || s == Surface::Water; };
    auto ringHits = [&](Vec2 p, float radius) {
        for (int a = 0; a < 8; ++a)
            if (hazard(at(p + fromAngle(TAU * a / 8) * radius))) return true;
        return false;
    };

    std::vector<float> cost((size_t)n * K);
    for (int i = 0; i < n; ++i) {
        const bool jump = inJumpZone(i);
        for (int j = 0; j < K; ++j) {
            float l = (j - half) * LAT;
            Vec2 p = samples_[i].pos + samples_[i].normal * l;
            float c = (l - ideal[i]) * (l - ideal[i]) * 0.004f;
            if (jump) c += l * l * 0.2f;
            if (!jump) {
                if (hazard(at(p)) || ringHits(p, CLEAR * 0.5f) || ringHits(p, CLEAR)) c += 1e6f;
                Surface s = at(p);
                if (s == Surface::Rough || s == Surface::Sticky) c += 6.0f;
                else if (s == Surface::Slick) c += 3.0f;
            }
            cost[(size_t)i * K + j] = c;
        }
    }

    // Run the search twice round the loop, then keep the second lap so the ends join up.
    const int T = 2 * n;
    std::vector<float> best((size_t)K), next((size_t)K);
    std::vector<int8_t> from((size_t)T * K);
    for (int j = 0; j < K; ++j) best[j] = cost[(size_t)j];
    for (int t = 1; t < T; ++t) {
        int i = t % n;
        for (int j = 0; j < K; ++j) {
            float b = std::numeric_limits<float>::max();
            int8_t bd = 0;
            for (int d = -1; d <= 1; ++d) {
                int q = j + d;
                if (q < 0 || q >= K) continue;
                float v = best[q] + (float)(d * d) * 1.0f;
                if (v < b) { b = v; bd = (int8_t)d; }
            }
            next[j] = b + cost[(size_t)i * K + j];
            from[(size_t)t * K + j] = bd;
        }
        best.swap(next);
    }
    int j = (int)(std::min_element(best.begin(), best.end()) - best.begin());
    std::vector<float> line(n);
    for (int t = T - 1; t >= 0; --t) {
        if (t >= n) line[t - n] = (j - half) * LAT;
        j += from[(size_t)t * K + j];
    }
    blur(line, 4, 2);

    float limit = hw - 14.0f;
    blockedLine_ = 0;
    for (int i = 0; i < n; ++i) {
        samples_[i].line = clampf(line[i], -limit, limit);
        Vec2 p = samples_[i].pos + samples_[i].normal * samples_[i].line;
        if (!inJumpZone(i) && (hazard(at(p)) || ringHits(p, 10.0f))) ++blockedLine_;
    }
    for (int i = 0; i < n; ++i) {
        auto point = [&](int k) { const Sample& s = samples_[wrap(k)]; return s.pos + s.normal * s.line; };
        Vec2 a = point(i) - point(i - 6), b = point(i + 6) - point(i);
        samples_[i].lineCurv = angleDiff(std::atan2(b.y, b.x), std::atan2(a.y, a.x)) / (6 * STEP);
    }
}

int Track::nearest(Vec2 p, int around, int window) const {
    int best = wrap(around);
    float bestD = std::numeric_limits<float>::max();
    for (int j = -window; j <= window; ++j) {
        int k = wrap(around + j);
        float d = len2(samples_[k].pos - p);
        if (d < bestD) { bestD = d; best = k; }
    }
    return best;
}

bool Track::inJumpZone(int i) const {
    for (const JumpSpan& s : jumps_) {
        int d = wrap(i - (s.rampStart - RUNUP_SAMPLES));
        if (d <= s.gapEnd + 6 - (s.rampStart - RUNUP_SAMPLES)) return true;
    }
    return false;
}

// ---------------------------------------------------------------- the ground

namespace {

uint32_t woodPlanks(float fx, float fy, float width, uint32_t light, uint32_t dark) {
    float plank = std::floor(fy / width);
    float g = std::sin(fx * 0.02f + 7.0f * fbm(fx * 0.005f + plank * 9, fy * 0.04f)) * 0.5f + 0.5f;
    uint32_t c = mixc(dark, light, g);
    c = shade(c, 0.92f + 0.14f * hash2((int)plank, 11));
    if (std::fmod(fy, width) < 2.5f) c = shade(c, 0.5f);
    return c;
}

uint32_t baseColor(ThemeId t, int x, int y) {
    float fx = (float)x, fy = (float)y;
    switch (t) {
    case ThemeId::Kitchen: {
        const float C = 30;
        int n = ((int)std::floor(fx / C) & 1) + ((int)std::floor(fy / C) & 1);
        uint32_t a = rgb(240, 234, 216), b = rgb(190, 208, 216);
        uint32_t c = n == 0 ? a : (n == 1 ? mixc(a, b, 0.5f) : b);
        if ((x ^ y) & 1) c = shade(c, 0.975f);
        return shade(c, 0.96f + 0.08f * fbm(fx * 0.01f, fy * 0.01f));
    }
    case ThemeId::Workshop: {
        uint32_t c = woodPlanks(fx, fy, 190, rgb(214, 172, 120), rgb(186, 140, 90));
        float stain = fbm(fx * 0.008f + 3, fy * 0.008f);
        if (stain > 0.66f) c = shade(c, 0.86f);
        if (hash2(x / 3, y / 3) > 0.985f) c = shade(c, 0.8f);
        return c;
    }
    case ThemeId::Freezer: {
        uint32_t c = mixc(rgb(200, 222, 236), rgb(236, 246, 250), fbm(fx * 0.02f, fy * 0.02f));
        if (hash2(x, y) > 0.985f) c = rgb(252, 254, 255);
        return c;
    }
    case ThemeId::Sink: {
        float n = fbm(fx * 0.012f, fy * 0.012f);
        uint32_t c = mixc(rgb(84, 138, 162), rgb(122, 174, 194), n);
        float caustic = std::fabs(std::sin(fx * 0.05f + 4 * n) * std::sin(fy * 0.06f + 4 * fbm(fx * 0.01f + 5, fy * 0.01f)));
        if (caustic < 0.06f) c = shade(c, 1.2f);
        return c;
    }
    case ThemeId::Desk: {
        float g = std::sin(fy * 0.05f + 8.0f * fbm(fx * 0.004f, fy * 0.03f)) * 0.5f + 0.5f;
        return shade(mixc(rgb(92, 60, 40), rgb(122, 84, 56), g), 0.96f + 0.08f * hash2(x / 2, y / 2));
    }
    case ThemeId::Sandbox: {
        uint32_t c = mixc(rgb(214, 182, 124), rgb(238, 208, 152), fbm(fx * 0.03f, fy * 0.03f));
        if (hash2(x, y) > 0.96f) c = shade(c, 0.88f);
        return c;
    }
    }
    return rgb(0, 0, 0);
}

// Beyond the table: the floor far below, or the wall of a container.
uint32_t edgeColor(ThemeId t, int x, int y, float e) {
    float fx = (float)x, fy = (float)y, v = e / Track::EDGE;
    uint32_t c;
    switch (t) {
    case ThemeId::Kitchen:
        c = (((x / 64) + (y / 64)) & 1) ? rgb(140, 120, 100) : rgb(190, 176, 150);
        if ((x % 64) < 2 || (y % 64) < 2) c = rgb(100, 90, 76);
        c = shade(c, 0.42f);
        break;
    case ThemeId::Workshop:
        c = shade(mixc(rgb(110, 110, 112), rgb(150, 150, 150), fbm(fx * 0.03f, fy * 0.03f)), 0.4f);
        break;
    case ThemeId::Desk:
        c = shade(mixc(rgb(70, 80, 110), rgb(96, 106, 136), hash2(x / 2, y / 2)), 0.42f);
        break;
    case ThemeId::Sink:
        c = mixc(rgb(150, 158, 168), rgb(212, 218, 226), v);
        c = shade(c, 0.96f + 0.08f * hash2(y / 2, 5));
        return e > Track::EDGE - 8 ? shade(c, 0.7f) : c;
    case ThemeId::Sandbox:
        c = woodPlanks(fy + fx, fx - fy, 34, rgb(178, 132, 84), rgb(136, 96, 58));
        return e > Track::EDGE - 6 ? shade(c, 0.66f) : c;
    case ThemeId::Freezer:
        c = mixc(rgb(216, 226, 234), rgb(246, 250, 252), v);
        return e > Track::EDGE - 6 ? shade(c, 0.72f) : c;
    default:
        c = rgb(0, 0, 0);
        break;
    }
    return e > Track::EDGE - 12 ? shade(c, 0.6f) : c;   // the table's shadow on the floor
}

uint32_t routeColor(ThemeId t, uint32_t c, float d, float along) {
    switch (t) {
    case ThemeId::Sandbox: {
        c = mixc(c, rgb(186, 152, 102), 0.6f);
        if (std::fabs(std::fabs(d) - 20) < 6 && std::fmod(along, 11.0f) < 5.5f) c = shade(c, 0.86f);   // tread marks
        return c;
    }
    case ThemeId::Freezer: return mixc(c, rgb(168, 204, 228), 0.62f);
    default: return c;
    }
}

uint32_t routeLine(ThemeId t, uint32_t c, float d, float hw, int x, int y) {
    float b = std::fabs(d - hw);
    if (t == ThemeId::Workshop && b < 1.8f && fbm(x * 0.3f, y * 0.3f) > 0.34f) return mixc(c, rgb(50, 110, 205), 0.8f);
    if (t == ThemeId::Desk && b < 4.5f) return mixc(c, rgb(246, 234, 90), 0.72f);
    return c;
}

uint32_t rampColor(ThemeId t, float progress, float d, float along) {
    uint32_t base;
    switch (t) {
    case ThemeId::Workshop: base = rgb(226, 196, 140); break;
    case ThemeId::Desk: base = rgb(236, 204, 120); break;
    case ThemeId::Freezer: base = rgb(120, 180, 226); break;
    default: base = rgb(206, 160, 104); break;
    }
    if (std::fmod(std::fabs(d) + 200, 18.0f) < 1.5f) base = shade(base, 0.7f);
    uint32_t c = shade(base, 0.72f + 0.55f * progress);
    if (std::fmod(along + std::fabs(d) * 0.8f, 18.0f) < 6.0f) c = shade(c, 0.78f);
    return c;
}

struct Placed {
    PropKind kind;
    Vec2 pos;
    float rot, scale;
    int seed;
    bool submerged;
};

// Paints a prop's shadow, then the prop, and marks what it does to the ground.
void stamp(std::vector<uint32_t>& pixels, std::vector<Surface>& surf, int W, int H, const Placed& p) {
    const PropInfo& info = propInfo(p.kind);
    const float reach = info.reach * p.scale + 1;
    const float shadow = p.submerged ? 0.0f : info.height * p.scale;
    const float sx = 0.55f, sy = 0.8f;

    const int x0 = std::max(0, (int)std::floor(p.pos.x - reach));
    const int y0 = std::max(0, (int)std::floor(p.pos.y - reach));
    const int x1 = std::min(W - 1, (int)std::ceil(p.pos.x + reach + shadow * sx));
    const int y1 = std::min(H - 1, (int)std::ceil(p.pos.y + reach + shadow * sy));
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w <= 0 || h <= 0) return;

    std::vector<uint8_t> mask((size_t)w * h, 0);
    std::vector<PropPixel> buf((size_t)w * h);
    const float cs = std::cos(p.rot), sn = std::sin(p.rot);
    const Vec2 world{-0.55f, -0.6f};
    const Vec2 light{world.x * cs + world.y * sn, -world.x * sn + world.y * cs};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float dx = x0 + x + 0.5f - p.pos.x, dy = y0 + y + 0.5f - p.pos.y;
            if (dx * dx + dy * dy > reach * reach) continue;
            float u = (dx * cs + dy * sn) / p.scale, v = (-dx * sn + dy * cs) / p.scale;
            PropPixel& px = buf[(size_t)y * w + x];
            if (shadeProp(p.kind, u, v, p.seed, light, px)) mask[(size_t)y * w + x] = 1;
        }
    }

    if (shadow >= 1.0f) {
        const int steps = std::min(24, (int)(shadow / 1.5f) + 1);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                bool dark = false;
                for (int k = 1; k <= steps && !dark; ++k) {
                    int ox = x - (int)std::lround(sx * 1.5f * k), oy = y - (int)std::lround(sy * 1.5f * k);
                    if (ox >= 0 && oy >= 0 && ox < w && oy < h && mask[(size_t)oy * w + ox]) dark = true;
                }
                if (dark) {
                    size_t o = (size_t)(y0 + y) * W + x0 + x;
                    pixels[o] = shade(pixels[o], 0.62f);
                }
            }
        }
    }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!mask[(size_t)y * w + x]) continue;
            const PropPixel& px = buf[(size_t)y * w + x];
            size_t o = (size_t)(y0 + y) * W + x0 + x;
            uint32_t c = px.color;
            float a = px.alpha;
            if (p.submerged) { c = mixc(c, rgb(84, 138, 162), 0.55f); a *= 0.75f; }
            pixels[o] = mixc(pixels[o], c, a);
            if (p.submerged) continue;
            if (px.solid) surf[o] = Surface::Wall;
            else if (px.setSurface && surf[o] != Surface::Ramp) surf[o] = px.surface;
        }
    }
}

}  // namespace

void Track::paint() {
    const ThemeId t = def_->theme;
    const ThemeInfo& th = themeInfo(t);
    const ThemeDressing& dress = themeDressing(t);
    const float hw = def_->halfWidth;
    const size_t area = (size_t)w_ * h_;
    pixels_.resize(area);
    surf_.assign(area, th.off);

    // Distance to the route and nearest sample, for every pixel near it.
    std::vector<float> dist(area, 1e9f);
    std::vector<int> nearestIdx(area, -1);
    const float R = hw + APRON;
    for (int i = 0; i < count(); i += 2) {
        Vec2 c = samples_[i].pos;
        int x0 = std::max(0, (int)(c.x - R)), x1 = std::min(w_ - 1, (int)(c.x + R));
        int y0 = std::max(0, (int)(c.y - R)), y1 = std::min(h_ - 1, (int)(c.y + R));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                float dx = x + 0.5f - c.x, dy = y + 0.5f - c.y;
                float d2 = dx * dx + dy * dy;
                size_t o = (size_t)y * w_ + x;
                if (d2 <= R * R && d2 < dist[o]) { dist[o] = d2; nearestIdx[o] = i; }
            }
        }
    }

    std::vector<uint8_t> tag(count(), 0);
    for (const JumpSpan& s : jumps_) {
        for (int j = s.rampStart; j < s.gapStart; ++j) tag[wrap(j)] = 1;
        for (int j = s.gapStart; j < s.gapEnd; ++j) tag[wrap(j)] = 2;
    }

    // The bare surface: table, route markings, ramps and gaps.
    for (int y = 0; y < h_; ++y) {
        for (int x = 0; x < w_; ++x) {
            size_t o = (size_t)y * w_ + x;
            float e = (float)std::min(std::min(x, y), std::min(w_ - 1 - x, h_ - 1 - y));
            if (e < EDGE) {
                pixels_[o] = edgeColor(t, x, y, e);
                surf_[o] = th.edge;
                continue;
            }
            uint32_t col = baseColor(t, x, y);
            Surface sf = th.off;
            if (e < EDGE + 6) col = shade(col, 0.72f);

            if (nearestIdx[o] >= 0) {
                float d = std::sqrt(dist[o]);
                int idx = nearestIdx[o];
                const Sample& sp = samples_[idx];
                Vec2 rel = Vec2{x + 0.5f, y + 0.5f} - sp.pos;
                float signedD = dot(rel, sp.normal);
                float along = idx * STEP;
                if (d <= hw) {
                    sf = Surface::Road;
                    col = routeColor(t, col, signedD, along);
                    if (along < 10.0f) {
                        bool check = (((int)(along / 5.0f) + (int)((signedD + 200) / 5.0f)) & 1) != 0;
                        col = check ? rgb(245, 245, 245) : rgb(24, 24, 24);
                    }
                    if (tag[idx] == 1) {
                        sf = Surface::Ramp;
                        int rampStart = idx;
                        while (tag[wrap(rampStart - 1)] == 1) --rampStart;
                        col = rampColor(t, (float)(idx - rampStart) / 7.0f, signedD, along);
                    }
                }
                col = routeLine(t, col, d, hw, x, y);
                bool isGap = tag[idx] == 2 || (idx + 1 < count() && tag[idx + 1] == 2 && dot(rel, sp.tan) > 0);
                if (isGap) {
                    sf = th.gap;
                    float depth = 0.5f + 0.5f * hash2(x / 3, y / 3);
                    col = th.gap == Surface::Water ? mixc(rgb(30, 78, 120), rgb(52, 110, 148), depth)
                                                   : mixc(rgb(10, 10, 16), rgb(30, 28, 34), depth);
                }
            }
            pixels_[o] = col;
            surf_[o] = sf;
        }
    }

    // Lay out everything that sits on the surface.
    std::vector<Placed> placed;
    const float cellSize = 10.0f;
    const int gw = w_ / (int)cellSize + 2, gh = h_ / (int)cellSize + 2;
    std::vector<uint8_t> occupiedSolid((size_t)gw * gh, 0), occupiedFlat((size_t)gw * gh, 0);

    auto capsuleCells = [&](Vec2 c, Vec2 axis, float halfLen, float radius, auto&& visit) {
        int steps = std::max(1, (int)(halfLen / 8.0f));
        for (int i = -steps; i <= steps; ++i) {
            Vec2 p = c + axis * (halfLen * (float)i / steps);
            int cx0 = std::max(0, (int)((p.x - radius) / cellSize)), cx1 = std::min(gw - 1, (int)((p.x + radius) / cellSize));
            int cy0 = std::max(0, (int)((p.y - radius) / cellSize)), cy1 = std::min(gh - 1, (int)((p.y + radius) / cellSize));
            for (int y = cy0; y <= cy1; ++y)
                for (int x = cx0; x <= cx1; ++x)
                    if (visit((size_t)y * gw + x)) return true;
        }
        return false;
    };
    auto occupy = [&](const Placed& p) {
        const PropInfo& info = propInfo(p.kind);
        auto& grid = info.solid ? occupiedSolid : occupiedFlat;
        capsuleCells(p.pos, fromAngle(p.rot), info.halfLen * p.scale, info.halfWid * p.scale + 4, [&](size_t c) { grid[c] = 1; return false; });
    };

    for (const PropPlacement& pl : def_->props) {
        Placed p{pl.kind, {}, pl.rotDeg * PI / 180.0f, pl.scale, pl.seed, pl.submerged};
        if (pl.routeRelative) {
            const Sample& s = samples_[wrap((int)(routeS(pl.a) / STEP))];
            p.pos = s.pos + s.normal * pl.b;
            p.rot += std::atan2(s.tan.y, s.tan.x);
        } else {
            p.pos = Vec2{pl.a, pl.b} + shift_;
        }
        placed.push_back(p);
        occupy(p);
    }

    // Fill the rest of the table with clutter, keeping it off the route.
    std::mt19937 rng((uint32_t)def_->seed);
    int totalWeight = 0;
    for (const ScatterEntry& e : dress.scatter) totalWeight += e.weight;
    auto rnd = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
    for (int attempt = 0; attempt < dress.attempts; ++attempt) {
        int pick = (int)(rng() % (uint32_t)totalWeight);
        const ScatterEntry* entry = &dress.scatter[0];
        for (const ScatterEntry& e : dress.scatter) {
            if (pick < e.weight) { entry = &e; break; }
            pick -= e.weight;
        }
        const PropInfo& info = propInfo(entry->kind);
        float scale = rnd(entry->minScale, entry->maxScale);
        Placed p{entry->kind, {rnd(EDGE, w_ - EDGE), rnd(EDGE, h_ - EDGE)}, rnd(0, TAU), scale, (int)(rng() & 0xffff), entry->submerged};
        const Vec2 axis = fromAngle(p.rot);
        const float halfLen = info.halfLen * scale, halfWid = info.halfWid * scale;
        const bool tiny = info.reach <= 44 && !info.solid;

        bool ok = true;
        for (float f : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            Vec2 q = p.pos + axis * (halfLen * f);
            int qx = (int)q.x, qy = (int)q.y;
            if (qx < 0 || qy < 0 || qx >= w_ || qy >= h_) { ok = false; break; }
            float e = (float)std::min(std::min(qx, qy), std::min(w_ - 1 - qx, h_ - 1 - qy));
            if (e < EDGE + halfWid + 8) { ok = false; break; }
            size_t o = (size_t)qy * w_ + qx;
            if (nearestIdx[o] >= 0) {
                if (inJumpZone(nearestIdx[o])) { ok = false; break; }
                if (!tiny && !p.submerged && std::sqrt(dist[o]) < hw + halfWid + 8) { ok = false; break; }
            }
        }
        if (!ok) continue;
        auto& grid = info.solid ? occupiedSolid : occupiedFlat;
        if (capsuleCells(p.pos, axis, halfLen, halfWid + 6, [&](size_t c) { return grid[c] != 0; })) continue;
        placed.push_back(p);
        occupy(p);
    }

    // The trail of small pieces that shows where the route runs.
    std::vector<Placed> border;
    if (dress.hasBorderPiece) {
        int spacing = std::max(1, (int)(dress.borderSpacing / STEP));
        for (int i = 8; i < count() - 4; i += spacing) {
            if (inJumpZone(i)) continue;
            const Sample& s = samples_[i];
            for (float side : {-1.0f, 1.0f}) {
                Vec2 pos = s.pos + s.normal * (side * hw + rnd(-3, 3)) + s.tan * rnd(-2, 2);
                border.push_back({dress.borderPiece, pos, rnd(0, TAU), rnd(0.9f, 1.15f), (int)(rng() & 0xffff), false});
            }
        }
    }

    for (const Placed& p : border) stamp(pixels_, surf_, w_, h_, p);
    std::stable_partition(placed.begin(), placed.end(), [](const Placed& p) { return !propInfo(p.kind).solid; });
    for (const Placed& p : placed) stamp(pixels_, surf_, w_, h_, p);
}
