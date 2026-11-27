#include "race.h"
#include <algorithm>

namespace {
constexpr int HEADING_STEPS = 32;          // steering is notched, like a sprite-rotation racer

// Player steering is linear: angle turned = turnRate * PLAYER_TURN_SCALE * time the key is held.
// Lower this if a short tap turns too far; raise it if tight corners cannot be made.
constexpr float PLAYER_TURN_SCALE = 0.48f;

constexpr float GRAVITY = 620.0f;
constexpr float RAMP_LIFT = 0.5f;          // launch speed upward, as a fraction of ground speed
constexpr float FALL_TIME = 0.8f;
constexpr float INVULN_TIME = 1.6f;
constexpr float SPIN_TIME = 0.8f;

// Collision response. Raise the restitution values and minimum bounces for a livelier game.
constexpr float RESTITUTION_CAR = 1.1f;    // separation speed as a fraction of closing speed
constexpr float MIN_CAR_BOUNCE = 120.0f;   // even a gentle nudge pushes cars this fast apart (px/s)
constexpr float MIN_CLOSING = 30.0f;       // below this closing speed the bounce is only proportional
constexpr float RESTITUTION_WALL = 0.8f;
constexpr float WALL_HIT_SPEED = 40.0f;    // slower contact than this counts as a scrape
constexpr float WALL_MIN_BOUNCE = 90.0f;
constexpr float WALL_SCRAPE = 0.97f;       // speed kept along a wall on each contact
constexpr float KNOCK_TIME = 0.35f;        // how long a hit breaks the tyres' grip (s)
constexpr float KNOCK_GRIP = 0.12f;        // grip left at the moment of impact
constexpr float KNOCK_THRUST = 0.35f;      // throttle left at the moment of impact
constexpr float YAW_SCALE = 0.003f;        // how much an off-centre hit twists a car
constexpr float YAW_MAX = 5.0f;            // rad/s
constexpr float YAW_DECAY = 7.0f;          // 1/s
constexpr float MAX_SPEED_AFTER_HIT = 1.4f;   // as a fraction of top speed
constexpr int SOLVER_PASSES = 4;           // lets a push ripple through a pile of cars
constexpr float SEPARATION_SKIN = 0.25f;   // leaves separated cars this far apart (px)
constexpr float RESPAWN_CLEARANCE = 5.0f;  // margin around a respawning car (px)
constexpr float WEDGED_TIME = 0.8f;        // a car stuck in an object this long is rescued (s)
constexpr float MAX_AIR_GAP = 8.0f;        // cars further apart in height than this pass over each other

// Network play
constexpr int CONTACT_WINDOW = 6;          // ticks within which two reports of one pair's bump are the same bump
constexpr float MAX_COAST = 0.4f;          // a remote car with no news for this long stops being extrapolated (s)
constexpr float MAX_TURN_RATE = 20.0f;     // rad/s; caps extrapolated spin, e.g. after a respawn resets the heading

constexpr int PROGRESS_WINDOW = 40;
constexpr float STRAY_MARGIN = Track::APRON + 8.0f;
constexpr float OFFSCREEN_MARGIN = 14.0f;

struct SurfaceFx { float accel, grip, drag; };

SurfaceFx surfaceFx(Surface s, const Track& t, const VehicleSpec& v) {
    switch (s) {
    case Surface::Rough: {
        float k = v.roughScale;
        return {1.0f - 0.45f * k, 1.0f - 0.3f * k, 1.3f * k};
    }
    case Surface::Slick: return {0.6f, 0.15f, 0.0f};
    case Surface::Sticky: return {0.55f, 1.2f, 2.4f};
    default: return {1.0f, t.theme().roadGrip, 0.0f};
    }
}

float quantize(float a) {
    float step = TAU / HEADING_STEPS;
    return std::round(a / step) * step;
}

float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

struct CarBox {
    Vec2 c, ax, ay;
    float hx, hy;
};

CarBox carBox(const Car& car, float inflate = 0) {
    CarBox b;
    b.c = car.pos;
    b.ax = fromAngle(car.heading);
    b.ay = {-b.ax.y, b.ax.x};
    b.hx = car.spec->halfLength + inflate;
    b.hy = car.spec->halfWidth + inflate;
    return b;
}

float projectedRadius(const CarBox& b, Vec2 axis) {
    return std::abs(dot(axis, b.ax)) * b.hx + std::abs(dot(axis, b.ay)) * b.hy;
}

// Separating-axis test. On overlap, n points from a to b and depth is how far to push them apart.
bool boxesOverlap(const CarBox& a, const CarBox& b, Vec2& n, float& depth) {
    const Vec2 axes[4] = {a.ax, a.ay, b.ax, b.ay};
    const Vec2 d = b.c - a.c;
    depth = 1e9f;
    for (Vec2 axis : axes) {
        float sep = dot(d, axis);
        float overlap = projectedRadius(a, axis) + projectedRadius(b, axis) - std::abs(sep);
        if (overlap <= 0) return false;
        if (overlap < depth) { depth = overlap; n = sep < 0 ? axis * -1.0f : axis; }
    }
    return true;
}

// The point on a box furthest along dir. Soft near the face directions, so two cars meeting
// square-on get a contact point on their centre lines rather than on an arbitrary corner.
Vec2 supportPoint(const CarBox& b, Vec2 dir) {
    return b.c + b.ax * (b.hx * clampf(dot(b.ax, dir) * 3.0f, -1, 1)) + b.ay * (b.hy * clampf(dot(b.ay, dir) * 3.0f, -1, 1));
}

// Calls visit with points around the outline of the car's box.
template <class F>
void forEachProbe(const Car& c, Vec2 pos, F&& visit) {
    const Vec2 ax = fromAngle(c.heading), ay{-ax.y, ax.x};
    const float hx = c.spec->halfLength, hy = c.spec->halfWidth;
    for (float s : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
        visit(pos + ax * (hx * s) + ay * hy);
        visit(pos + ax * (hx * s) - ay * hy);
    }
    visit(pos + ax * hx);
    visit(pos - ax * hx);
}

void applyKnock(Car& c, float amount) { c.knock = std::max(c.knock, KNOCK_TIME * amount); }
void addYaw(Car& c, float torque) { c.yawKick = clampf(c.yawKick + torque * YAW_SCALE, -YAW_MAX, YAW_MAX); }

void limitSpeed(Car& c) {
    const float limit = c.spec->topSpeed * MAX_SPEED_AFTER_HIT;
    const float s = len(c.vel);
    if (s > limit) c.vel = c.vel * (limit / s);
}
}  // namespace

