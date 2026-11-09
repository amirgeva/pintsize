#include "gfx.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr int CHUNK = 256;
constexpr int FRAMES = 64;
constexpr int FRAME_SIZE = 32;
constexpr size_t MAX_SKIDS = 3000;
constexpr float FALL_DURATION = 0.8f;

// 5x7 glyphs for ASCII 32..90, one byte per column, least significant bit at the top.
const uint8_t GLYPHS[59][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x56,0x20,0x50},{0x00,0x08,0x07,0x03,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x2A,0x1C,0x7F,0x1C,0x2A},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x80,0x70,0x30,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x00,0x60,0x60,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x72,0x49,0x49,0x49,0x46},{0x21,0x41,0x49,0x4D,0x33},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x31},{0x41,0x21,0x11,0x09,0x07},
    {0x36,0x49,0x49,0x49,0x36},{0x46,0x49,0x49,0x29,0x1E},{0x00,0x00,0x14,0x00,0x00},{0x00,0x40,0x34,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x59,0x09,0x06},
    {0x3E,0x41,0x5D,0x59,0x4E},{0x7C,0x12,0x11,0x12,0x7C},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x41,0x3E},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x41,0x51,0x73},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x1C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x26,0x49,0x49,0x49,0x32},
    {0x03,0x01,0x7F,0x01,0x03},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
    {0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x59,0x49,0x4D,0x43},
};

enum : uint32_t { BODY = 1, DARK = 2, LIGHT = 3 };
const uint32_t TYRE = 0xFF16161Au, GLASS = 0xFF24344Eu, STEEL = 0xFF8C94A0u, CHROME = 0xFFD8DEE6u;

struct Part { float u0, u1, v0, v1; uint32_t color; float taper; };

std::vector<Part> vehicleParts(VehicleKind k) {
    switch (k) {
    case VehicleKind::Coupe: return {
        {-8,-4,-6.5f,6.5f,TYRE,0},{4,8,-6.5f,6.5f,TYRE,0},
        {-10,10,-5,5,BODY,0.25f},{-10,-8,-5,5,DARK,0},{-5,3,-4,4,DARK,0},{-3,0.5f,-3.4f,3.4f,LIGHT,0},
        {0.5f,3.5f,-3.6f,3.6f,GLASS,0},{-6,-3.8f,-3.4f,3.4f,GLASS,0},{8.5f,10,-3.5f,3.5f,CHROME,0}};
    case VehicleKind::Buggy: return {
        {-9,-3,-7.5f,7.5f,TYRE,0},{4,9,-7,7,TYRE,0},{-7,8,-2,2,STEEL,0},{-2,4,-6,6,STEEL,0},
        {-8,3,-4.5f,4.5f,BODY,0},{-4,1,-3,3,GLASS,0},{-9,-7.5f,-5,5,DARK,0},{3,8,-3,3,DARK,0.3f}};
    case VehicleKind::Formula: return {
        {-9,-4,-6.5f,6.5f,TYRE,0},{5,9,-6.5f,6.5f,TYRE,0},{10,13,-6,6,DARK,0},{-12,-9.5f,-6,6,DARK,0},
        {-10,12,-2.5f,2.5f,BODY,0.4f},{-6,2,-4,4,BODY,0},{-3,1,-1.8f,1.8f,GLASS,0}};
    case VehicleKind::Skiff: return {
        {-11,-9,-3,3,TYRE,0},{-9,3,-5.5f,5.5f,BODY,0},{3,13,-5.5f,5.5f,BODY,0.9f},
        {-7,2,-3.8f,3.8f,DARK,0},{-3,2,-3,3,CHROME,0},{2,4,-3.4f,3.4f,GLASS,0}};
    case VehicleKind::Tank: return {
        {-11,11,-9,-5,TYRE,0},{-11,11,5,9,TYRE,0},{-10,10,-6,6,BODY,0},{-6,4,-4.5f,4.5f,DARK,0},
        {-4,3,-3.5f,3.5f,LIGHT,0},{3,16,-1.2f,1.2f,STEEL,0}};
    case VehicleKind::Sled: return {
        {-11,12,-6.5f,-5,STEEL,0},{-11,12,5,6.5f,STEEL,0},{-9,6,-4.5f,4.5f,BODY,0},{6,11,-4.5f,4.5f,BODY,0.8f},
        {-6,1,-3,3,DARK,0},{-3,0.5f,-2.2f,2.2f,CHROME,0}};
    default: return {};
    }
}

