#pragma once
#include "common.h"
#include "props.h"
#include <vector>

enum class VehicleKind { Coupe, Buggy, Formula, Skiff, Tank, Sled, Count };

struct VehicleSpec {
    const char* name;
    float accel;       // px/s^2 of thrust
    float topSpeed;    // px/s; drag is derived as accel / topSpeed
    float brake;       // px/s^2
    float turnRate;    // rad/s; applies at any speed, including standing still
    float grip;        // 1/s decay of sideways velocity; low values slide more
    float mass;
    float radius;      // used for shell hits
    float halfLength, halfWidth;   // collision box, matched to the sprite
    float roughScale;  // 1 = full off-road penalty, 0 = none
    bool hasGun;
};
const VehicleSpec& vehicleSpec(VehicleKind k);

enum class ThemeId { Kitchen, Workshop, Freezer, Sink, Desk, Sandbox };

struct ThemeInfo {
    const char* name;
    Surface off;      // the ground beside the route
    Surface gap;      // what a jump crosses
    Surface edge;     // what lies past the edge of the world: a drop, or a wall
    float roadGrip;   // grip multiplier on the ground
    uint8_t dust[3];  // colour kicked up from rough ground
};
const ThemeInfo& themeInfo(ThemeId t);

struct JumpDef {
    float at;    // position along the route, in control-point units (3.5 = halfway from point 3 to 4)
    float gap;   // length of the gap in px
};

struct PropPlacement {
    PropKind kind;
    bool routeRelative;  // false: a,b are x,y. true: a is the route position, b the offset from its centre
    float a, b;
    float rotDeg;        // absolute, or relative to the route direction when routeRelative
    float scale;
    int seed;
    bool submerged;      // seen through water: no collision
};

struct ScatterEntry {
    PropKind kind;
    int weight;
    float minScale, maxScale;
    bool submerged;
};

struct ThemeDressing {
    std::vector<ScatterEntry> scatter;
    int attempts;
    bool hasBorderPiece;
    PropKind borderPiece;
    float borderSpacing;
};
const ThemeDressing& themeDressing(ThemeId t);

struct TrackDef {
    const char* name;
    ThemeId theme;
    VehicleKind vehicle;
    float halfWidth;
    int laps;
    std::vector<Vec2> points;
    std::vector<JumpDef> jumps;
    std::vector<PropPlacement> props;
    int seed;
};
const std::vector<TrackDef>& allTracks();

// The six drivers; a car's colour index picks one.
constexpr int NUM_DRIVERS = 6;
const char* driverName(int i);

struct Sample {
    Vec2 pos, tan, normal;
    float curvature = 0;   // signed 1/radius; positive turns toward +normal
    float line = 0;        // AI racing-line offset along the normal
    float lineCurv = 0;    // curvature of the racing line itself
};

struct JumpSpan {
    int rampStart, gapStart, gapEnd;   // sample indices
};

class Track {
public:
    static constexpr float STEP = 4.0f;       // px between samples
    static constexpr float APRON = 100.0f;    // how far beside the route the track "owns" the ground
    static constexpr float EDGE = 70.0f;      // width of the band outside the table
    static constexpr int RUNUP_SAMPLES = 70;  // respawn this far before a jump

    explicit Track(const TrackDef& def);

    const TrackDef& def() const { return *def_; }
    const ThemeInfo& theme() const { return themeInfo(def_->theme); }
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<uint32_t>& pixels() const { return pixels_; }
    const std::vector<Sample>& samples() const { return samples_; }
    const std::vector<JumpSpan>& jumps() const { return jumps_; }
    int count() const { return (int)samples_.size(); }
    float length() const { return count() * STEP; }
    float halfWidth() const { return def_->halfWidth; }
    int blockedLineSamples() const { return blockedLine_; }

    Surface at(Vec2 p) const {
        int x = (int)std::floor(p.x), y = (int)std::floor(p.y);
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return Surface::Void;
        return surf_[(size_t)y * w_ + x];
    }
    int wrap(int i) const { int n = count(); return ((i % n) + n) % n; }
    int nearest(Vec2 p, int around, int window) const;
    int respawnIndex(int i) const { return respawn_[wrap(i)]; }
    bool inJumpZone(int i) const;

private:
    void buildCenterline();
    void buildJumps();
    void paint();
    void buildRacingLine();
    float routeS(float at) const;

    const TrackDef* def_;
    int w_ = 0, h_ = 0;
    Vec2 shift_;
    std::vector<Vec2> pts_;
    std::vector<float> segStart_;
    std::vector<Sample> samples_;
    std::vector<JumpSpan> jumps_;
    std::vector<int> respawn_;
    std::vector<uint32_t> pixels_;
    std::vector<Surface> surf_;
    int blockedLine_ = 0;
};