float carOverlapDepth(const Car& a, const Car& b) {
    Vec2 n;
    float depth;
    return boxesOverlap(carBox(a), carBox(b), n, depth) ? depth : 0.0f;
}

Race::Race(const Track& track, const RaceConfig& cfg) : track_(track), cfg_(cfg), rng_(cfg.seed) {
    const VehicleSpec& spec = vehicleSpec(track.def().vehicle);
    cfg_.numCars = std::max(1, std::min(cfg_.numCars, MAX_CARS));
    cars_.resize(cfg_.numCars);
    for (auto& row : lastContact_)
        for (int& t : row) t = -1000;
    const float hw = track.halfWidth();
    int localHumans = 0;
    for (int i = 0; i < cfg_.numCars; ++i) {
        Car& c = cars_[i];
        c.id = i;
        c.human = i < cfg.humans;
        c.driver = cfg.driver[i];
        c.remote = c.driver != cfg.self;
        c.humanIndex = c.human && !c.remote ? localHumans++ : -1;
        networked_ = networked_ || c.remote;
        c.color = cfg.colors[i];
        c.spec = &spec;
        c.aiBias = (float)((i % 3) - 1) * 8.0f;

        // Two-by-two grid behind the start line, with the humans at the back in a circuit race.
        int slot = cfg_.mode == RaceMode::Circuit ? cfg_.numCars - 1 - i : i;
        int back = 8 + (slot / 2) * 12 + (slot % 2) * 5;
        float lateral = cfg_.numCars == 1 ? 0.0f : ((slot % 2) ? hw * 0.42f : -hw * 0.42f);
        int idx = track.wrap(-back);
        c.idx = idx;
        placeCar(c, idx, lateral);
        c.invuln = 0;
        c.progress = -back * Track::STEP;
        c.lapsDone = -1;
    }

    // The camera follows this machine's player, or failing that the first car driven here.
    focus_ = -1;
    for (const Car& c : cars_) if (focus_ < 0 && c.human && !c.remote) focus_ = c.id;
    for (const Car& c : cars_) if (focus_ < 0 && !c.remote) focus_ = c.id;
    if (focus_ < 0) focus_ = 0;

    buildSpeedProfile();
    updateCamera(true);
}

void Race::placeCar(Car& c, int idx, float lateral) {
    idx = track_.wrap(idx);
    const Sample& s = track_.samples()[idx];

    int delta = track_.wrap(idx - c.idx);
    if (delta > track_.count() / 2) delta -= track_.count();
    c.progress += delta * Track::STEP;

    c.idx = idx;
    c.safeIdx = idx;
    c.pos = s.pos + s.normal * lateral;
    c.vel = {};
    c.heading = std::atan2(s.tan.y, s.tan.x);
    c.z = c.vz = 0;
    c.wasOnRamp = false;
    c.state = CarState::Drive;
    c.spin = 0;
    c.sinking = false;
    c.invuln = INVULN_TIME;
    c.knock = 0;
    c.yawKick = 0;
    c.wedged = 0;
}

void Race::buildSpeedProfile() {
    const VehicleSpec& v = vehicleSpec(track_.def().vehicle);
    int n = track_.count();
    speedProfile_.assign(n, v.topSpeed);
    float lateral = v.grip * v.topSpeed * 0.42f * track_.theme().roadGrip;
    for (int i = 0; i < n; ++i) {
        float k = 0;
        for (int j = -4; j <= 4; ++j) k = std::max(k, std::abs(track_.samples()[track_.wrap(i + j)].lineCurv));
        if (k > 1e-4f) speedProfile_[i] = std::min(v.topSpeed, std::sqrt(lateral / k));
        if (track_.inJumpZone(i)) speedProfile_[i] = v.topSpeed;
    }
    float decel = v.brake * 0.55f * track_.theme().roadGrip;
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = n - 1; i >= 0; --i) {
            if (track_.inJumpZone(i)) continue;
            float next = speedProfile_[track_.wrap(i + 1)];
            speedProfile_[i] = std::min(speedProfile_[i], std::sqrt(next * next + 2 * decel * Track::STEP));
        }
    }
}

// ---------------------------------------------------------------- tick

void Race::step(const Input inputs[2]) {
    events_.clear();
    events_.insert(events_.end(), pendingEvents_.begin(), pendingEvents_.end());
    pendingEvents_.clear();
    out_.clear();
    ++tick_;
    advance(inputs);
    if (networked_) collectNews();
}