uint32_t scaleColor(uint32_t c, float f) {
    int r = std::min(255, (int)(((c >> 16) & 255) * f));
    int g = std::min(255, (int)(((c >> 8) & 255) * f));
    int b = std::min(255, (int)((c & 255) * f));
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

SDL_Color col(int r, int g, int b, int a = 255) { return {(Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a}; }
}  // namespace

const char* driverName(int i) {
    static const char* names[NUM_DRIVERS] = {"DOT", "RIVET", "PIXIE", "BOLT", "GUS", "MOTH"};
    return names[i];
}

SDL_Color driverColor(int i) {
    static const SDL_Color colors[NUM_DRIVERS] = {
        {226,58,52,255},{56,120,232,255},{240,198,44,255},{52,184,92,255},{170,80,216,255},{242,130,40,255}};
    return colors[i];
}

Gfx::Gfx(SDL_Renderer* r) : r_(r) {
    buildFont();
    buildCars();
    skids_.reserve(MAX_SKIDS);
}

Gfx::~Gfx() {
    freeChunks();
    for (SDL_Texture* t : cars_) SDL_DestroyTexture(t);
    if (font_) SDL_DestroyTexture(font_);
}

void Gfx::buildFont() {
    std::vector<uint32_t> px(59 * 6 * 8, 0);
    for (int g = 0; g < 59; ++g)
        for (int x = 0; x < 5; ++x)
            for (int y = 0; y < 8; ++y)
                if (GLYPHS[g][x] & (1 << y)) px[(size_t)y * 59 * 6 + g * 6 + x] = 0xFFFFFFFFu;
    font_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 59 * 6, 8);
    SDL_UpdateTexture(font_, nullptr, px.data(), 59 * 6 * 4);
    SDL_SetTextureBlendMode(font_, SDL_BLENDMODE_BLEND);
}

void Gfx::buildCars() {
    cars_.reserve((int)VehicleKind::Count * NUM_DRIVERS);
    std::vector<uint32_t> frame(FRAME_SIZE * FRAME_SIZE);
    std::vector<uint32_t> atlas(FRAMES * FRAME_SIZE * FRAME_SIZE);
    for (int k = 0; k < (int)VehicleKind::Count; ++k) {
        auto parts = vehicleParts((VehicleKind)k);
        for (int d = 0; d < NUM_DRIVERS; ++d) {
            SDL_Color dc = driverColor(d);
            uint32_t body = 0xFF000000u | ((uint32_t)dc.r << 16) | ((uint32_t)dc.g << 8) | dc.b;
            std::fill(atlas.begin(), atlas.end(), 0u);
            for (int f = 0; f < FRAMES; ++f) {
                float a = TAU * f / FRAMES, ca = std::cos(a), sa = std::sin(a);
                std::fill(frame.begin(), frame.end(), 0u);
                for (int y = 0; y < FRAME_SIZE; ++y) {
                    for (int x = 0; x < FRAME_SIZE; ++x) {
                        float px = x + 0.5f - FRAME_SIZE / 2.0f, py = y + 0.5f - FRAME_SIZE / 2.0f;
                        float u = px * ca + py * sa, v = -px * sa + py * ca;
                        for (const Part& p : parts) {
                            if (u < p.u0 || u > p.u1) continue;
                            float t = (u - p.u0) / (p.u1 - p.u0);
                            float scale = 1.0f - p.taper * t;
                            if (v < p.v0 * scale || v > p.v1 * scale) continue;
                            uint32_t c = p.color;
                            if (c == BODY) c = body;
                            else if (c == DARK) c = scaleColor(body, 0.6f);
                            else if (c == LIGHT) c = scaleColor(body, 1.3f);
                            frame[(size_t)y * FRAME_SIZE + x] = c;
                        }
                    }
                }
                // A one-pixel dark outline and a highlight on the sunward edge.
                for (int y = 0; y < FRAME_SIZE; ++y) {
                    for (int x = 0; x < FRAME_SIZE; ++x) {
                        uint32_t c = frame[(size_t)y * FRAME_SIZE + x];
                        auto solid = [&](int ax, int ay) {
                            return ax >= 0 && ay >= 0 && ax < FRAME_SIZE && ay < FRAME_SIZE && frame[(size_t)ay * FRAME_SIZE + ax] != 0;
                        };
                        uint32_t out = c;
                        if (c == 0) {
                            if (solid(x - 1, y) || solid(x + 1, y) || solid(x, y - 1) || solid(x, y + 1)) out = 0xFF101014u;
                        } else if (!solid(x - 1, y) || !solid(x, y - 1)) {
                            out = scaleColor(c, 1.25f);
                        }
                        atlas[(size_t)y * FRAMES * FRAME_SIZE + f * FRAME_SIZE + x] = out;
                    }
                }
            }
            SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, FRAMES * FRAME_SIZE, FRAME_SIZE);
            SDL_UpdateTexture(t, nullptr, atlas.data(), FRAMES * FRAME_SIZE * 4);
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            cars_.push_back(t);
        }
    }
}

