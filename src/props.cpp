#include "props.h"
#include "pix.h"

using namespace pix;

const PropInfo& propInfo(PropKind k) {
    static const PropInfo table[] = {
#define X(name, reach, halfLen, halfWid, height, solid) {reach, halfLen, halfWid, height, solid},
        PROP_LIST(X)
#undef X
    };
    return table[(int)k];
}

namespace {
const float LZ = 0.58f;

uint32_t lit(uint32_t c, float nx, float ny, float nz, Vec2 l, float spec, float amb = 0.5f) {
    float d = std::max(0.0f, nx * l.x + ny * l.y + nz * LZ);
    uint32_t out = shade(c, amb + (1.0f - amb) * d / LZ);
    if (spec > 0) out = mixc(out, rgb(255, 255, 255), std::pow(d, 20.0f) * spec);
    return out;
}

uint32_t sphere(float u, float v, float r, uint32_t c, Vec2 l, float spec = 0.4f) {
    float nx = u / r, ny = v / r, q = 1 - nx * nx - ny * ny;
    return lit(c, nx, ny, q > 0 ? std::sqrt(q) : 0.0f, l, spec);
}

// A cylinder lying along the u axis.
uint32_t rod(float v, float r, uint32_t c, Vec2 l, float spec = 0.35f) {
    float ny = clampf(v / r, -1, 1);
    return lit(c, 0, ny, std::sqrt(std::max(0.0f, 1 - ny * ny)), l, spec);
}

// Brightens the side of a round thing facing the light; a negative k suits a hollow.
uint32_t radial(uint32_t c, float u, float v, float r, Vec2 l, float k) {
    return shade(c, 1.0f + k * (u * l.x + v * l.y) / std::max(r, 1.0f));
}

// Darkens a shape towards its outline, so flat objects read as having thickness.
uint32_t edged(uint32_t c, float d, float width = 2.5f, float amount = 0.78f) {
    return d > -width ? shade(c, amount) : c;
}

int cell(float a, float size) { return (int)std::floor(a / size); }

float blobDist(float u, float v, float R, int seed) {
    float a = std::atan2(v, u);
    float n = fbm(std::cos(a) * 1.6f + seed * 3.1f + 20, std::sin(a) * 1.6f + seed * 1.7f + 20);
    return std::sqrt(u * u + v * v) - R * (0.62f + 0.72f * n);
}

const uint32_t STEEL = 0xFFB0B8C4u, STEEL_DARK = 0xFF6E7682u, BRASS = 0xFFC8A040u;
const uint32_t WOOD = 0xFFA87642u, WOOD_DARK = 0xFF6E4826u, WOOD_LIGHT = 0xFFD6B078u;
const uint32_t CERAMIC = 0xFFF6F4ECu, BLACK = 0xFF1C1C20u;

uint32_t metal(float u, float v) { return mixc(STEEL, shade(STEEL, 0.82f), fbm(u * 0.03f, v * 0.5f)); }

uint32_t grain(uint32_t c, float u, float v) {
    float g = std::sin(v * 0.55f + 5.0f * fbm(u * 0.02f, v * 0.12f));
    return shade(c, 0.93f + 0.09f * g);
}

// Shared look for spills: a wet blob with a bright meniscus.
bool spill(float u, float v, int seed, uint32_t c, float alpha, Surface s, PropPixel& o) {
    float d = blobDist(u, v, 52, seed);
    if (d > 0) return false;
    o.color = d > -2.5f ? shade(c, 1.25f) : mixc(c, shade(c, 1.15f), fbm(u * 0.06f, v * 0.06f));
    o.alpha = alpha;
    o.setSurface = true;
    o.surface = s;
    return true;
}

bool shadeKitchen(PropKind k, float u, float v, float r, int seed, Vec2 l, PropPixel& o) {
    using K = PropKind;
    switch (k) {
    case K::Plate:
    case K::BreakfastPlate: {
        if (r > 84) return false;
        static const uint32_t bands[3] = {rgb(70, 110, 180), rgb(60, 140, 110), rgb(190, 80, 70)};
        uint32_t c = CERAMIC;
        if (r > 80) c = radial(c, u, v, r, l, 0.28f);
        else if (r > 60) c = radial(c, u, v, r, l, -0.18f);
        else if (r > 54) c = radial(c, u, v, r, l, 0.16f);
        else c = shade(c, 0.97f);
        if (r > 69 && r < 73) c = bands[seed % 3];
        o.color = c;
        if (k == K::BreakfastPlate && r < 56) {
            float du = u + 14, dv = v + 8, dr = std::sqrt(du * du + dv * dv);
            if (blobDist(du, dv, 30, seed) < 0) o.color = rgb(250, 248, 240);
            if (dr < 13) o.color = sphere(du, dv, 13, rgb(246, 176, 34), l, 0.6f);
            if (v > 16 && v < 44 && std::fabs(u - 8) < 34) {
                float w = std::sin(u * 0.28f) * 2.5f;
                if (std::fabs(v - 24 - w) < 4 || std::fabs(v - 36 - w) < 4)
                    o.color = mixc(rgb(170, 62, 44), rgb(226, 150, 120), fbm(u * 0.3f, v * 0.3f));
            }
        }
        return true;
    }
    case K::Bowl: {
        if (r > 58) return false;
        if (r > 52) { o.color = radial(CERAMIC, u, v, r, l, 0.28f); return true; }
        uint32_t c = radial(rgb(246, 238, 216), u, v, r, l, -0.14f);
        int cx = cell(u, 15), cy = cell(v, 15);
        float px = (cx + 0.5f) * 15 + (hash2(cx, cy) - 0.5f) * 6, py = (cy + 0.5f) * 15 + (hash2(cy, cx) - 0.5f) * 6;
        float d = std::sqrt((u - px) * (u - px) + (v - py) * (v - py));
        if (hash2(cx + 5, cy + 3) > 0.25f && d < 6 && d > 2.4f) c = mixc(rgb(198, 142, 70), rgb(226, 178, 100), hash2(cx, cy + 9));
        o.color = c;
        return true;
    }
    case K::Mug: {
        static const uint32_t colors[4] = {rgb(206, 64, 58), rgb(58, 110, 190), rgb(240, 236, 226), rgb(70, 150, 110)};
        uint32_t body = colors[seed % 4];
        if (r <= 28) {
            if (r > 24) { o.color = radial(body, u, v, r, l, 0.3f); return true; }
            uint32_t coffee = rgb(72, 44, 26);
            float nd = (u * l.x + v * l.y) / 24;
            o.color = r > 22 ? shade(coffee, 0.65f) : mixc(coffee, rgb(150, 110, 76), clampf(-nd * 0.6f, 0, 0.4f));
            return true;
        }
        float d = sdBox(u - 37, v, 9, 9, 6);
        if (u > 20 && std::fabs(d) < 3.2f) { o.color = shade(body, 1.0f + 0.2f * (d / 3.2f)); return true; }
        return false;
    }
    case K::Glass: {
        if (r > 24) return false;
        uint32_t juice = (seed & 1) ? rgb(240, 150, 40) : rgb(190, 220, 240);
        if (r > 21.5f) { o.color = radial(rgb(226, 240, 250), u, v, r, l, 0.25f); return true; }
        o.color = r > 19 ? shade(juice, 0.7f) : mixc(juice, rgb(255, 255, 255), clampf(-(u * l.x + v * l.y) / 40.0f, 0, 0.4f));
        return true;
    }
    case K::BottleStand: {
        if (r > 26) return false;
        uint32_t glass = (seed & 1) ? rgb(120, 76, 36) : rgb(44, 110, 66);
        o.color = r < 9 ? radial(BRASS, u, v, r, l, 0.4f) : (r < 11 ? shade(glass, 0.5f) : sphere(u, v, 27, glass, l, 0.8f));
        return true;
    }
    case K::BottleLying: {
        float R;
        if (u < -78 || u > 96) return false;
        if (u < -70) R = 24 * std::sqrt(std::max(0.0f, 1 - ((u + 70) / 8) * ((u + 70) / 8)));
        else if (u < 28) R = 24;
        else if (u < 52) R = lerpf(24, 9.5f, (u - 28) / 24);
        else if (u < 88) R = 9.5f;
        else R = 11;
        if (std::fabs(v) > R) return false;
        static const uint32_t glass[3] = {rgb(44, 110, 66), rgb(120, 76, 36), rgb(60, 110, 180)};
        uint32_t c = glass[seed % 3];
        if (u > -60 && u < 8) {
            c = rgb(236, 226, 196);
            if (u > -48 && u < -4 && std::fabs(v) < 10) c = rgb(186, 60, 50);
            if (u > -40 && u < -12 && std::fabs(v) < 3) c = rgb(240, 220, 170);
        }
        if (u >= 88) c = BRASS;
        o.color = rod(v, R, c, l, 0.7f);
        return true;
    }
    case K::Fork: {
        if (u > 42 && u <= 76) {
            for (float t : {-10.5f, -3.5f, 3.5f, 10.5f})
                if (std::fabs(v - t) < 2.4f) { o.color = rod(v - t, 2.4f, metal(u, v), l); return true; }
            return false;
        }
        if (u > 16 && u <= 42) {
            float hw = lerpf(5.0f, 14.0f, clampf((u - 16) / 12, 0, 1));
            if (std::fabs(v) > hw) return false;
            o.color = metal(u, v);
            return true;
        }
        if (u < -76 || u > 16) return false;
        float hw = 4.5f + (u < -40 ? (-40 - u) * 0.08f : 0);
        if (std::fabs(v) > hw) return false;
        o.color = rod(v, hw, metal(u, v), l, 0.5f);
        return true;
    }
    case K::Knife: {
        if (u < -72 || u > 74) return false;
        if (u < -8) {
            if (std::fabs(v) > 7) return false;
            uint32_t c = grain(rgb(70, 44, 28), u, v);
            if (std::fabs(u + 56) < 2.5f || std::fabs(u + 24) < 2.5f) c = BRASS;
            o.color = rod(v, 7, c, l, 0.3f);
            return true;
        }
        float bottom = 8;
        if (u > 42) { float t = (u - 42) / 32; bottom = 8 - 16 * t * t; }
        if (v < -8 || v > bottom) return false;
        o.color = v > bottom - 3 ? rgb(226, 232, 240) : metal(u, v);
        return true;
    }
    case K::Spoon: {
        float eu = (u - 44) / 26, ev = v / 17;
        float e = eu * eu + ev * ev;
        if (e <= 1) { o.color = radial(metal(u, v), eu, ev, std::sqrt(e), l, -0.35f); return true; }
        if (u < -72 || u > 24 || std::fabs(v) > 4.5f) return false;
        o.color = rod(v, 4.5f, metal(u, v), l, 0.5f);
        return true;
    }
    case K::Napkin: {
        if (sdBox(u, v, 52, 52) > 0) return false;
        static const uint32_t colors[3] = {rgb(238, 214, 150), rgb(204, 220, 186), rgb(232, 190, 180)};
        uint32_t c = colors[seed % 3];
        if (std::fabs(u) < 1.2f || std::fabs(v) < 1.2f) c = shade(c, 0.86f);
        if (u + v > 0) c = shade(c, 0.95f);
        o.color = edged(c, sdBox(u, v, 52, 52), 1.5f, 0.88f);
        return true;
    }
    case K::Apple: {
        if (sdSeg(u, v, 0, 0, 9, -8, 1.8f) < 0) { o.color = rgb(90, 60, 30); return true; }
        float lu = (u - 15) / 10, lv = (v + 13) / 5;
        if (lu * lu + lv * lv < 1) { o.color = rgb(80, 150, 60); return true; }
        if (r > 26) return false;
        o.color = sphere(u, v, 26, (seed & 1) ? rgb(204, 44, 40) : rgb(150, 190, 60), l, 0.55f);
        if (r < 5) o.color = shade(o.color, 0.65f);
        return true;
    }
    case K::Orange: {
        if (r > 25) return false;
        o.color = shade(sphere(u, v, 25, rgb(240, 140, 30), l, 0.4f), 0.95f + 0.1f * hash2(cell(u, 2), cell(v, 2)));
        if (std::fabs(u) < 2 && std::fabs(v) < 2) o.color = rgb(90, 110, 50);
        return true;
    }
    case K::Banana: {
        const float R = 85, limit = 0.85f;
        float dv = v + R, th = std::atan2(u, dv);
        if (std::fabs(th) > limit) return false;
        float d = std::sqrt(u * u + dv * dv) - R;
        float t = th / limit, hw = 12 * (1 - t * t * t * t * 0.6f);
        if (std::fabs(d) > hw) return false;
        uint32_t c = std::fabs(th) > limit * 0.92f ? rgb(90, 70, 36) : rgb(240, 208, 70);
        if (std::fabs(d - 3) < 0.8f) c = shade(c, 0.82f);
        o.color = rod(d, hw, c, l, 0.25f);
        return true;
    }
    case K::SaltShaker: {
        if (r > 14) return false;
        o.color = r > 11 ? radial(rgb(232, 236, 240), u, v, r, l, 0.3f) : radial(STEEL, u, v, r, l, 0.25f);
        for (int i = 0; i < 5; ++i) {
            float a = TAU * i / 5;
            float du = u - std::cos(a) * 6, dv = v - std::sin(a) * 6;
            if (du * du + dv * dv < 2.5f) o.color = BLACK;
        }
        return true;
    }
    case K::PepperMill: {
        if (r > 20) return false;
        o.color = r < 8 ? sphere(u, v, 8, WOOD_DARK, l, 0.4f) : radial(grain(WOOD_DARK, u, v), u, v, r, l, 0.35f);
        return true;
    }
    case K::ButterDish: {
        float d = sdBox(u, v, 56, 36, 10);
        if (d > 0) return false;
        o.color = edged(CERAMIC, d);
        float b = sdBox(u, v, 40, 22, 2);
        if (b < 0) {
            o.color = edged(rgb(244, 224, 130), b, 1.5f, 0.88f);
            if (std::fabs(std::sin(u * 0.35f) * 4 - v) < 1.2f) o.color = rgb(226, 200, 100);
        }
        return true;
    }
    case K::Toast: {
        float d = sdBox(u, v, 34, 34, 9);
        if (d > 0) return false;
        o.color = d > -5 ? rgb(150, 96, 46) : mixc(rgb(214, 160, 92), rgb(244, 208, 140), fbm(u * 0.2f, v * 0.2f));
        return true;
    }
    case K::JamJar: {
        if (r > 28) return false;
        if (r > 25) { o.color = radial(BRASS, u, v, r, l, 0.3f); return true; }
        o.color = ((cell(u, 6) + cell(v, 6)) & 1) ? rgb(206, 60, 60) : rgb(245, 240, 232);
        return true;
    }
    case K::EggCup: {
        if (r > 20) return false;
        o.color = r > 14 ? radial(rgb(90, 140, 200), u, v, r, l, 0.3f) : sphere(u, v, 14.5f, rgb(240, 226, 196), l, 0.3f);
        return true;
    }
    case K::Teapot: {
        static const uint32_t colors[3] = {rgb(50, 120, 130), rgb(200, 90, 70), rgb(236, 230, 214)};
        uint32_t c = colors[seed % 3];
        if (r <= 56) {
            o.color = sphere(u, v, 58, c, l, 0.6f);
            if (r < 26 && r > 23.5f) o.color = shade(c, 0.6f);
            if (r < 8) o.color = sphere(u, v, 8, c, l, 0.8f);
            return true;
        }
        if (u > 40 && u < 98) {
            float hw = lerpf(15, 7, (u - 40) / 58);
            if (std::fabs(v) <= hw) { o.color = rod(v, hw, c, l, 0.5f); return true; }
        }
        float d = sdBox(u + 70, v, 20, 26, 16);
        if (u < -40 && std::fabs(d) < 5) { o.color = shade(c, 1.0f + 0.14f * (d / 5)); return true; }
        return false;
    }
    case K::CerealBox: {
        float d = sdBox(u, v, 90, 60, 2);
        if (d > 0) return false;
        float du = u + 10, dv = v - 30, a = std::atan2(dv, du);
        uint32_t c = std::fmod(a + TAU, 0.5f) < 0.25f ? rgb(246, 190, 50) : rgb(232, 90, 50);
        float dr = std::sqrt(du * du + dv * dv);
        if (dr < 38 && dv > -6) c = CERAMIC;
        if (dr < 30 && dr > 2 && dv > 0 && dv < 6) c = rgb(198, 142, 70);
        if (v < -30 && v > -50 && std::fabs(u) < 74) {
            c = rgb(40, 70, 150);
            if (std::fmod(u + 200, 17.0f) < 11 && std::fabs(v + 40) < 6) c = rgb(245, 245, 245);
        }
        o.color = edged(c, d, 3.0f, 0.72f);
        return true;
    }
    case K::JuiceSpill:  return spill(u, v, seed, rgb(244, 160, 40), 0.82f, Surface::Slick, o);
    case K::MilkSpill:   return spill(u, v, seed, rgb(248, 246, 238), 0.9f, Surface::Slick, o);
    case K::JamSpill:    return spill(u, v, seed, rgb(160, 26, 46), 0.92f, Surface::Sticky, o);
    case K::CoffeeSpill: return spill(u, v, seed, rgb(70, 42, 24), 0.85f, Surface::Slick, o);
    case K::Crumbs: {
        if (r > 40) return false;
        int cx = cell(u, 6), cy = cell(v, 6);
        if (hash2(cx + seed, cy) < 0.72f + r * 0.005f) return false;
        float du = u - (cx + 0.5f) * 6, dv = v - (cy + 0.5f) * 6;
        if (du * du + dv * dv > 5) return false;
        o.color = mixc(rgb(196, 142, 80), rgb(236, 200, 140), hash2(cx, cy + 2));
        return true;
    }
    case K::CuttingBoard: {
        float d = sdBox(u, v, 170, 100, 14);
        if (d > 0) return false;
        float hole = std::sqrt((u + 150) * (u + 150) + v * v);
        if (hole < 11) return false;
        uint32_t c = grain(rgb(190, 142, 88), u * 0.6f, v * 2.2f);
        if (std::fabs(d + 12) < 2) c = shade(c, 0.8f);
        o.color = edged(c, d, 2.5f, 0.72f);
        return true;
    }
    case K::Placemat: {
        float d = sdBox(u, v, 140, 95, 4);
        if (d > 0) return false;
        static const uint32_t colors[3] = {rgb(150, 70, 56), rgb(66, 104, 130), rgb(96, 124, 84)};
        uint32_t c = colors[seed % 3];
        if (cell(v, 18) & 1) c = shade(c, 1.15f);
        if ((cell(u, 3) + cell(v, 3)) & 1) c = shade(c, 0.94f);
        o.color = edged(c, d, 5.0f, 0.8f);
        return true;
    }
    case K::CerealRing: {
        if (r > 5 || r < 2) return false;
        o.color = radial(mixc(rgb(198, 142, 70), rgb(232, 186, 104), (float)(seed % 7) / 7), u, v, r, l, 0.35f);
        return true;
    }
    default: return false;
    }
}

bool shadeWorkshop(PropKind k, float u, float v, float r, int seed, Vec2 l, PropPixel& o) {
    using K = PropKind;
    switch (k) {
    case K::Hammer: {
        float lu = u - 56;
        if (std::fabs(lu) < 14 && v > -38 && v < 38) {
            if (v < -6) {
                float t = (-6 - v) / 32, hw = 14 * (1 - 0.55f * t);
                if (std::fabs(lu) > hw || std::fabs(lu) < 1.2f + t * 4) return false;
            }
            o.color = v > 28 ? rgb(214, 220, 228) : rod(lu, 14, STEEL, l, 0.5f);
            return true;
        }
        if (u < -110 || u > 44 || std::fabs(v) > 8) return false;
        o.color = rod(v, 8, u < -50 ? rgb(40, 40, 44) : grain(rgb(200, 160, 106), u, v), l, 0.3f);
        return true;
    }
    case K::HandSaw: {
        if (u < -100) {
            float d = sdBox(u + 136, v, 34, 44, 12);
            if (d > 0 || sdBox(u + 142, v, 18, 26, 9) < 0) return false;
            o.color = edged(grain(rgb(150, 90, 46), v, u), d);
            return true;
        }
        if (u > 190) return false;
        float bottom = lerpf(46, 16, (u + 100) / 290) + std::fmod(u + 1000, 9.0f) / 9.0f * 4;
        if (v < -38 || v > bottom) return false;
        uint32_t c = mixc(rgb(196, 204, 214), rgb(150, 158, 170), fbm(u * 0.02f, v * 0.2f));
        if (v > bottom - 6) c = rgb(120, 126, 136);
        if (v < -34) c = shade(c, 1.12f);
        o.color = c;
        return true;
    }
    case K::Chisel: {
        if (u < -100 || u > 100) return false;
        if (u < -10) {
            float hw = 13 - (u < -88 ? (-88 - u) * 0.5f : 0);
            if (std::fabs(v) > hw) return false;
            o.color = rod(v, hw, grain(rgb(196, 140, 74), u, v), l, 0.3f);
            return true;
        }
        if (u < 0) { if (std::fabs(v) > 10) return false; o.color = rod(v, 10, BRASS, l, 0.5f); return true; }
        if (std::fabs(v) > 11) return false;
        o.color = u > 82 ? rgb(226, 232, 240) : metal(u, v);
        return true;
    }
    case K::Screwdriver: {
        static const uint32_t colors[3] = {rgb(200, 50, 44), rgb(40, 110, 190), rgb(230, 170, 40)};
        if (u < -72 || u > 85) return false;
        if (u < 5) {
            if (std::fabs(v) > 14) return false;
            uint32_t c = colors[seed % 3];
            if (std::fabs(std::fmod(v + 100, 7.0f) - 3.5f) < 1) c = shade(c, 0.75f);
            o.color = rod(v, 14, c, l, 0.45f);
            return true;
        }
        if (std::fabs(v) > 4.5f) return false;
        o.color = rod(v, 4.5f, STEEL, l, 0.6f);
        return true;
    }
    case K::Plane: {
        float d = sdBox(u, v, 110, 30, 6);
        if (d > 0) return false;
        uint32_t c = edged(rgb(120, 128, 140), d, 3.5f, 0.74f);
        if (sdBox(u + 60, v, 26, 10, 8) < 0) c = rod(v, 10, grain(rgb(120, 62, 40), u, v), l, 0.3f);
        else if (sdBox(u - 5, v, 26, 18, 3) < 0) c = radial(BRASS, u - 5, v, 26, l, 0.25f);
        float du = u - 72;
        if (du * du + v * v < 169) c = sphere(du, v, 13, rgb(120, 62, 40), l, 0.4f);
        o.color = c;
        return true;
    }
    case K::Clamp: {
        float outer = sdBox(u, v, 70, 52, 14), inner = sdBox(u - 26, v, 62, 32, 6);
        if (std::fabs(u - 40) < 4.5f && v > -10 && v < 52 && inner < 0) { o.color = rod(u - 40, 4.5f, STEEL, l, 0.6f); return true; }
        if (std::fabs(v + 10) < 3 && std::fabs(u - 40) < 12 && inner < 0) { o.color = STEEL_DARK; return true; }
        if (outer > 0 || inner < 0) return false;
        o.color = edged(rgb(50, 84, 130), std::max(outer, -inner), 3.0f, 0.72f);
        return true;
    }
    case K::TapeMeasure: {
        float d = sdBox(u, v, 38, 38, 10);
        if (d < 0) {
            o.color = edged(rgb(244, 196, 40), d, 3.0f, 0.74f);
            if (r < 18) o.color = r < 15 ? radial(BLACK, u, v, r, l, 0.5f) : STEEL;
            return true;
        }
        if (u < 38 || u > 190 || std::fabs(v) > 6) return false;
        o.solid = false;
        o.color = std::fmod(u, 12.0f) < 1.2f ? BLACK : rgb(246, 214, 90);
        if (u > 186) o.color = STEEL;
        return true;
    }
    case K::TrySquare: {
        float s = sdBox(u + 100, v, 14, 60, 2);
        if (s < 0) {
            uint32_t c = edged(grain(rgb(96, 54, 36), v, u), s);
            float rr = std::fabs(v) - 30;
            if ((u + 100) * (u + 100) + rr * rr < 16) c = BRASS;
            o.color = c;
            return true;
        }
        if (u < -86 || u > 120 || v < -60 || v > -38) return false;
        o.color = std::fmod(u + 200, 12.0f) < 1.2f && v > -44 ? BLACK : metal(u, v);
        return true;
    }
    case K::PaintCan: {
        if (r > 54) return false;
        static const uint32_t paints[4] = {rgb(196, 60, 50), rgb(50, 120, 190), rgb(240, 200, 60), rgb(90, 170, 110)};
        uint32_t p = paints[seed % 4];
        if (r > 49) { o.color = radial(STEEL, u, v, r, l, 0.3f); return true; }
        if (r > 46) { o.color = STEEL_DARK; return true; }
        // A steel lid, with paint dried around its rim and a run down one side.
        float a = std::atan2(v, u);
        float smear = 36 + 9 * (fbm(std::cos(a) * 2 + seed, std::sin(a) * 2) - 0.5f) * 2;
        bool run = std::fabs(angleDiff(a, (float)seed)) < 0.28f && r > 16;
        o.color = (r > smear || run) ? radial(p, u, v, r, l, 0.12f) : radial(mixc(STEEL, shade(STEEL, 0.86f), fbm(u * 0.04f, v * 0.04f)), u, v, r, l, 0.08f);
        if (r < 3) o.color = STEEL_DARK;
        return true;
    }
    case K::WoodBlock:
    case K::Plank: {
        float hu = k == K::Plank ? 170.0f : 58.0f, hv = k == K::Plank ? 26.0f : 28.0f;
        float d = sdBox(u, v, hu, hv, 2);
        if (d > 0) return false;
        uint32_t base = (seed & 1) ? rgb(214, 170, 112) : rgb(186, 130, 78);
        uint32_t c = grain(base, u, v);
        if (std::fabs(u) > hu - 5) c = shade(c, 0.78f);
        o.color = edged(c, d, 2.5f, 0.76f);
        return true;
    }
    case K::PaintBrush: {
        if (u < -90 || u > 62) return false;
        if (u < 0) { if (std::fabs(v) > 8) return false; o.color = rod(v, 8, rgb(190, 60, 48), l, 0.4f); return true; }
        if (u < 18) { if (std::fabs(v) > 12) return false; o.color = rod(v, 12, STEEL, l, 0.6f); return true; }
        float hw = lerpf(18, 12, (u - 18) / 44);
        if (std::fabs(v) > hw) return false;
        o.color = shade(rgb(60, 110, 180), 0.8f + 0.3f * hash2(0, cell(v, 2)));
        return true;
    }
    case K::Drill: {
        if (u > 40 && u < 118 && std::fabs(v + 10) < 4) { o.color = rod(v + 10, 4, STEEL, l, 0.6f); return true; }
        if (u > 22 && u <= 42 && std::fabs(v + 10) < 13) { o.color = rod(v + 10, 13, BLACK, l, 0.5f); return true; }
        float grip = sdBox(u + 34, v - 34, 18, 34, 6);
        if (grip < 0) { o.color = edged(rgb(40, 40, 44), grip); return true; }
        float body = sdBox(u + 22, v + 10, 46, 21, 10);
        if (body > 0) return false;
        o.color = rod(v + 10, 21, rgb(230, 120, 40), l, 0.4f);
        return true;
    }
    case K::Sandpaper: {
        float d = sdBox(u, v, 65, 45);
        if (d > 0) return false;
        o.color = mixc(rgb(140, 84, 56), rgb(192, 136, 96), hash2((int)u, (int)v));
        o.setSurface = true;
        o.surface = Surface::Rough;
        return true;
    }
    case K::Sawdust: {
        float d = blobDist(u, v, 66, seed);
        if (d > 0) return false;
        o.color = mixc(rgb(214, 176, 120), rgb(246, 222, 176), hash2((int)u, (int)v));
        o.alpha = clampf(-d / 14.0f, 0, 0.95f);
        if (o.alpha > 0.35f) { o.setSurface = true; o.surface = Surface::Rough; }
        return true;
    }
    case K::Shavings: {
        // A curl of planed wood: a spiral ribbon, two turns long.
        float a = std::atan2(v, u) + PI + (float)seed;
        a = std::fmod(a, TAU);
        for (int turn = 0; turn < 2; ++turn) {
            float d = r - (5.0f + (a + TAU * turn) * 2.3f);
            if (std::fabs(d) < 2.0f) {
                o.color = shade(WOOD_LIGHT, 0.86f + 0.16f * (d / 2.0f) + 0.08f * (float)turn);
                return true;
            }
        }
        return false;
    }
    case K::Screw: {
        if (u < -14) { float du = u + 14; if (du * du + v * v > 25) return false; o.color = radial(STEEL, du, v, 5, l, 0.4f); if (std::fabs(du) < 0.7f) o.color = BLACK; return true; }
        float hw = u > 12 ? 2.2f * (18 - u) / 6 : 2.2f;
        if (u > 18 || std::fabs(v) > hw) return false;
        o.color = std::fmod(u + 40, 3.0f) < 1.2f ? STEEL_DARK : STEEL;
        return true;
    }
    case K::Nail: {
        if (u < -13) { float du = u + 13; if (du * du + v * v > 12) return false; o.color = STEEL; return true; }
        if (u > 16 || std::fabs(v) > 1.4f) return false;
        o.color = rod(v, 1.4f, STEEL, l, 0.5f);
        return true;
    }
    case K::GlueSpill: return spill(u, v, seed, rgb(244, 240, 226), 0.92f, Surface::Sticky, o);
    case K::Vise: {
        float d = sdBox(u, v, 70, 50, 6);
        if (d > 0) return false;
        uint32_t c = edged(rgb(70, 90, 100), d, 3.5f, 0.72f);
        if (u > 36 && u < 48) c = STEEL;
        if (std::fabs(v) < 5 && u < 36) c = rod(v, 5, STEEL, l, 0.6f);
        o.color = c;
        return true;
    }
    case K::Pencil: {
        if (std::fabs(u) > 95) return false;
        float R = u > 70 ? 5.5f * (95 - u) / 25 : 5.5f;
        if (std::fabs(v) > R) return false;
        static const uint32_t colors[3] = {rgb(240, 196, 50), rgb(200, 60, 50), rgb(60, 120, 80)};
        uint32_t c = colors[seed % 3];
        if (u < -84) c = rgb(232, 140, 150);
        else if (u < -74) c = BRASS;
        else if (u >= 88) c = rgb(50, 50, 56);
        else if (u >= 70) c = WOOD_LIGHT;
        else if (std::fabs(v) < 1.8f) c = shade(c, 1.12f);
        o.color = rod(v, 5.5f, c, l, 0.3f);
        return true;
    }
    default: return false;
    }
}

bool shadeDesk(PropKind k, float u, float v, float r, int seed, Vec2 l, PropPixel& o) {
    using K = PropKind;
    static const uint32_t covers[5] = {rgb(150, 50, 50), rgb(40, 84, 130), rgb(60, 120, 90), rgb(120, 70, 140), rgb(190, 130, 50)};
    switch (k) {
    case K::Book: {
        float d = sdBox(u, v, 100, 70, 3);
        if (d > 0) return false;
        uint32_t c = covers[seed % 5];
        if (u < -86) c = shade(c, 0.74f);
        if (sdBox(u + 4, v + 20, 52, 13) < 0) c = rgb(232, 214, 160);
        if (v > 63) c = (cell(v, 1.5f) & 1) ? rgb(236, 230, 214) : rgb(214, 208, 190);
        o.color = edged(c, d, 2.0f, 0.75f);
        return true;
    }
    case K::BookStack: {
        float top = sdBox(u, v, 100, 70, 3);
        if (top < 0) {
            uint32_t c = covers[seed % 5];
            if (u < -86) c = shade(c, 0.74f);
            if (sdBox(u + 4, v + 20, 52, 13) < 0) c = rgb(232, 214, 160);
            o.color = edged(c, top, 2.0f, 0.75f);
            return true;
        }
        float low = sdBox(u - 12, v - 11, 104, 74, 3);
        if (low > 0) return false;
        o.color = edged(covers[(seed + 2) % 5], low, 2.0f, 0.75f);
        return true;
    }
    case K::Notebook: {
        float d = sdBox(u, v, 85, 60, 3);
        if (d > 0) return false;
        uint32_t c = rgb(244, 240, 226);
        if (std::fmod(v + 200, 13.0f) < 1) c = rgb(170, 196, 226);
        if (u < -70) c = covers[seed % 5];
        if (u < -76 && u > -86 && std::fmod(v + 200, 12.0f) < 6) c = STEEL;
        o.color = edged(c, d, 1.5f, 0.86f);
        return true;
    }
    case K::PaperSheet: {
        float d = sdBox(u, v, 63, 89);
        if (d > 0) return false;
        uint32_t c = rgb(250, 248, 240);
        if (std::fmod(v + 300, 11.0f) < 1) c = rgb(186, 208, 234);
        if (std::fabs(u + 46) < 0.8f) c = rgb(228, 120, 120);
        int line = cell(v + 300, 11);
        if (line > 22 && line < 34 && u > -40 && u < 44 && hash2(cell(u, 5), line + seed) > 0.5f && std::fmod(v + 300, 11.0f) > 6)
            c = rgb(60, 66, 96);
        o.color = edged(c, d, 1.2f, 0.88f);
        return true;
    }
    case K::Eraser: {
        float d = sdBox(u, v, 30, 15, 5);
        if (d > 0) return false;
        o.color = edged(u < 6 ? rgb(60, 110, 180) : rgb(236, 170, 180), d, 2.0f, 0.8f);
        return true;
    }
    case K::Ruler: {
        float d = sdBox(u, v, 100, 14);
        if (d > 0) return false;
        uint32_t c = rgb(236, 204, 120);
        float m = std::fmod(u + 200, 6.0f);
        if (m < 1 && v < (std::fmod(u + 200, 30.0f) < 1 ? 2 : -6)) c = BLACK;
        o.color = edged(c, d, 1.2f, 0.82f);
        return true;
    }
    case K::PaperClip: {
        if (std::fabs(sdBox(u, v, 14, 5, 5)) < 1.0f || std::fabs(sdBox(u - 2, v, 10, 2.6f, 2.5f)) < 0.8f) { o.color = rgb(204, 212, 222); return true; }
        return false;
    }
    case K::Stapler: {
        float d = sdBox(u, v, 62, 17, 6);
        if (d > 0) return false;
        uint32_t c = rod(v, 17, rgb(150, 40, 40), l, 0.5f);
        if (u > -20 && std::fabs(v) < 8) c = rod(v, 8, STEEL, l, 0.6f);
        o.color = c;
        return true;
    }
    case K::PenCup: {
        if (r > 42) return false;
        if (r > 37) { o.color = radial(rgb(60, 70, 90), u, v, r, l, 0.3f); return true; }
        o.color = rgb(26, 28, 36);
        static const float tips[5][2] = {{-14, -10}, {12, -16}, {16, 12}, {-8, 18}, {0, 2}};
        static const uint32_t colors[5] = {rgb(240, 196, 50), rgb(200, 60, 50), rgb(60, 120, 180), rgb(80, 170, 110), rgb(236, 170, 180)};
        for (int i = 0; i < 5; ++i) {
            float du = u - tips[i][0], dv = v - tips[i][1];
            if (du * du + dv * dv < 36) o.color = sphere(du, dv, 6, colors[i], l, 0.5f);
        }
        return true;
    }
    case K::Calculator: {
        float d = sdBox(u, v, 55, 85, 6);
        if (d > 0) return false;
        uint32_t c = edged(rgb(60, 64, 72), d, 3.0f, 0.75f);
        if (sdBox(u, v + 62, 44, 14, 2) < 0) c = rgb(150, 176, 150);
        if (v > -40) {
            float cu = std::fmod(u + 220, 24.0f) - 12, cv = std::fmod(v + 220, 22.0f) - 11;
            if (std::fabs(u) < 48 && v < 78 && sdBox(cu, cv, 9, 8, 2) < 0) c = (cell(v + 220, 22) & 1) ? rgb(200, 204, 212) : rgb(176, 182, 192);
        }
        o.color = c;
        return true;
    }
    case K::Scissors: {
        for (float a : {0.12f, -0.12f}) {
            float cu = std::cos(a), su = std::sin(a);
            float u1 = u * cu - v * su, v1 = u * su + v * cu;
            if (u1 >= -4 && u1 <= 95 && std::fabs(v1) < 6.5f * (1 - u1 / 100) + 1) { o.color = metal(u1, v1); return true; }
        }
        for (float side : {1.0f, -1.0f}) {
            float du = u + 62, dv = v - side * 20, dr = std::sqrt(du * du + dv * dv);
            if (dr < 20 && dr > 12) { o.color = radial(rgb(200, 50, 44), du, dv, dr, l, 0.3f); return true; }
            if (sdSeg(u, v, 0, 0, -48, side * 15, 5) < 0) { o.color = rgb(200, 50, 44); return true; }
        }
        return false;
    }
    case K::InkBottle: {
        if (r > 24) return false;
        o.color = r < 11 ? radial(BLACK, u, v, r, l, 0.5f) : sphere(u, v, 25, rgb(30, 40, 90), l, 0.8f);
        return true;
    }
    case K::InkSpill: return spill(u, v, seed, rgb(26, 34, 96), 0.92f, Surface::Slick, o);
    case K::StickyNote: {
        float d = sdBox(u, v, 40, 40);
        if (d > 0) return false;
        static const uint32_t colors[3] = {rgb(250, 232, 110), rgb(250, 170, 190), rgb(150, 226, 180)};
        o.color = edged(colors[seed % 3], d, 1.2f, 0.88f);
        return true;
    }
    case K::Keyboard: {
        float d = sdBox(u, v, 220, 80, 8);
        if (d > 0) return false;
        uint32_t c = edged(rgb(52, 54, 62), d, 3.5f, 0.75f);
        float cu = std::fmod(u + 660, 30.0f) - 15, cv = std::fmod(v + 660, 28.0f) - 14;
        if (std::fabs(u) < 208 && std::fabs(v) < 68 && sdBox(cu, cv, 12, 11, 2) < 0)
            c = shade(rgb(210, 214, 222), 0.92f + 0.12f * hash2(cell(u + 660, 30), cell(v + 660, 28)));
        o.color = c;
        return true;
    }
    case K::Mouse: {
        float e = (u / 34) * (u / 34) + (v / 20) * (v / 20);
        if (e > 1) return false;
        float nx = u / 34, ny = v / 20;
        uint32_t c = lit(rgb(210, 214, 222), nx, ny, std::sqrt(std::max(0.0f, 1 - e)), l, 0.5f);
        if (u > 6 && std::fabs(v) < 1) c = shade(c, 0.6f);
        o.color = c;
        return true;
    }
    case K::LampBase: {
        if (r > 70) return false;
        uint32_t c = radial(rgb(60, 66, 76), u, v, r, l, r > 62 ? 0.35f : 0.05f);
        if (r < 20) c = sphere(u, v, 20, STEEL, l, 0.7f);
        o.color = c;
        return true;
    }
    default: return false;
    }
}

bool shadeSink(PropKind k, float u, float v, float r, int seed, Vec2 l, PropPixel& o) {
    using K = PropKind;
    switch (k) {
    case K::Sponge: {
        float d = sdBox(u, v, 62, 38, 10);
        if (d > 0) return false;
        uint32_t c = v < -14 ? rgb(60, 130, 80) : rgb(244, 208, 80);
        if (hash2(cell(u, 3), cell(v, 3)) > 0.82f) c = shade(c, 0.8f);
        o.color = edged(c, d, 3.0f, 0.82f);
        return true;
    }
    case K::DishBrush: {
        float du = u - 62, dr = std::sqrt(du * du + v * v);
        if (dr < 30) {
            o.color = dr > 26 ? rgb(226, 214, 180) : (((cell(du, 4) + cell(v, 4)) & 1) ? rgb(236, 226, 196) : rgb(186, 170, 130));
            return true;
        }
        if (u < -100 || u > 40 || std::fabs(v) > 7) return false;
        o.color = rod(v, 7, grain(rgb(190, 142, 88), u, v), l, 0.3f);
        return true;
    }
    case K::Saucepan: {
        if (r <= 90) {
            if (r > 84) { o.color = radial(STEEL, u, v, r, l, 0.3f); return true; }
            o.color = radial(mixc(rgb(90, 96, 106), rgb(120, 126, 136), fbm(u * 0.03f, v * 0.03f)), u, v, r, l, -0.25f);
            return true;
        }
        if (u < 88 || u > 262 || std::fabs(v) > 11) return false;
        o.color = rod(v, 11, BLACK, l, 0.4f);
        return true;
    }
    case K::Suds: {
        if (blobDist(u, v, 44, seed) > 0) return false;
        int cx = cell(u, 12), cy = cell(v, 12);
        float px = (cx + 0.5f) * 12 + (hash2(cx, cy) - 0.5f) * 5, py = (cy + 0.5f) * 12 + (hash2(cy, cx) - 0.5f) * 5;
        float du = u - px, dv = v - py, dr = std::sqrt(du * du + dv * dv), rad = 4.5f + hash2(cx + 3, cy) * 4;
        o.setSurface = true;
        o.surface = Surface::Rough;
        if (dr > rad) { o.color = rgb(226, 238, 246); o.alpha = 0.85f; return true; }
        o.color = radial(rgb(244, 250, 254), du, dv, dr, l, 0.25f);
        return true;
    }
    case K::SudsPuff: {
        if (r > 7) return false;
        o.color = radial(rgb(244, 250, 254), u, v, r, l, 0.3f);
        o.alpha = 0.9f;
        return true;
    }
    case K::Drain: {
        if (r > 30) return false;
        if (r < 16) {
            o.color = shade(rgb(14, 24, 36), 0.7f + 0.6f * r / 16);
            o.setSurface = true;
            o.surface = Surface::Void;
            return true;
        }
        float a = std::atan2(v, u);
        o.color = r > 26 ? radial(STEEL, u, v, r, l, 0.3f) : (std::fmod(a + TAU, TAU / 12) < TAU / 24 ? STEEL : STEEL_DARK);
        return true;
    }
    default: return false;
    }
    (void)seed;
}

bool shadeSandbox(PropKind k, float u, float v, float r, int seed, Vec2 l, PropPixel& o) {
    using K = PropKind;
    const uint32_t SAND = rgb(226, 194, 136);
    switch (k) {
    case K::Bucket: {
        if (r > 62) return false;
        static const uint32_t colors[3] = {rgb(214, 60, 50), rgb(50, 120, 200), rgb(240, 190, 50)};
        uint32_t c = colors[seed % 3];
        if (std::fabs(v) < 3 && r < 56) { o.color = rod(v, 3, STEEL, l, 0.6f); return true; }
        if (r > 54) { o.color = radial(c, u, v, r, l, 0.3f); return true; }
        o.color = r > 46 ? shade(c, 0.6f) : radial(SAND, u, v, r, l, -0.2f);
        return true;
    }
    case K::Spade: {
        if (u > 40 && u < 110) {
            float hw = u > 90 ? 26 * (110 - u) / 20 : 26.0f;
            if (std::fabs(v) > hw) return false;
            o.color = radial(rgb(50, 130, 190), u - 70, v, 34, l, -0.2f);
            return true;
        }
        if (u < -110 || std::fabs(v) > 6) {
            if (std::fabs(u + 104) < 6 && std::fabs(v) < 22) { o.color = rod(u + 104, 6, rgb(240, 190, 50), l, 0.4f); return true; }
            return false;
        }
        if (u > 40) return false;
        o.color = rod(v, 6, rgb(240, 190, 50), l, 0.4f);
        return true;
    }
    case K::Rake: {
        if (u > 66 && u < 78 && std::fabs(v) < 42) { o.color = rod(u - 72, 6, rgb(90, 180, 100), l, 0.4f); return true; }
        if (u >= 78 && u < 100 && std::fabs(v) < 42 && std::fmod(v + 100, 14.0f) < 5) { o.color = rgb(70, 150, 84); return true; }
        if (u < -110 || u > 66 || std::fabs(v) > 6) return false;
        o.color = rod(v, 6, rgb(90, 180, 100), l, 0.4f);
        return true;
    }
    case K::Castle: {
        float best = 1e9f, tu = 0, tv = 0;
        for (float a : {-1.0f, 1.0f})
            for (float b : {-1.0f, 1.0f}) {
                float du = u - a * 54, dv = v - b * 54, d = std::sqrt(du * du + dv * dv);
                if (d < best) { best = d; tu = du; tv = dv; }
            }
        if (best < 28) {
            float a = std::atan2(tv, tu);
            uint32_t c = best > 20 ? (std::sin(a * 6) > 0 ? shade(SAND, 1.12f) : shade(SAND, 0.7f)) : shade(SAND, 0.82f);
            o.color = radial(c, tu, tv, best, l, 0.18f);
            return true;
        }
        float d = sdBox(u, v, 54, 54, 2);
        if (d > 0) return false;
        uint32_t c = shade(SAND, 0.95f + 0.1f * hash2((int)u, (int)v));
        if (d > -8) c = (std::fmod(u + v + 400, 14.0f) < 7) ? shade(SAND, 1.12f) : shade(SAND, 0.72f);
        else if (sdBox(u, v, 26, 26, 2) < 0) c = shade(SAND, 0.8f);
        o.color = c;
        return true;
    }
    case K::Tower: {
        if (r > 38) return false;
        float a = std::atan2(v, u);
        uint32_t c = r > 28 ? (std::sin(a * 8) > 0 ? shade(SAND, 1.12f) : shade(SAND, 0.68f)) : shade(SAND, 0.82f);
        o.color = radial(c, u, v, r, l, 0.16f);
        return true;
    }
    case K::Pebble:
    case K::Rock: {
        float R0 = k == K::Rock ? 40.0f : 18.0f;
        float a = std::atan2(v, u);
        float R = R0 * (0.82f + 0.36f * fbm(std::cos(a) * 1.5f + seed, std::sin(a) * 1.5f));
        if (r > R) return false;
        uint32_t c = mixc(rgb(120, 118, 114), rgb(170, 166, 160), hash2(seed, 3));
        o.color = sphere(u, v, R * 1.02f, shade(c, 0.94f + 0.12f * fbm(u * 0.2f, v * 0.2f)), l, 0.12f);
        return true;
    }
    case K::Ball: {
        if (r > 56) return false;
        static const uint32_t gores[6] = {rgb(226, 60, 60), rgb(250, 250, 245), rgb(50, 130, 210), rgb(250, 250, 245), rgb(246, 200, 50), rgb(250, 250, 245)};
        float a = std::atan2(v + 14, u + 10);
        int g = (int)std::floor((a + PI) / (TAU / 6));
        uint32_t c = gores[((g % 6) + 6) % 6];
        o.color = sphere(u, v, 57, c, l, 0.7f);
        return true;
    }
    case K::Stick: {
        if (std::fabs(u) > 112) return false;
        float hw = 7 + 1.5f * std::sin(u * 0.05f + seed);
        if (std::fabs(v) > hw) return false;
        o.color = rod(v, hw, shade(WOOD_DARK, 0.9f + 0.3f * fbm(u * 0.15f, v * 0.5f)), l, 0.1f);
        return true;
    }
    case K::Leaf: {
        float e = (u / 22) * (u / 22) + (v / 11) * (v / 11);
        if (e > 1) return false;
        uint32_t c = (seed & 1) ? rgb(120, 150, 60) : rgb(176, 130, 50);
        if (std::fabs(v) < 0.8f) c = shade(c, 0.7f);
        o.color = c;
        return true;
    }
    case K::SmallPebble: {
        if (r > 5) return false;
        o.color = sphere(u, v, 5.2f, mixc(rgb(130, 126, 120), rgb(200, 196, 186), (float)(seed % 5) / 5), l, 0.2f);
        return true;
    }
    default: return false;
    }
}

bool shadeFreezer(PropKind k, float u, float v, float r, int seed, Vec2 l, PropPixel& o) {
    using K = PropKind;
    switch (k) {
    case K::IceCube: {
        float d = sdBox(u, v, 24, 24, 7);
        if (d > 0) return false;
        uint32_t c = mixc(rgb(170, 214, 240), rgb(232, 246, 252), fbm(u * 0.1f, v * 0.1f));
        if (d > -3) c = rgb(246, 252, 255);
        o.color = c;
        o.alpha = 0.92f;
        return true;
    }
    case K::IceTray: {
        float d = sdBox(u, v, 130, 48, 6);
        if (d > 0) return false;
        uint32_t c = edged(rgb(70, 130, 190), d, 3.0f, 0.76f);
        float cu = std::fmod(u + 650, 42.0f) - 21, cv = std::fmod(v + 648, 42.0f) - 21;
        if (std::fabs(u) < 124 && std::fabs(v) < 42 && sdBox(cu, cv, 17, 17, 4) < 0)
            c = mixc(rgb(186, 224, 244), rgb(238, 248, 252), fbm(u * 0.1f, v * 0.1f));
        o.color = c;
        return true;
    }
    case K::PeaBag: {
        float wob = 5 * (fbm(u * 0.03f + seed, v * 0.03f) - 0.5f);
        float d = sdBox(u, v, 96, 70, 16) + wob;
        if (d > 0) return false;
        uint32_t c = rgb(40, 130, 76);
        int cx = cell(u, 13), cy = cell(v, 13);
        float du = std::fmod(u + 1300, 13.0f) - 6.5f, dv = std::fmod(v + 1300, 13.0f) - 6.5f;
        if (du * du + dv * dv < 24 && hash2(cx, cy) > 0.3f) c = sphere(du, dv, 5, rgb(120, 200, 90), l, 0.4f);
        if (sdBox(u, v + 30, 54, 16, 3) < 0) c = rgb(240, 240, 232);
        o.color = edged(c, d, 4.0f, 0.74f);
        return true;
    }
    case K::Tub: {
        if (r > 84) return false;
        static const uint32_t colors[3] = {rgb(226, 120, 150), rgb(120, 190, 230), rgb(246, 210, 130)};
        uint32_t c = colors[seed % 3];
        if (r > 78) { o.color = radial(c, u, v, r, l, 0.25f); return true; }
        if (r < 44) c = rgb(250, 246, 236);
        if (r < 40 && r > 34) c = colors[(seed + 1) % 3];
        o.color = c;
        return true;
    }
    case K::FoodBox: {
        float d = sdBox(u, v, 120, 85, 3);
        if (d > 0) return false;
        static const uint32_t colors[3] = {rgb(196, 60, 50), rgb(40, 90, 150), rgb(230, 150, 40)};
        uint32_t c = colors[seed % 3];
        if (sdBox(u + 20, v + 10, 70, 44, 22) < 0) c = rgb(240, 216, 150);
        if (v > 54 && std::fabs(u) < 100) c = rgb(250, 250, 245);
        c = mixc(c, rgb(234, 244, 250), clampf((fbm(u * 0.04f + seed, v * 0.04f) - 0.52f) * 3, 0, 0.7f));
        o.color = edged(c, d, 3.0f, 0.72f);
        return true;
    }
    case K::Popsicle: {
        if (u < -40) {
            if (u < -78 || std::fabs(v) > 6) return false;
            o.color = edged(WOOD_LIGHT, sdBox(u + 58, v, 20, 6), 1.0f, 0.82f);
            return true;
        }
        float d = sdBox(u, v, 46, 21, 18);
        if (d > 0) return false;
        static const uint32_t colors[3] = {rgb(226, 60, 70), rgb(240, 150, 50), rgb(90, 180, 220)};
        o.color = rod(v, 21, colors[seed % 3], l, 0.55f);
        return true;
    }
    case K::FishStick: {
        float d = sdBox(u, v, 34, 11, 5);
        if (d > 0) return false;
        o.color = rod(v, 11, shade(rgb(224, 160, 70), 0.88f + 0.24f * hash2(cell(u, 2), cell(v, 2))), l, 0.15f);
        return true;
    }
    case K::FrostPatch: {
        float d = blobDist(u, v, 52, seed);
        if (d > 0) return false;
        o.color = rgb(250, 253, 255);
        o.alpha = clampf(-d / 22.0f, 0, 0.85f) * (0.65f + 0.35f * hash2(cell(u, 2), cell(v, 2)));
        return true;
    }
    case K::IcePatch: return spill(u, v, seed, rgb(150, 206, 240), 0.8f, Surface::Slick, o);
    case K::Pea: {
        if (r > 4.5f) return false;
        o.color = sphere(u, v, 4.7f, rgb(110, 190, 84), l, 0.4f);
        return true;
    }
    default: return false;
    }
}
}  // namespace

bool shadeProp(PropKind k, float u, float v, int seed, Vec2 l, PropPixel& o) {
    o = PropPixel{};
    o.solid = propInfo(k).solid;
    const float r = std::sqrt(u * u + v * v);
    if (seed < 0) seed = -seed;
    if (k <= PropKind::CerealRing) return shadeKitchen(k, u, v, r, seed, l, o);
    if (k <= PropKind::Pencil) return shadeWorkshop(k, u, v, r, seed, l, o);
    if (k <= PropKind::LampBase) return shadeDesk(k, u, v, r, seed, l, o);
    if (k <= PropKind::Drain) return shadeSink(k, u, v, r, seed, l, o);
    if (k <= PropKind::SmallPebble) return shadeSandbox(k, u, v, r, seed, l, o);
    return shadeFreezer(k, u, v, r, seed, l, o);
}