void Race::advance(const Input inputs[2]) {
    if (phase_ == Phase::Countdown) {
        timer_ -= DT;
        int light = (int)std::ceil(timer_ / (COUNTDOWN_TIME / 4.0f));
        if (light < lastBeep_ && light > 0) { emit(EventType::Beep, cam_, -1); lastBeep_ = light; }
        if (timer_ <= 0) {
            phase_ = Phase::Running;
            emit(EventType::Go, cam_, -1);
            roundGrace_ = 1.5f;
        }
        updateCamera(false);
        return;
    }

    if (phase_ == Phase::RoundPause) {
        timer_ -= DT;
        if (timer_ <= 0) {
            if (matchWinner_ >= 0) { phase_ = Phase::Finished; sinceFinished_ = 0; }
            else resetRound();
        }
        return;
    }

    if (phase_ == Phase::Running) clock_ += DT;
    else sinceFinished_ += DT;

    updateRubberBand();
    for (Car& c : cars_) {
        if (c.remote) {
            // Driven elsewhere: carry on from its last known state until the next one arrives.
            c.sinceUpdate += DT;
            if (c.sinceUpdate <= MAX_COAST) coast(c, DT);
            continue;
        }
        Input in;
        bool humanDriving = c.human && !c.finished && phase_ == Phase::Running;
        in = humanDriving ? inputs[c.humanIndex] : aiInput(c);
        const float before = c.heading;
        stepCar(c, in);
        c.turnRate = clampf((c.heading - before) / DT, -MAX_TURN_RATE, MAX_TURN_RATE);
    }
    collideCars();
    stepShells();

    if (cfg_.mode == RaceMode::HeadToHead) {
        if (phase_ == Phase::Running) updateHeadToHead();
    } else if (referee()) {
        assignPlaces();
        if (phase_ == Phase::Running) checkRaceEnd();
    }
    updateCamera(false);
}

// Moves a remote car on as if nothing changes: straight-line motion, steady turning, ballistic jumps.
void Race::coast(Car& c, float t) {
    c.pos += c.vel * t;
    if (c.state == CarState::Drive) c.heading += c.turnRate * t;
    if (c.z > 0 || c.vz != 0) {
        c.z += c.vz * t - 0.5f * GRAVITY * t * t;
        c.vz -= GRAVITY * t;
        if (c.z <= 0) { c.z = 0; c.vz = 0; }
    }
    if (t > 0) {
        c.invuln = std::max(0.0f, c.invuln - t);
        c.spin = std::max(0.0f, c.spin - t);
        c.knock = std::max(0.0f, c.knock - t);
        c.bumpSound = std::max(0.0f, c.bumpSound - t);
        if (c.state == CarState::Fall) c.fallTimer = std::max(0.0f, c.fallTimer - t);
    }
}

void Race::stepCar(Car& c, const Input& inRaw) {
    const VehicleSpec& v = *c.spec;
    c.invuln = std::max(0.0f, c.invuln - DT);
    c.gunCooldown = std::max(0.0f, c.gunCooldown - DT);
    c.bumpSound = std::max(0.0f, c.bumpSound - DT);
    const float knocked = clampf(c.knock / KNOCK_TIME, 0, 1);
    c.knock = std::max(0.0f, c.knock - DT);
    c.lapTime += (c.finished || phase_ != Phase::Running) ? 0.0f : DT;

    if (c.state == CarState::Fall) {
        c.fallTimer -= DT;
        c.pos += c.vel * DT;
        c.vel = c.vel * 0.94f;
        if (c.fallTimer <= 0) respawn(c);
        return;
    }

    Input in = inRaw;
    if (c.spin > 0) {
        c.spin -= DT;
        c.heading += 14.0f * DT;
        in = Input{};
    }

    const bool air = c.z > 0;
    const Surface here = track_.at(c.pos);
    SurfaceFx fx = surfaceFx(air ? Surface::Road : here, track_, v);

    // Steering rotates the car directly, at any speed, and in the air too.
    int steer = (in.right ? 1 : 0) - (in.left ? 1 : 0);
    const bool playerSteering = c.human && !c.finished && phase_ == Phase::Running;
    c.heading += steer * v.turnRate * (playerSteering ? PLAYER_TURN_SCALE : 1.0f) * DT;
    c.heading += c.yawKick * DT;
    c.yawKick *= std::exp(-YAW_DECAY * DT);
    if (std::abs(c.yawKick) < 0.02f) c.yawKick = 0;
    float h = playerSteering ? c.heading : quantize(c.heading);
    Vec2 f = fromAngle(h), r{-f.y, f.x};

    c.skid = 0;
    if (!air) {
        float vf = dot(c.vel, f), vl = dot(c.vel, r);
        float accel = v.accel * fx.accel * c.speedMul * lerpf(1.0f, KNOCK_THRUST, knocked);
        float drag = v.accel / v.topSpeed;
        if (in.gas) {
            vf += accel * DT;
        } else if (in.brake) {
            if (vf > 6.0f) vf -= v.brake * DT;
            else if (vf > -v.topSpeed * 0.4f) vf -= v.accel * 0.6f * DT;
        } else {
            vf -= vf * 0.5f * DT;  // engine braking while coasting
        }
        vf -= vf * (drag + fx.drag) * DT;
        vl *= std::exp(-v.grip * fx.grip * lerpf(1.0f, KNOCK_GRIP, knocked) * DT);   // a hit breaks the tyres loose
        c.vel = f * vf + r * vl;
        c.skid = std::abs(vl);
    }

    Vec2 prev = c.pos;
    c.pos += c.vel * DT;
    if (!air) {
        resolveWalls(c, prev);
        if (c.wedged > WEDGED_TIME) { ++c.rescues; respawn(c); return; }
    }

    if (c.z > 0 || c.vz != 0) {
        c.vz -= GRAVITY * DT;
        c.z += c.vz * DT;
        if (c.z <= 0) {
            if (c.vz < -60) emit(EventType::Land, c.pos, c.id, -c.vz / 200.0f);
            c.z = 0;
            c.vz = 0;
        }
    }

    Surface now = track_.at(c.pos);
    c.surface = now;
    bool onRamp = c.z <= 0 && now == Surface::Ramp;
    float speed = len(c.vel);
    if (c.wasOnRamp && !onRamp && speed > 60 && dot(c.vel, f) > 0) {
        c.vz = speed * RAMP_LIFT;
        c.z = 0.5f;
        emit(EventType::Jump, c.pos, c.id, speed / v.topSpeed);
    }
    c.wasOnRamp = onRamp;

    updateProgress(c);

    if (c.z <= 0) {
        if (now == Surface::Void) { startFall(c, false); return; }
        if (now == Surface::Water) { startFall(c, true); return; }
        const Sample& s = track_.samples()[c.idx];
        float dist = len(c.pos - s.pos);
        if ((now == Surface::Road || now == Surface::Slick) && dist <= track_.halfWidth())
            c.safeIdx = c.idx;
        if (dist > track_.halfWidth() + STRAY_MARGIN) {
            ++c.strays;
            respawn(c);
            return;
        }
    }

    if (in.fire && v.hasGun && c.gunCooldown <= 0) {
        c.gunCooldown = 0.8f;
        Vec2 dir = fromAngle(h);
        Shell s{c.pos + dir * (v.radius + 8), dir * 380.0f + c.vel * 0.3f, 1.2f, c.id, (c.id << 24) | (shellSeq_++ & 0xFFFFFF)};
        shells_.push_back(s);
        if (networked_) out_.shots.push_back({s.id, s.owner, s.pos, s.vel, s.life});
        emit(EventType::Shot, c.pos, c.id);
    }
}