void Gfx::freeChunks() {
    for (SDL_Texture* t : chunks_) SDL_DestroyTexture(t);
    chunks_.clear();
}

void Gfx::setTrack(const Track& t) {
    freeChunks();
    chunkCols_ = (t.width() + CHUNK - 1) / CHUNK;
    chunkRows_ = (t.height() + CHUNK - 1) / CHUNK;
    for (int cy = 0; cy < chunkRows_; ++cy) {
        for (int cx = 0; cx < chunkCols_; ++cx) {
            int w = std::min(CHUNK, t.width() - cx * CHUNK);
            int h = std::min(CHUNK, t.height() - cy * CHUNK);
            SDL_Texture* tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
            const uint32_t* src = t.pixels().data() + (size_t)cy * CHUNK * t.width() + cx * CHUNK;
            SDL_UpdateTexture(tex, nullptr, src, t.width() * 4);
            chunks_.push_back(tex);
        }
    }
    resetFx();
}

void Gfx::resetFx() {
    particles_.clear();
    skids_.clear();
    skidHead_ = 0;
}

void Gfx::spawn(Vec2 p, Vec2 v, float life, SDL_Color c, int size) {
    if (particles_.size() < 600) particles_.push_back({p, v, life, life, c, size});
}

void Gfx::burst(Vec2 p, int n, float speed, float life, SDL_Color c, int size) {
    std::uniform_real_distribution<float> a(0, TAU), s(0.3f, 1.0f);
    for (int i = 0; i < n; ++i) spawn(p, fromAngle(a(rng_)) * (speed * s(rng_)), life * s(rng_), c, size);
}

void Gfx::updateFx(const Race& race) {
    for (const RaceEvent& e : race.events()) {
        switch (e.type) {
        case EventType::Bump: case EventType::Wall: burst(e.pos, 6, 90, 0.3f, col(255, 220, 120), 1); break;
        case EventType::Splash: burst(e.pos, 22, 120, 0.6f, col(170, 220, 250), 2); break;
        case EventType::Hit: burst(e.pos, 26, 150, 0.6f, col(255, 150, 50), 2); break;
        case EventType::Land: burst(e.pos, 8, 70, 0.3f, col(210, 200, 180), 1); break;
        case EventType::Respawn: burst(e.pos, 12, 60, 0.4f, col(255, 255, 255), 1); break;
        default: break;
        }
    }

    for (const Car& c : race.cars()) {
        if (c.state != CarState::Drive || c.z > 0) continue;
        float speed = len(c.vel);
        Vec2 f = fromAngle(c.heading), r{-f.y, f.x};
        if (c.skid > 62.0f && c.surface != Surface::Rough) {
            for (int side = -1; side <= 1; side += 2) {
                Vec2 p = c.pos - f * 7.0f + r * (side * 4.5f);
                if (skids_.size() < MAX_SKIDS) skids_.push_back(p);
                else { skids_[skidHead_] = p; skidHead_ = (skidHead_ + 1) % MAX_SKIDS; }
            }
        }
        if (c.surface == Surface::Rough && speed > 50.0f && (rng_() % 3) == 0) {
            const uint8_t* d = race.track().theme().dust;
            SDL_Color dust = col(d[0], d[1], d[2]);
            spawn(c.pos - f * 7.0f, c.vel * -0.1f, 0.4f, dust, 2);
        }
    }

    for (size_t i = 0; i < particles_.size();) {
        Particle& p = particles_[i];
        p.life -= DT;
        p.pos += p.vel * DT;
        p.vel = p.vel * 0.95f;
        if (p.life <= 0) { particles_[i] = particles_.back(); particles_.pop_back(); }
        else ++i;
    }
}

