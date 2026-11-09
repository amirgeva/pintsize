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
constexpr float RESTITUTION_CAR = 0.9f;
constexpr float RESTITUTION_WALL = 0.45f;
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
}  // namespace

Race::Race(const Track& track, const RaceConfig& cfg) : track_(track), cfg_(cfg), rng_(cfg.seed) {
    const VehicleSpec& spec = vehicleSpec(track.def().vehicle);
    cars_.resize(cfg.numCars);
    const float hw = track.halfWidth();
    for (int i = 0; i < cfg.numCars; ++i) {
        Car& c = cars_[i];
        c.id = i;
        c.human = i < cfg.humans;
        c.humanIndex = c.human ? i : -1;
        c.color = cfg.colors[i];
        c.spec = &spec;
        c.aiBias = (float)((i % 3) - 1) * 8.0f;

        // Two-by-two grid behind the start line, with the humans at the back in a circuit race.
        int slot = cfg.mode == RaceMode::Circuit ? cfg.numCars - 1 - i : i;
        int back = 8 + (slot / 2) * 12 + (slot % 2) * 5;
        float lateral = cfg.numCars == 1 ? 0.0f : ((slot % 2) ? hw * 0.42f : -hw * 0.42f);
        int idx = track.wrap(-back);
        c.idx = idx;
        placeCar(c, idx, lateral);
        c.invuln = 0;
        c.progress = -back * Track::STEP;
        c.lapsDone = -1;
    }
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
        Input in;
        bool humanDriving = c.human && !c.finished && phase_ == Phase::Running;
        in = humanDriving ? inputs[c.humanIndex] : aiInput(c);
        stepCar(c, in);
    }
    collideCars();
    stepShells();

    if (cfg_.mode == RaceMode::HeadToHead) {
        if (phase_ == Phase::Running) updateHeadToHead();
    } else if (phase_ == Phase::Running) {
        checkRaceEnd();
    }
    updateCamera(false);
}

void Race::stepCar(Car& c, const Input& inRaw) {
    const VehicleSpec& v = *c.spec;
    c.invuln = std::max(0.0f, c.invuln - DT);
    c.gunCooldown = std::max(0.0f, c.gunCooldown - DT);
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
    float h = playerSteering ? c.heading : quantize(c.heading);
    Vec2 f = fromAngle(h), r{-f.y, f.x};

    c.skid = 0;
    if (!air) {
        float vf = dot(c.vel, f), vl = dot(c.vel, r);
        float accel = v.accel * fx.accel * c.speedMul;
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
        vl *= std::exp(-v.grip * fx.grip * DT);
        c.vel = f * vf + r * vl;
        c.skid = std::abs(vl);
    }

    Vec2 prev = c.pos;
    c.pos += c.vel * DT;
    if (!air) resolveWalls(c, prev);

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
        shells_.push_back({c.pos + dir * (v.radius + 8), dir * 380.0f + c.vel * 0.3f, 1.2f, c.id});
        emit(EventType::Shot, c.pos, c.id);
    }
}

void Race::resolveWalls(Car& c, Vec2 prevPos) {
    static const int PROBES = 8;
    const float r = c.spec->radius;
    auto gather = [&](Vec2 p, Vec2& away) {
        away = {};
        bool hit = false;
        for (int i = 0; i < PROBES; ++i) {
            Vec2 d = fromAngle(TAU * i / PROBES);
            if (track_.at(p + d * r) == Surface::Wall) { away -= d; hit = true; }
        }
        return hit;
    };

    Vec2 away;
    if (!gather(c.pos, away)) return;
    if (len2(away) < 1e-3f) { c.pos = prevPos; c.vel = c.vel * -0.3f; return; }

    Vec2 n = norm(away);
    for (int i = 0; i < 8; ++i) {
        c.pos += n;
        Vec2 tmp;
        if (!gather(c.pos, tmp)) break;
    }
    float vn = dot(c.vel, n);
    if (vn < 0) {
        Vec2 tangent = c.vel - n * vn;
        c.vel = tangent * 0.92f - n * (vn * RESTITUTION_WALL);
        if (vn < -40) emit(EventType::Wall, c.pos, c.id, -vn / c.spec->topSpeed);
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

void Race::respawn(Car& c) {
    int idx = track_.respawnIndex(c.safeIdx);
    int prevLaps = c.lapsDone;
    placeCar(c, idx, track_.samples()[track_.wrap(idx)].line);   // the line is clear of props
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
            c.finished = true;
            c.finishTime = clock_;
            c.place = nextPlace_++;
            emit(EventType::Lap, c.pos, c.id, 2.0f);
        }
    }
}

void Race::collideCars() {
    for (size_t i = 0; i < cars_.size(); ++i) {
        for (size_t j = i + 1; j < cars_.size(); ++j) {
            Car& a = cars_[i];
            Car& b = cars_[j];
            if (a.state != CarState::Drive || b.state != CarState::Drive) continue;
            if (a.invuln > 0 || b.invuln > 0) continue;
            if (std::abs(a.z - b.z) > 8.0f) continue;
            Vec2 d = b.pos - a.pos;
            float minD = a.spec->radius + b.spec->radius;
            float dist2 = len2(d);
            if (dist2 >= minD * minD || dist2 < 1e-4f) continue;
            float dist = std::sqrt(dist2);
            Vec2 n = d * (1.0f / dist);
            float total = a.spec->mass + b.spec->mass;
            float overlap = minD - dist;
            a.pos -= n * (overlap * b.spec->mass / total);
            b.pos += n * (overlap * a.spec->mass / total);
            float rel = dot(b.vel - a.vel, n);
            if (rel < 0) {
                float impulse = -(1 + RESTITUTION_CAR) * rel / (1 / a.spec->mass + 1 / b.spec->mass);
                a.vel -= n * (impulse / a.spec->mass);
                b.vel += n * (impulse / b.spec->mass);
                if (rel < -20) emit(EventType::Bump, (a.pos + b.pos) * 0.5f, (int)i, -rel / 200.0f);
            }
        }
    }
}

void Race::stepShells() {
    for (size_t i = 0; i < shells_.size();) {
        Shell& s = shells_[i];
        s.life -= DT;
        s.pos += s.vel * DT;
        bool dead = s.life <= 0 || track_.at(s.pos) == Surface::Wall;
        if (!dead) {
            for (Car& c : cars_) {
                if (c.id == s.owner || c.state != CarState::Drive || c.invuln > 0 || c.z > 8) continue;
                if (len2(c.pos - s.pos) < c.spec->radius * c.spec->radius + 16) {
                    c.vel = c.vel * 0.15f + norm(s.vel) * 60.0f;
                    c.spin = SPIN_TIME;
                    c.invuln = SPIN_TIME + 0.5f;   // no chain-stunning a car that was just hit
                    emit(EventType::Hit, c.pos, c.id);
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
            if (dist < 200.0f && dot(d, f) > dist * 0.97f && (rng_() % 100) < 3) { in.fire = true; break; }
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
        if (ca.finished != cb.finished) return ca.finished;
        if (ca.finished) return ca.place < cb.place;
        return ca.progress > cb.progress;
    });
    return order;
}

void Race::checkRaceEnd() {
    int finished = 0, humansLeft = 0;
    for (const Car& c : cars_) {
        finished += c.finished ? 1 : 0;
        humansLeft += (c.human && !c.finished) ? 1 : 0;
    }
    int total = (int)cars_.size();
    bool done;
    if (cfg_.humans == 0) done = finished == total;
    else done = humansLeft == 0 || (total > cfg_.humans && finished >= total - cfg_.humans);
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
    int focus = 0;
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