bool Race::touchesWall(const Car& c, Vec2 pos) const {
    bool hit = false;
    forEachProbe(c, pos, [&](Vec2 p) { hit = hit || track_.at(p) == Surface::Wall; });
    return hit;
}

void Race::resolveWalls(Car& c, Vec2 prevPos) {
    Vec2 towardFree, towardCar, hitSum;
    int hits = 0;
    forEachProbe(c, c.pos, [&](Vec2 p) {
        if (track_.at(p) != Surface::Wall) return;
        ++hits;
        hitSum += p;
        towardCar += c.pos - p;
        for (int a = 0; a < 12; ++a) {
            Vec2 d = fromAngle(TAU * a / 12);
            if (track_.at(p + d * 7.0f) != Surface::Wall) towardFree += d;
        }
    });
    if (hits == 0) { c.wedged = 0; return; }

    // Use the real way out of the object. A thin object has free space on both sides,
    // so in that case fall back on the direction back towards the car.
    Vec2 n;
    if (len2(towardFree) > 1.0f && dot(towardFree, towardCar) > 0) n = norm(towardFree);
    else if (len2(towardCar) > 1e-3f) n = norm(towardCar);
    else { c.pos = prevPos; c.vel = c.vel * -0.3f; c.wedged += DT; return; }

    const Vec2 start = c.pos;
    bool clear = false;
    for (int i = 0; i < 14 && !clear; ++i) {
        c.pos += n;
        clear = !touchesWall(c, c.pos);
    }
    if (clear) {
        c.wedged = 0;
    } else {
        c.pos = touchesWall(c, prevPos) ? start : prevPos;
        c.wedged += DT;
    }

    const float vn = dot(c.vel, n);
    if (vn >= 0) return;
    const Vec2 tangent = c.vel - n * vn;
    const bool realHit = vn < -WALL_HIT_SPEED;
    float out = -vn * RESTITUTION_WALL;
    if (realHit) out = std::max(out, WALL_MIN_BOUNCE);
    c.vel = tangent * WALL_SCRAPE + n * out;
    if (!realHit) return;

    applyKnock(c, 0.7f);
    addYaw(c, cross(hitSum * (1.0f / hits) - c.pos, n) * (out - vn));
    limitSpeed(c);
    if (c.bumpSound <= 0) {
        c.bumpSound = 0.1f;
        emit(EventType::Wall, c.pos, c.id, -vn / c.spec->topSpeed);
    }
}

void Race::startFall(Car& c, bool sink) {
    c.state = CarState::Fall;
    c.fallTimer = FALL_TIME;
    c.sinking = sink;
    c.wasOnRamp = false;
    ++c.falls;
    emit(sink ? EventType::Splash : EventType::Fall, c.pos, c.id);
}

bool Race::spotIsClear(const Car& c, int idx, float lateral) const {
    const Sample& s = track_.samples()[track_.wrap(idx)];
    Car probe = c;
    probe.pos = s.pos + s.normal * lateral;
    probe.heading = std::atan2(s.tan.y, s.tan.x);
    const Surface ground = track_.at(probe.pos);
    if (ground == Surface::Void || ground == Surface::Water || touchesWall(probe, probe.pos)) return false;

    const CarBox box = carBox(probe, RESPAWN_CLEARANCE);
    for (const Car& o : cars_) {
        if (o.id == c.id || o.state != CarState::Drive || o.z > MAX_AIR_GAP) continue;
        Vec2 n;
        float depth;
        if (boxesOverlap(box, carBox(o), n, depth)) return false;
    }
    return true;
}