void Gfx::drawCar(VehicleKind k, int color, int x, int y, float heading, float scale, int alpha) {
    SDL_Texture* tex = carTexture(k, color);
    int frame = (int)std::lround(heading / TAU * FRAMES);
    frame = ((frame % FRAMES) + FRAMES) % FRAMES;
    SDL_Rect src{frame * FRAME_SIZE, 0, FRAME_SIZE, FRAME_SIZE};
    int size = (int)std::lround(FRAME_SIZE * scale);
    SDL_Rect dst{x - size / 2, y - size / 2, size, size};
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, (Uint8)alpha);
    SDL_RenderCopy(r_, tex, &src, &dst);
}

void Gfx::drawRace(const Race& race) {
    const Track& track = race.track();
    Vec2 cam = race.camera();
    int ox = (int)std::floor(cam.x - VIEW_W * 0.5f);
    int oy = (int)std::floor(cam.y - VIEW_H * 0.5f);

    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
    SDL_RenderClear(r_);

    int c0 = std::max(0, ox / CHUNK), c1 = std::min(chunkCols_ - 1, (ox + VIEW_W) / CHUNK);
    int r0 = std::max(0, oy / CHUNK), r1 = std::min(chunkRows_ - 1, (oy + VIEW_H) / CHUNK);
    for (int cy = r0; cy <= r1; ++cy) {
        for (int cx = c0; cx <= c1; ++cx) {
            SDL_Texture* tex = chunks_[(size_t)cy * chunkCols_ + cx];
            int w, h;
            SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
            SDL_Rect dst{cx * CHUNK - ox, cy * CHUNK - oy, w, h};
            SDL_RenderCopy(r_, tex, nullptr, &dst);
        }
    }

    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, 12, 12, 16, 90);
    for (const Vec2& s : skids_) {
        int x = (int)s.x - ox, y = (int)s.y - oy;
        if (x >= 0 && y >= 0 && x < VIEW_W && y < VIEW_H) SDL_RenderDrawPoint(r_, x, y);
    }

    VehicleKind kind = track.def().vehicle;

    // Shadows first, so no car's shadow falls on top of another car.
    for (const Car& c : race.cars()) {
        if (c.state != CarState::Drive) continue;
        SDL_Texture* tex = carTexture(kind, c.color);
        int frame = (int)std::lround(c.heading / TAU * FRAMES);
        frame = ((frame % FRAMES) + FRAMES) % FRAMES;
        SDL_Rect src{frame * FRAME_SIZE, 0, FRAME_SIZE, FRAME_SIZE};
        SDL_Rect dst{(int)c.pos.x - ox - FRAME_SIZE / 2 + 2 + (int)(c.z * 0.5f),
                     (int)c.pos.y - oy - FRAME_SIZE / 2 + 3 + (int)(c.z * 0.8f), FRAME_SIZE, FRAME_SIZE};
        SDL_SetTextureColorMod(tex, 0, 0, 0);
        SDL_SetTextureAlphaMod(tex, 100);
        SDL_RenderCopy(r_, tex, &src, &dst);
    }

    SDL_SetRenderDrawColor(r_, 255, 236, 150, 255);
    for (const Shell& s : race.shells()) {
        SDL_Rect d{(int)s.pos.x - ox - 1, (int)s.pos.y - oy - 1, 3, 3};
        SDL_RenderFillRect(r_, &d);
    }

    std::vector<const Car*> order;
    for (const Car& c : race.cars()) order.push_back(&c);
    std::sort(order.begin(), order.end(), [](const Car* a, const Car* b) { return a->z < b->z; });
    for (const Car* c : order) {
        int x = (int)c->pos.x - ox, y = (int)c->pos.y - oy - (int)(c->z * 0.6f);
        if (c->state == CarState::Fall) {
            float t = clampf(c->fallTimer / FALL_DURATION, 0, 1);
            drawCar(kind, c->color, x, y, c->heading + (1 - t) * 4.0f, 0.25f + 0.75f * t, (int)(255 * t));
            continue;
        }
        if (c->invuln > 0 && (((int)(c->invuln * 14)) & 1)) continue;
        drawCar(kind, c->color, x, y, c->heading, 1.0f + c->z * 0.012f);
    }

    for (const Particle& p : particles_) {
        int alpha = (int)(255 * clampf(p.life / p.maxLife, 0, 1));
        SDL_SetRenderDrawColor(r_, p.color.r, p.color.g, p.color.b, (Uint8)alpha);
        SDL_Rect d{(int)p.pos.x - ox, (int)p.pos.y - oy, p.size, p.size};
        SDL_RenderFillRect(r_, &d);
    }

    // Name tags over the human cars during the countdown.
    if (race.phase() == Phase::Countdown) {
        for (const Car& c : race.cars()) {
            if (!c.human) continue;
            char tag[4] = {'P', (char)('1' + c.humanIndex), 0, 0};
            text((int)c.pos.x - ox, (int)c.pos.y - oy - 20, tag, col(255, 255, 255), 1, 1);
        }
    }
}