void Race::respawn(Car& c) {
    // Start on the racing line, which is clear of props, and look nearby if a car is parked there.
    const int base = track_.respawnIndex(c.safeIdx);
    const float limit = track_.halfWidth() - 12.0f;
    int idx = base;
    float lateral = track_.samples()[track_.wrap(base)].line;
    bool found = false;
    for (int back = 0; back <= 48 && !found; back += 12) {
        const int i = track_.wrap(base - back);
        const float line = track_.samples()[i].line;
        for (float offset : {0.0f, 24.0f, -24.0f, 44.0f, -44.0f}) {
            const float candidate = clampf(line + offset, -limit, limit);
            if (spotIsClear(c, i, candidate)) { idx = i; lateral = candidate; found = true; break; }
        }
    }

    int prevLaps = c.lapsDone;
    placeCar(c, idx, lateral);
    c.lapsDone = prevLaps;
    emit(EventType::Respawn, c.pos, c.id);
}

void Race::updateProgress(Car& c) {
    int idx = track_.nearest(c.pos, c.idx, PROGRESS_WINDOW);
    int delta = idx - c.idx;
    int n = track_.count();
    if (delta > n / 2) delta -= n;
    if (delta < -n / 2) delta += n;
    c.idx = idx;
    c.progress += delta * Track::STEP;
    if (c.finished) return;   // cars keep circulating after the flag, but their times are final

    float L = track_.length();
    int laps = (int)std::floor(c.progress / L);
    if (laps > c.lapsDone) {
        c.lapsDone = laps;
        if (laps <= 0) { c.lapTime = 0; return; }
        if (c.bestLap <= 0 || c.lapTime < c.bestLap) c.bestLap = c.lapTime;
        c.lapTime = 0;
        if (c.human && laps < cfg_.laps) emit(EventType::Lap, c.pos, c.id);
        if (cfg_.mode != RaceMode::HeadToHead && laps >= cfg_.laps && !c.finished) {
            c.finished = true;   // the referee gives it a place (assignPlaces)
            c.finishTime = clock_;
            emit(EventType::Lap, c.pos, c.id, 2.0f);
        }
    }
}

void Race::collideCars() {
    const int count = (int)cars_.size();

    // Cars driven by this module are solved together. A car driven by another module takes part
    // through a scratch copy: the push and bounce it would get are worked out but thrown away here,
    // because its own module does the same for it when it sees the contact. A bounce is also sent
    // as a contact, so the other module never misses its half because it saw the cars a tick apart.
    Car scratch[MAX_CARS];
    Car* view[MAX_CARS];
    for (int i = 0; i < count; ++i) {
        if (cars_[i].remote) { scratch[i] = cars_[i]; view[i] = &scratch[i]; }
        else view[i] = &cars_[i];
    }

    bool bounced[MAX_CARS][MAX_CARS] = {};   // separating can take several passes, but each pair bounces once a tick
    for (int pass = 0; pass < SOLVER_PASSES; ++pass) {
        bool anyOverlap = false;
        for (int i = 0; i < count; ++i) {
            for (int j = i + 1; j < count; ++j) {
                if (cars_[i].remote && cars_[j].remote) continue;   // their modules handle it
                Car& a = *view[i];
                Car& b = *view[j];
                if (a.state != CarState::Drive || b.state != CarState::Drive) continue;
                if (std::abs(a.z - b.z) > MAX_AIR_GAP) continue;
                Vec2 n;
                float depth;
                if (!boxesOverlap(carBox(a), carBox(b), n, depth)) continue;
                anyOverlap = true;
                separateCars(a, b, n, depth);
                if (bounced[i][j]) continue;
                bounced[i][j] = true;

                if (!cars_[i].remote && !cars_[j].remote) { bounceCars(a, b, n); continue; }
                if ((int)tick_ - lastContact_[i][j] <= CONTACT_WINDOW) continue;   // already bounced, here or there
                Car& other = cars_[i].remote ? a : b;
                const Car& mine = cars_[i].remote ? b : a;
                const Vec2 vel0 = other.vel;
                const float yaw0 = other.yawKick;
                const float closing = dot(a.vel - b.vel, n);
                if (!bounceCars(a, b, n)) continue;
                lastContact_[i][j] = (int)tick_;
                out_.contacts.push_back({mine.id, other.id, other.vel - vel0, other.knock, other.yawKick - yaw0,
                                         (a.pos + b.pos) * 0.5f, closing / 200.0f});
            }
        }
        if (!anyOverlap) break;
    }
}

void Race::separateCars(Car& a, Car& b, Vec2 n, float depth) {
    const float total = a.spec->mass + b.spec->mass;
    const float push = depth + SEPARATION_SKIN;
    Vec2 moveA = n * (-push * b.spec->mass / total);
    Vec2 moveB = n * (push * a.spec->mass / total);

    // A car pinned against an object cannot give way, so the other one moves instead.
    const bool aPinned = touchesWall(a, a.pos + moveA);
    const bool bPinned = touchesWall(b, b.pos + moveB);
    if (aPinned && !bPinned) { moveA = {}; moveB = n * push; }
    else if (bPinned && !aPinned) { moveB = {}; moveA = n * -push; }
    a.pos += moveA;
    b.pos += moveB;
}

bool Race::bounceCars(Car& a, Car& b, Vec2 n) {
    const float closing = dot(a.vel - b.vel, n);
    if (closing <= 0) return false;   // already moving apart

    float separation = closing * RESTITUTION_CAR;
    if (closing > MIN_CLOSING) separation = std::max(separation, MIN_CAR_BOUNCE);
    const float invA = 1.0f / a.spec->mass, invB = 1.0f / b.spec->mass;
    const float impulse = (closing + separation) / (invA + invB);
    a.vel -= n * (impulse * invA);
    b.vel += n * (impulse * invB);

    const float hardness = clampf(closing / 150.0f, 0.4f, 1.0f);
    applyKnock(a, hardness);
    applyKnock(b, hardness);

    const CarBox boxA = carBox(a), boxB = carBox(b);
    const Vec2 contact = (supportPoint(boxA, n) + supportPoint(boxB, n * -1.0f)) * 0.5f;
    addYaw(a, cross(contact - a.pos, n * -1.0f) * (impulse * invA));
    addYaw(b, cross(contact - b.pos, n) * (impulse * invB));

    limitSpeed(a);
    limitSpeed(b);

    if (closing > 20.0f && a.bumpSound <= 0 && b.bumpSound <= 0) {
        a.bumpSound = b.bumpSound = 0.1f;
        emit(EventType::Bump, contact, a.id, closing / 200.0f, b.id);
    }
    return true;
}

void Race::stepShells() {
    for (size_t i = 0; i < shells_.size();) {
        Shell& s = shells_[i];
        s.life -= DT;
        s.pos += s.vel * DT;
        bool dead = s.life <= 0 || track_.at(s.pos) == Surface::Wall;
        if (!dead) {
            for (Car& c : cars_) {
                // Each module decides whether a shell hits the cars it drives, whoever fired it.
                if (c.remote || c.id == s.owner || c.state != CarState::Drive || c.invuln > 0 || c.z > 8) continue;
                if (len2(c.pos - s.pos) < c.spec->radius * c.spec->radius + 16) {
                    c.vel = c.vel * 0.15f + norm(s.vel) * 60.0f;
                    c.spin = SPIN_TIME;
                    c.invuln = SPIN_TIME + 0.5f;   // no chain-stunning a car that was just hit
                    emit(EventType::Hit, c.pos, c.id);
                    if (networked_) out_.shellsGone.push_back(s.id);
                    dead = true;
                    break;
                }
            }
        }
        if (dead) { shells_[i] = shells_.back(); shells_.pop_back(); }
        else ++i;
    }
}

// ---------------------------------------------------------------- computer drivers

void Race::updateRubberBand() {
    float reference = 0;
    bool hasHuman = false;
    for (const Car& c : cars_) {
        if (c.human) { reference = hasHuman ? std::max(reference, c.progress) : c.progress; hasHuman = true; }
    }
    for (Car& c : cars_) {
        if (c.human) { c.speedMul = 1; continue; }
        float mul = cfg_.aiSkill;
        if (hasHuman) mul *= clampf(1.0f + (reference - c.progress) / 2200.0f, 0.90f, 1.10f);
        if (track_.inJumpZone(c.idx)) mul = std::max(mul, 1.0f);   // never let the handicap ground a jump
        c.speedMul = mul;
    }
}

Input Race::aiInput(Car& c) {
    Input in;
    if (c.state != CarState::Drive || c.spin > 0) return in;
    const VehicleSpec& v = *c.spec;
    float speed = len(c.vel);

    int ahead = (int)((26.0f + speed * 0.20f) / Track::STEP);
    int ti = track_.wrap(c.idx + ahead);
    const Sample& t = track_.samples()[ti];
    float lateral = t.line + (track_.inJumpZone(ti) ? 0.0f : c.aiBias);
    float limit = track_.halfWidth() - 12.0f;
    Vec2 target = t.pos + t.normal * clampf(lateral, -limit, limit);
    Vec2 toTarget = target - c.pos;
    float want = std::atan2(toTarget.y, toTarget.x);

    // Aim past the target by the slide angle, so the car's motion, not its nose, hits the line.
    if (speed > 40) {
        float motion = std::atan2(c.vel.y, c.vel.x);
        want += clampf(angleDiff(want, motion), -0.6f, 0.6f) * 0.8f;
    }

    float err = angleDiff(want, c.heading);

    // Back out when wedged against something.
    if (c.aiReverse > 0) {
        c.aiReverse -= DT;
        in.brake = true;
        in.left = err > 0.1f;
        in.right = err < -0.1f;
        return in;
    }
    if (phase_ != Phase::Countdown && speed < 18.0f) {
        c.aiStuck += DT;
        if (c.aiStuck > 1.0f) { c.aiStuck = 0; c.aiReverse = 0.6f; ++c.rescues; }
    } else {
        c.aiStuck = std::max(0.0f, c.aiStuck - DT * 2);
    }

    in.right = err > 0.10f;
    in.left = err < -0.10f;

    float desired = speedProfile_[c.idx] * c.speedMul;
    float forward = dot(c.vel, fromAngle(c.heading));
    if (forward > desired + 14.0f) in.brake = true;
    else if (forward < desired || std::abs(err) < 1.2f) in.gas = forward < desired + 2.0f;

    if (v.hasGun && c.gunCooldown <= 0) {
        Vec2 f = fromAngle(c.heading);
        for (const Car& o : cars_) {
            if (o.id == c.id || o.state != CarState::Drive || o.invuln > 0) continue;
            Vec2 d = o.pos - c.pos;
            float dist = len(d);
            // Computer tanks mostly save their shells for the player, or the pack stalls in a pile-up.
            const uint32_t chance = o.human ? 3u : 1u;
            if (dist < 200.0f && dot(d, f) > dist * 0.97f && (rng_() % 100) < chance) { in.fire = true; break; }
        }
    }
    return in;
}

// ---------------------------------------------------------------- rules

std::vector<int> Race::standings() const {
    std::vector<int> order(cars_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const Car& ca = cars_[a];
        const Car& cb = cars_[b];
        if (ca.place > 0 && cb.place > 0) return ca.place < cb.place;
        if (ca.finished != cb.finished) return ca.finished;
        if (ca.finished) {
            // A module that is not the referee can see a car finish before its place arrives.
            if ((ca.place > 0) != (cb.place > 0)) return ca.place > 0;
            return ca.finishTime < cb.finishTime;
        }
        return ca.progress > cb.progress;
    });
    return order;
}