void Gfx::text(int x, int y, const char* s, SDL_Color c, int scale, int align) {
    int n = (int)std::strlen(s);
    int width = n * 6 * scale;
    if (align == 1) x -= width / 2;
    else if (align == 2) x -= width;
    SDL_SetTextureColorMod(font_, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(font_, c.a);
    for (int i = 0; i < n; ++i) {
        int ch = (unsigned char)s[i];
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 90) ch = '?';
        SDL_Rect src{(ch - 32) * 6, 0, 6, 8};
        SDL_Rect dst{x + i * 6 * scale, y, 6 * scale, 8 * scale};
        SDL_RenderCopy(r_, font_, &src, &dst);
    }
}

void Gfx::fill(int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_Rect d{x, y, w, h};
    SDL_RenderFillRect(r_, &d);
}

void Gfx::clear(SDL_Color c) {
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
    SDL_RenderClear(r_);
}

void Gfx::drawTrackPreview(const TrackDef& def, SDL_Rect area, SDL_Color c) {
    const auto& p = def.points;
    int n = (int)p.size();
    std::vector<Vec2> line;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int i = 0; i < n; ++i) {
        Vec2 p0 = p[(i + n - 1) % n], p1 = p[i], p2 = p[(i + 1) % n], p3 = p[(i + 2) % n];
        for (int k = 0; k < 12; ++k) {
            float t = k / 12.0f, t2 = t * t, t3 = t2 * t;
            Vec2 v = (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 +
                      (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
            line.push_back(v);
            minx = std::min(minx, v.x); miny = std::min(miny, v.y);
            maxx = std::max(maxx, v.x); maxy = std::max(maxy, v.y);
        }
    }
    float scale = std::min(area.w / (maxx - minx), area.h / (maxy - miny));
    float ox = area.x + (area.w - (maxx - minx) * scale) * 0.5f;
    float oy = area.y + (area.h - (maxy - miny) * scale) * 0.5f;
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, 255);
    for (size_t i = 0; i < line.size(); ++i) {
        Vec2 a = line[i], b = line[(i + 1) % line.size()];
        int ax = (int)(ox + (a.x - minx) * scale), ay = (int)(oy + (a.y - miny) * scale);
        int bx = (int)(ox + (b.x - minx) * scale), by = (int)(oy + (b.y - miny) * scale);
        for (int t = 0; t < 2; ++t) SDL_RenderDrawLine(r_, ax + t, ay, bx + t, by);
    }
    int sx = (int)(ox + (p[0].x - minx) * scale), sy = (int)(oy + (p[0].y - miny) * scale);
    SDL_SetRenderDrawColor(r_, 255, 255, 255, 255);
    SDL_Rect start{sx - 2, sy - 2, 5, 5};
    SDL_RenderFillRect(r_, &start);
}