// The referee places finishers in the order of their own finishing times, which matters when
// another module reports a finish a tick or two after it happened.
void Race::assignPlaces() {
    int fresh[MAX_CARS];
    int n = 0;
    for (const Car& c : cars_) if (c.finished && c.place == 0) fresh[n++] = c.id;
    std::sort(fresh, fresh + n, [&](int a, int b) {
        if (cars_[a].finishTime != cars_[b].finishTime) return cars_[a].finishTime < cars_[b].finishTime;
        return a < b;
    });
    for (int i = 0; i < n; ++i) cars_[fresh[i]].place = nextPlace_++;
}

void Race::checkRaceEnd() {
    int finished = 0, humans = 0, humansLeft = 0;
    for (const Car& c : cars_) {
        finished += c.finished ? 1 : 0;
        humans += c.human ? 1 : 0;   // counted here, because a car whose player leaves becomes a computer car
        humansLeft += (c.human && !c.finished) ? 1 : 0;
    }
    int total = (int)cars_.size();
    bool done;
    if (humans == 0) done = finished == total;
    else done = humansLeft == 0 || (total > humans && finished >= total - humans);
    if (!done) return;

    // Anyone still racing is classified by how far they got.
    for (int id : standings()) {
        Car& c = cars_[id];
        if (!c.finished) { c.place = nextPlace_++; c.finished = c.human ? true : c.finished; }
    }
    phase_ = Phase::Finished;
    sinceFinished_ = 0;
}

void Race::updateCamera(bool snap) {
    int focus = focus_;
    if (cfg_.mode == RaceMode::HeadToHead) focus = leader_;
    const Car& c = cars_[focus];
    Vec2 lead = c.vel * 0.34f;
    float l = len(lead);
    if (l > 95.0f) lead = lead * (95.0f / l);
    if (c.state == CarState::Fall) lead = {};
    Vec2 target = c.pos + lead;
    if (snap) cam_ = target;
    else cam_ += (target - cam_) * 0.11f;

    float hw = VIEW_W * 0.5f, hh = VIEW_H * 0.5f;
    cam_.x = clampf(cam_.x, hw, std::max(hw, track_.width() - hw));
    cam_.y = clampf(cam_.y, hh, std::max(hh, track_.height() - hh));
}

void Race::updateHeadToHead() {
    const int other = 1 - leader_;
    if (cars_[other].progress > cars_[leader_].progress + 14.0f) leader_ = other;

    if (roundGrace_ > 0) { roundGrace_ -= DT; }
    else {
        for (int i = 0; i < 2; ++i) {
            if (i == leader_) continue;
            Vec2 d = cars_[i].pos - cam_;
            if (std::abs(d.x) > VIEW_W * 0.5f + OFFSCREEN_MARGIN || std::abs(d.y) > VIEW_H * 0.5f + OFFSCREEN_MARGIN) {
                award(1 - i);
                return;
            }
        }
    }

    if (cars_[leader_].lapsDone >= cfg_.laps && !suddenDeath_) {
        if (points_[0] != points_[1]) {
            matchWinner_ = points_[0] > points_[1] ? 0 : 1;
            emit(EventType::Point, cam_, matchWinner_);
            phase_ = Phase::RoundPause;
            timer_ = 1.6f;
        } else {
            suddenDeath_ = true;
        }
    }
}

void Race::award(int winner) {
    int loser = 1 - winner;
    points_[winner]++;
    points_[loser]--;
    emit(EventType::Point, cars_[winner].pos, winner);
    if (points_[winner] >= WIN_POINTS || suddenDeath_) matchWinner_ = winner;
    phase_ = Phase::RoundPause;
    timer_ = 1.4f;
}

void Race::resetRound() {
    Car& lead = cars_[leader_];
    Car& trail = cars_[1 - leader_];
    int idx = track_.respawnIndex(lead.safeIdx);
    float line = track_.samples()[track_.wrap(idx)].line;
    placeCar(lead, idx, line - 11.0f);
    int laps = lead.lapsDone;
    float prog = lead.progress;
    placeCar(trail, idx, line + 11.0f);
    trail.progress = prog;
    trail.lapsDone = laps;
    updateCamera(true);
    phase_ = Phase::Running;
    roundGrace_ = 1.5f;
}

// ---------------------------------------------------------------- modules

CarSnapshot Race::snapshot(const Car& c) const {
    CarSnapshot s;
    s.id = c.id;
    s.pos = c.pos;
    s.vel = c.vel;
    s.heading = c.heading;
    s.turnRate = c.turnRate;
    s.z = c.z;
    s.vz = c.vz;
    s.state = c.state;
    s.sinking = c.sinking;
    s.wasOnRamp = c.wasOnRamp;
    s.finished = c.finished;
    s.fallTimer = c.fallTimer;
    s.invuln = c.invuln;
    s.spin = c.spin;
    s.knock = c.knock;
    s.yawKick = c.yawKick;
    s.skid = c.skid;
    s.surface = c.surface;
    s.idx = c.idx;
    s.safeIdx = c.safeIdx;
    s.lapsDone = c.lapsDone;
    s.progress = c.progress;
    s.lapTime = c.lapTime;
    s.bestLap = c.bestLap;
    s.finishTime = c.finishTime;
    return s;
}

void Race::collectNews() {
    out_.from = cfg_.self;
    out_.tick = tick_;
    for (const Car& c : cars_) if (!c.remote) out_.cars.push_back(snapshot(c));

    // Sounds and effects of the cars driven here. A bump with another module's car is left out:
    // that module plays its own when it sees the bump or gets the contact.
    for (const RaceEvent& e : events_) {
        if (e.car < 0 || e.car >= (int)cars_.size() || cars_[e.car].remote) continue;
        if (e.type == EventType::Bump && (e.other < 0 || e.other >= (int)cars_.size() || cars_[e.other].remote)) continue;
        out_.events.push_back(e);
    }

    if (referee()) {
        bool changed = phase_ == Phase::Finished && !reportedOver_;
        for (const Car& c : cars_) changed = changed || c.place != reportedPlaces_[c.id];
        if (changed) {
            out_.hasResult = true;
            out_.over = reportedOver_ = phase_ == Phase::Finished;
            for (const Car& c : cars_) out_.places[c.id] = reportedPlaces_[c.id] = c.place;
        }
    }
}

void Race::apply(const RaceNews& n, float age) {
    // The state left by this call is moved on one tick by the next step, so extrapolate it to a tick
    // before now. Very old news is shown where it was and left to go stale.
    const float ahead = clampf(age - DT, -3 * DT, MAX_COAST);
    for (const CarSnapshot& s : n.cars) {
        if (s.id < 0 || s.id >= (int)cars_.size()) continue;
        if (n.from == snapFrom_[s.id] && n.tick <= snapTick_[s.id]) continue;   // older than what is shown
        snapFrom_[s.id] = n.from;
        snapTick_[s.id] = n.tick;
        applySnapshot(s, ahead);
        cars_[s.id].sinceUpdate = std::max(0.0f, age - DT);
    }
    for (const CarContact& c : n.contacts) applyContact(c);
    for (const ShellShot& s : n.shots) applyShot(s, ahead);
    for (int id : n.shellsGone) removeShell(id);
    for (const RaceEvent& e : n.events) {
        if (e.car >= 0 && e.car < (int)cars_.size() && cars_[e.car].remote) pendingEvents_.push_back(e);
    }
    if (n.hasResult && !referee()) applyResult(n.places, n.over);
}

void Race::applySnapshot(const CarSnapshot& s, float ahead) {
    Car& c = cars_[s.id];
    if (!c.remote) return;   // only this module moves the cars it drives
    const float check[] = {s.pos.x, s.pos.y, s.vel.x, s.vel.y, s.heading, s.turnRate, s.z, s.vz, s.progress};
    for (float f : check) if (!std::isfinite(f)) return;

    c.pos = s.pos;
    c.vel = s.vel;
    c.heading = s.heading;
    c.turnRate = clampf(s.turnRate, -MAX_TURN_RATE, MAX_TURN_RATE);
    c.z = s.z;
    c.vz = s.vz;
    c.state = s.state;
    c.sinking = s.sinking;
    c.wasOnRamp = s.wasOnRamp;
    c.finished = c.finished || s.finished;
    c.fallTimer = s.fallTimer;
    c.invuln = s.invuln;
    c.spin = s.spin;
    c.knock = s.knock;
    c.yawKick = s.yawKick;
    c.skid = s.skid;
    c.surface = s.surface;
    c.idx = track_.wrap(s.idx);
    c.safeIdx = track_.wrap(s.safeIdx);
    c.lapsDone = s.lapsDone;
    c.progress = s.progress;
    c.lapTime = s.lapTime;
    c.bestLap = s.bestLap;
    c.finishTime = s.finishTime;
    coast(c, ahead);
}

void Race::applyContact(const CarContact& ct) {
    const int count = (int)cars_.size();
    if (ct.from < 0 || ct.to < 0 || ct.from >= count || ct.to >= count || ct.from == ct.to) return;
    const int i = std::min(ct.from, ct.to), j = std::max(ct.from, ct.to);
    if ((int)tick_ - lastContact_[i][j] <= CONTACT_WINDOW) return;   // this module already bounced them
    lastContact_[i][j] = (int)tick_;

    Car& c = cars_[ct.to];
    if (!c.remote && std::isfinite(ct.dvel.x) && std::isfinite(ct.dvel.y)) {
        c.vel += ct.dvel;
        c.knock = std::max(c.knock, std::min(ct.knock, KNOCK_TIME));
        c.yawKick = clampf(c.yawKick + ct.yaw, -YAW_MAX, YAW_MAX);
        limitSpeed(c);
    }
    if (ct.strength > 0.1f) pendingEvents_.push_back({EventType::Bump, ct.pos, ct.to, ct.strength, ct.from});
}

void Race::applyShot(const ShellShot& s, float ahead) {
    if (s.owner < 0 || s.owner >= (int)cars_.size() || !cars_[s.owner].remote) return;
    for (const Shell& x : shells_) if (x.id == s.id) return;
    Shell shell{s.pos + s.vel * ahead, s.vel, s.life - ahead, s.owner, s.id};
    if (shell.life > 0) shells_.push_back(shell);
}

void Race::removeShell(int id) {
    for (size_t i = 0; i < shells_.size(); ++i) {
        if (shells_[i].id != id) continue;
        shells_[i] = shells_.back();
        shells_.pop_back();
        return;
    }
}

void Race::applyResult(const int places[MAX_CARS], bool over) {
    for (Car& c : cars_) {
        if (places[c.id] > 0) c.place = places[c.id];
        if (over && c.human && !c.remote) c.finished = true;   // the race is over: stop taking keys
    }
    if (over && phase_ != Phase::Finished) {
        phase_ = Phase::Finished;
        sinceFinished_ = 0;
    }
}

void Race::takeOver(int car) {
    if (car < 0 || car >= (int)cars_.size() || !cars_[car].remote) return;
    Car& c = cars_[car];
    c.remote = false;
    c.driver = cfg_.self;
    cfg_.driver[car] = cfg_.self;
    c.human = false;
    c.humanIndex = -1;
    c.aiStuck = c.aiReverse = 0;
}
