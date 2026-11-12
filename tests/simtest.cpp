// Headless check that computer drivers can complete every track and that the game modes terminate.
#include "link.h"
#include "race.h"
#include <algorithm>
#include <cstdio>

static const float MAX_ALLOWED_OVERLAP = 1.5f;   // px; about what rounding to screen pixels hides

// The deepest overlap between any two cars that are at the same height and still in play.
static float worstOverlap(const Race& race) {
    float worst = 0;
    const auto& cars = race.cars();
    for (size_t i = 0; i < cars.size(); ++i) {
        for (size_t j = i + 1; j < cars.size(); ++j) {
            if (cars[i].state != CarState::Drive || cars[j].state != CarState::Drive) continue;
            if (std::abs(cars[i].z - cars[j].z) > 8.0f) continue;
            worst = std::max(worst, carOverlapDepth(cars[i], cars[j]));
        }
    }
    return worst;
}

static int runCircuit(const TrackDef& def, const Track& track, float skill) {
    RaceConfig cfg;
    cfg.humans = 0;
    cfg.aiSkill = skill;
    cfg.laps = def.laps;
    Race race(track, cfg);
    Input none[2];
    int ticks = 0;
    const int limit = 60 * 60 * 6;
    float deepest = 0;
    int bumps = 0, hits = 0;
    while (race.phase() != Phase::Finished && ticks < limit) {
        race.step(none);
        ++ticks;
        deepest = std::max(deepest, worstOverlap(race));
        for (const RaceEvent& e : race.events()) {
            bumps += e.type == EventType::Bump ? 1 : 0;
            hits += e.type == EventType::Hit ? 1 : 0;
        }
    }

    int failures = 0;
    printf("  skill %.2f (deepest overlap %.2f px, %d bumps, %d shell hits%s):", skill, deepest, bumps, hits, deepest > MAX_ALLOWED_OVERLAP ? "  WRONG" : "");
    if (deepest > MAX_ALLOWED_OVERLAP) ++failures;
    for (const Car& c : race.cars()) {
        printf("  [%6.1fs best %5.1f F%d S%d R%d]", c.finished ? c.finishTime : -1.0f, c.bestLap, c.falls, c.strays, c.rescues);
        if (!c.finished) ++failures;
    }
    printf("%s\n", failures ? "  DID NOT FINISH" : "");
    return failures;
}

static int runHeadToHead(const Track& track) {
    RaceConfig cfg;
    cfg.mode = RaceMode::HeadToHead;
    cfg.numCars = 2;
    cfg.humans = 0;
    cfg.laps = 3;
    Race race(track, cfg);
    Input none[2];
    int ticks = 0;
    const int limit = 60 * 60 * 15;
    while (race.phase() != Phase::Finished && ticks < limit) { race.step(none); ++ticks; }
    printf("  head-to-head: winner %d, points %d-%d, %.1fs%s\n", race.matchWinner(), race.points(0), race.points(1), ticks * DT,
           race.suddenDeath() ? " (sudden death)" : "");
    // Two identical computer cars can legitimately stay level in sudden death for ever.
    return race.phase() == Phase::Finished || race.suddenDeath() ? 0 : 1;
}

// A player who never touches the keys must lose every point to the computer.
static int runIdleHeadToHead(const Track& track) {
    RaceConfig cfg;
    cfg.mode = RaceMode::HeadToHead;
    cfg.numCars = 2;
    cfg.humans = 1;
    cfg.laps = 3;
    Race race(track, cfg);
    Input none[2];
    int ticks = 0;
    while (race.phase() != Phase::Finished && ticks < 60 * 60 * 5) { race.step(none); ++ticks; }
    bool ok = race.matchWinner() == 1 && race.points(0) == 0 && race.points(1) == Race::WIN_POINTS;
    printf("  idle player head-to-head: winner %d, points %d-%d %s\n", race.matchWinner(), race.points(0), race.points(1), ok ? "" : " WRONG");
    return ok ? 0 : 1;
}

// A player who never moves must be classified last once the computers finish.
static int runIdleCircuit(const TrackDef& def, const Track& track) {
    RaceConfig cfg;
    cfg.humans = 1;
    cfg.laps = def.laps;
    Race race(track, cfg);
    Input none[2];
    int ticks = 0;
    while (race.phase() != Phase::Finished && ticks < 60 * 60 * 6) { race.step(none); ++ticks; }
    auto order = race.standings();
    bool ok = race.phase() == Phase::Finished && order.back() == 0;
    printf("  idle player circuit: last place is car %d %s\n", order.back(), ok ? "" : " WRONG");
    return ok ? 0 : 1;
}

static int respawnCount(const Race& race) {
    return race.cars()[0].falls + race.cars()[0].strays;
}

// Respawning resets the heading, so a measurement during which the car left the track is ignored.
static int runSteeringFeel(int trackIndex) {
    const TrackDef& def = allTracks()[trackIndex];
    printf(" on %s\n", def.name);
    Track track(def);
    RaceConfig cfg;
    cfg.mode = RaceMode::TimeTrial;
    cfg.numCars = 1;
    cfg.humans = 1;
    Race race(track, cfg);
    Input in[2];
    while (race.phase() == Phase::Countdown) race.step(in);
    in[0].gas = true;
    for (int i = 0; i < 90; ++i) race.step(in);

    if (race.phase() != Phase::Running) {
        printf("  WRONG: a solo race ended before the player finished\n");
        return 1;
    }

    // The angle turned must be exactly proportional to the time the key is held.
    int failures = 0, valid = 0;
    float perTick = 0;
    for (int ticks : {1, 2, 4, 8, 16, 32}) {
        float before = race.cars()[0].heading;
        int respawns = respawnCount(race);
        in[0].right = true;
        for (int i = 0; i < ticks; ++i) race.step(in);
        in[0].right = false;
        float turned = race.cars()[0].heading - before;
        for (int i = 0; i < 6; ++i) race.step(in);
        const Car& car = race.cars()[0];
        if (respawnCount(race) != respawns || car.knock > 0 || car.yawKick != 0) { printf("  %d ticks: car hit something or left the track, ignored\n", ticks); continue; }
        ++valid;
        if (perTick == 0) perTick = turned / ticks;
        bool linear = std::abs(turned / ticks - perTick) < perTick * 0.01f;
        printf("  %2d ticks turned %6.2f degrees%s\n", ticks, turned * 180.0f / PI, linear ? "" : "  WRONG");
        if (!linear) ++failures;
    }
    if (valid == 0) ++failures;
    return failures;
}

// Drives a whole race through the player's steering, to prove the turn rate can make every corner.
static int runPlayerBot(const TrackDef& def, const Track& track) {
    RaceConfig cfg;
    cfg.mode = RaceMode::TimeTrial;
    cfg.numCars = 1;
    cfg.humans = 1;
    cfg.laps = def.laps;
    Race race(track, cfg);
    Input in[2];
    int ticks = 0;
    while (race.phase() != Phase::Finished && ticks < 60 * 60 * 6) {
        const Car& c = race.cars()[0];
        float speed = len(c.vel);
        int ahead = (int)((30.0f + speed * 0.3f) / Track::STEP);
        const Sample& s = track.samples()[track.wrap(c.idx + ahead)];
        Vec2 to = s.pos + s.normal * s.line - c.pos;
        float err = angleDiff(std::atan2(to.y, to.x), c.heading);
        in[0] = Input{};
        in[0].right = err > 0.03f;
        in[0].left = err < -0.03f;
        in[0].gas = std::abs(err) < 0.7f;
        in[0].brake = std::abs(err) > 0.9f && speed > 90.0f;
        race.step(in);
        ++ticks;
    }
    const Car& c = race.cars()[0];
    printf("  player steering bot: %6.1fs, best lap %5.1f, falls %d, strays %d%s\n", c.finishTime, c.bestLap, c.falls, c.strays,
           c.finished && c.finishTime > 0 ? "" : "  DID NOT FINISH");
    return c.finished && c.finishTime > 0 ? 0 : 1;
}

// Props are placed by hand and at random, so check nothing ended up blocking the way.
static int runLayoutChecks(const Track& track) {
    int failures = 0;
    RaceConfig cfg;
    cfg.humans = 0;
    Race race(track, cfg);
    for (const Car& c : race.cars()) {
        bool blocked = track.at(c.pos) == Surface::Wall;
        for (int a = 0; a < 8; ++a) blocked = blocked || track.at(c.pos + fromAngle(TAU * a / 8) * c.spec->radius) == Surface::Wall;
        if (blocked) { printf("  WRONG: grid slot %d starts inside a prop\n", c.id); ++failures; }
    }
    int stuck = track.blockedLineSamples();
    printf("  racing line: %d blocked samples of %d%s\n", stuck, track.count(), stuck ? "  WRONG" : "");
    return failures + (stuck ? 1 : 0);
}

static void stage(Car& c, const Track& track, int idx, float lateral, float heading, Vec2 vel) {
    const Sample& s = track.samples()[track.wrap(idx)];
    c.pos = s.pos + s.normal * lateral;
    c.heading = heading;
    c.vel = vel;
    c.idx = c.safeIdx = track.wrap(idx);
    c.z = c.vz = 0;
    c.invuln = 0;
}

// Finds a stretch of route that is straight and clear, to crash cars on.
static int findClearStraight(const Track& track) {
    for (int i = 0; i < track.count(); ++i) {
        bool ok = true;
        for (int j = -30; j <= 30 && ok; j += 3) {
            const Sample& s = track.samples()[track.wrap(i + j)];
            ok = std::abs(s.curvature) < 0.0015f && !track.inJumpZone(track.wrap(i + j));
            for (float l = -40; l <= 40 && ok; l += 5) ok = track.at(s.pos + s.normal * l) == Surface::Road;
        }
        if (ok) return i;
    }
    return -1;
}

static int runBounceChecks(const Track& track) {
    const int idx = findClearStraight(track);
    if (idx < 0) { printf("  no clear straight on this course, skipped\n"); return 0; }
    const Sample& s = track.samples()[idx];
    const float heading = std::atan2(s.tan.y, s.tan.x);
    RaceConfig cfg;
    cfg.numCars = 2;
    cfg.humans = 2;
    int failures = 0;

    {   // Rear-ending a parked car: they must fly apart at least as fast as they met.
        Race race(track, cfg);
        Input in[2];
        while (race.phase() == Phase::Countdown) race.step(in);
        Car& target = race.carForTest(0);
        Car& hitter = race.carForTest(1);
        stage(target, track, idx, 0, heading, {});
        stage(hitter, track, idx - 12, 0, heading, s.tan * 200.0f);
        in[1].gas = true;
        float closing = 0, parted = 0, deepest = 0;
        for (int t = 0; t < 40; ++t) {
            float before = dot(hitter.vel - target.vel, s.tan);
            race.step(in);
            deepest = std::max(deepest, worstOverlap(race));
            if (len(target.vel) > 30.0f) { closing = before; parted = dot(target.vel - hitter.vel, s.tan); break; }
        }
        bool ok = closing > 100.0f && parted >= closing && deepest <= MAX_ALLOWED_OVERLAP;
        printf("  rear-end: met at %.0f px/s, parted at %.0f px/s, overlap %.2f px%s\n", closing, parted, deepest, ok ? "" : "  WRONG");
        if (!ok) ++failures;
    }
    {   // T-boning a parked car: the knock must carry it well sideways, not die in the tyres.
        Race race(track, cfg);
        Input in[2];
        while (race.phase() == Phase::Countdown) race.step(in);
        Car& target = race.carForTest(0);
        Car& hitter = race.carForTest(1);
        stage(target, track, idx, 0, heading, {});
        const Vec2 toward = s.normal * -1.0f;
        stage(hitter, track, idx, 34.0f, std::atan2(toward.y, toward.x), toward * 200.0f);
        in[1].gas = true;
        const Vec2 start = target.pos;
        float deepest = 0;
        int after = -1;
        for (int t = 0; t < 60 && after < 20; ++t) {
            race.step(in);
            deepest = std::max(deepest, worstOverlap(race));
            if (after >= 0) ++after;
            else if (len(target.vel) > 30.0f) after = 0;
        }
        float carried = dot(target.pos - start, toward);
        bool ok = carried >= 30.0f && deepest <= MAX_ALLOWED_OVERLAP;   // tyre grip alone would stop it within about 25 px
        printf("  T-bone: target carried %.0f px sideways, overlap %.2f px%s\n", carried, deepest, ok ? "" : "  WRONG");
        if (!ok) ++failures;
    }
    return failures;
}

// Drives a car straight at the side of a solid object and checks it rebounds without entering it.
static int runWallBounce(const Track& track) {
    const float hw = track.halfWidth();
    for (int i = 40; i < track.count() - 40; ++i) {
        if (track.inJumpZone(i)) continue;
        const Sample& s = track.samples()[i];
        float wall = -1;
        for (float l = 0; l <= hw; l += 2) {
            if (track.at(s.pos + s.normal * l) == Surface::Wall) { wall = l; break; }
        }
        if (wall < 0) continue;
        bool open = true;
        for (float l = wall - 44; l < wall - 1 && open; l += 2)
            for (float t : {-12.0f, 0.0f, 12.0f}) open = open && track.at(s.pos + s.normal * l + s.tan * t) != Surface::Wall;
        if (!open) continue;

        RaceConfig cfg;
        cfg.mode = RaceMode::TimeTrial;
        cfg.numCars = 1;
        cfg.humans = 1;
        Race race(track, cfg);
        Input in[2];
        while (race.phase() == Phase::Countdown) race.step(in);
        Car& car = race.carForTest(0);
        stage(car, track, i, wall - 34.0f, std::atan2(s.normal.y, s.normal.x), s.normal * 220.0f);
        float impact = 0, rebound = 0;
        int inside = 0;
        for (int t = 0; t < 40; ++t) {
            float before = dot(car.vel, s.normal);
            race.step(in);
            const Vec2 ax = fromAngle(car.heading), ay{-ax.y, ax.x};
            const float hx = car.spec->halfLength - 1.5f, hy = car.spec->halfWidth - 1.5f;
            for (float a : {-1.0f, 1.0f})
                for (float b : {-1.0f, 1.0f})
                    if (track.at(car.pos + ax * (hx * a) + ay * (hy * b)) == Surface::Wall) ++inside;
            // Objects are rarely square-on to the route, so measure the speed kept rather than one direction.
            if (dot(car.vel, s.normal) < -20.0f) { impact = before; rebound = len(car.vel); break; }
        }
        bool ok = impact > 100.0f && rebound >= impact * 0.6f && inside == 0;
        printf("  wall: hit at %.0f px/s, rebounded at %.0f px/s, corners inside %d%s\n", impact, rebound, inside, ok ? "" : "  WRONG");
        return ok ? 0 : 1;
    }
    printf("  no object beside the route on this course, skipped\n");
    return 0;
}

// ---------------------------------------------------------------- modules

// Splits a race between two modules joined by a simulated link, the way two machines share one:
// module 0 (the host) drives cars 0, 2 and 3, and module 1 drives car 1.
struct ModulePair {
    Race a, b;
    LocalLink link;

    static RaceConfig config(int self, int humans, int numCars, int laps) {
        RaceConfig cfg;
        cfg.humans = humans;
        cfg.numCars = numCars;
        cfg.laps = laps;
        cfg.driver[1] = 1;
        cfg.self = self;
        return cfg;
    }
    ModulePair(const Track& track, int humans, int numCars, int laps, int delay, float loss)
        : a(track, config(0, humans, numCars, laps)), b(track, config(1, humans, numCars, laps)), link(delay, loss) {
        link.add(a);
        link.add(b);
    }
    void step(const Input inA[2], const Input inB[2]) {
        a.step(inA);
        b.step(inB);
        link.exchange();
    }
    const Car& own(int car) const { return car == 1 ? b.cars()[car] : a.cars()[car]; }       // where its module has it
    const Car& seen(int car) const { return car == 1 ? a.cars()[car] : b.cars()[car]; }      // where the other module shows it
};

static float percentile(std::vector<float> v, float p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, (size_t)(p * (v.size() - 1)))];
}

// A whole race across the link. Both modules must agree on the result, and each must show the
// other's car close to where that car really is.
static int runModuleRace(const TrackDef& def, const Track& track, int delay, float loss) {
    ModulePair m(track, 0, 4, def.laps, delay, loss);
    Input none[2];
    int ticks = 0;
    const int limit = 60 * 60 * 6;
    std::vector<float> errors;
    float deepestA = 0, deepestB = 0, deepestTrue = 0;
    int contacts = 0, hits = 0;
    while ((m.a.phase() != Phase::Finished || m.b.phase() != Phase::Finished) && ticks < limit) {
        m.step(none, none);
        ++ticks;
        contacts += (int)(m.a.news().contacts.size() + m.b.news().contacts.size());
        for (const Race* r : {&m.a, &m.b})
            for (const RaceEvent& e : r->events()) hits += e.type == EventType::Hit ? 1 : 0;
        if (m.a.phase() != Phase::Running) continue;
        for (int car = 0; car < 4; ++car) {
            const Car& own = m.own(car);
            const Car& seen = m.seen(car);
            if (own.state == CarState::Drive && seen.state == CarState::Drive) errors.push_back(len(own.pos - seen.pos));
        }
        deepestA = std::max(deepestA, worstOverlap(m.a));
        deepestB = std::max(deepestB, worstOverlap(m.b));
        // The overlap between the cars as their own modules have them, which is what both screens converge on.
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j) {
                const Car& ci = m.own(i);
                const Car& cj = m.own(j);
                if (ci.state == CarState::Drive && cj.state == CarState::Drive && std::abs(ci.z - cj.z) <= 8.0f)
                    deepestTrue = std::max(deepestTrue, carOverlapDepth(ci, cj));
            }
    }

    int failures = 0;
    const bool done = m.a.phase() == Phase::Finished && m.b.phase() == Phase::Finished;
    const bool agree = m.a.standings() == m.b.standings();
    for (const Car& c : m.a.cars()) failures += c.finished ? 0 : 1;
    const float p99 = percentile(errors, 0.99f);
    // Within a few pixels almost always; the rest is respawns and collisions, which jump.
    const bool close = p99 <= 4.0f;
    // Each module sees the other's car late, so two cars closing at a typical 150 px/s can sink
    // about 2.5 px further into each other for every tick of delay before both have reacted.
    const bool apart = deepestTrue <= 1.5f + 2.5f * delay;
    if (!done || !agree || !close || !apart) ++failures;
    printf("  modules, %d tick delay, %.0f%% states lost: %s, results %s, position error median %.2f / 99%% %.2f / max %.0f px, "
           "overlap seen %.1f/%.1f real %.1f px, %d contacts, %d shell hits%s\n",
           delay, loss * 100, done ? "finished" : "DID NOT FINISH", agree ? "agree" : "DIFFER", percentile(errors, 0.5f), p99,
           percentile(errors, 1.0f), deepestA, deepestB, deepestTrue, contacts, hits, failures ? "  WRONG" : "");
    return failures;
}

// The rear-end crash from runBounceChecks, with the two cars in different modules.
static int runModuleBounce(const Track& track, int delay) {
    const int idx = findClearStraight(track);
    if (idx < 0) return 0;
    const Sample& s = track.samples()[idx];
    const float heading = std::atan2(s.tan.y, s.tan.x);
    ModulePair m(track, 2, 2, 3, delay, 0.0f);
    Input none[2], gas[2];
    gas[0].gas = true;   // car 1 is the only person at module 1's keyboard
    while (m.a.phase() == Phase::Countdown) m.step(none, none);
    for (Race* r : {&m.a, &m.b}) {
        stage(r->carForTest(0), track, idx, 0, heading, {});
        stage(r->carForTest(1), track, idx - 12, 0, heading, s.tan * 200.0f);
    }
    float closing = 0, parted = 0, deepest = 0;
    for (int t = 0; t < 60; ++t) {
        const Car& target = m.own(0);
        const Car& hitter = m.own(1);
        float before = dot(hitter.vel - target.vel, s.tan);
        m.step(none, gas);
        deepest = std::max(deepest, carOverlapDepth(m.own(0), m.own(1)));
        if (len(m.own(0).vel) > 30.0f && dot(m.own(0).vel - m.own(1).vel, s.tan) > 0) {
            if (closing == 0) closing = before;
            if (t > 0) parted = std::max(parted, dot(m.own(0).vel - m.own(1).vel, s.tan));
        }
        if (closing > 0 && parted > 0 && t > 20) break;
    }
    // Each module bounces only its own car, so allow a little less than the shared solver gives.
    bool ok = closing > 100.0f && parted >= closing * 0.9f && deepest <= 4.0f;
    printf("  modules rear-end, %d tick delay: met at %.0f px/s, parted at %.0f px/s, overlap %.2f px%s\n", delay, closing, parted,
           deepest, ok ? "" : "  WRONG");
    return ok ? 0 : 1;
}

int main() {
    int failures = 0;
    printf("STEERING\n");
    failures += runSteeringFeel(0);
    failures += runSteeringFeel(2);
    for (const TrackDef& def : allTracks()) {
        Track track(def);
        printf("%s (%dx%d, %.0f px/lap)\n", def.name, track.width(), track.height(), track.length());
        failures += runLayoutChecks(track);
        failures += runBounceChecks(track);
        failures += runWallBounce(track);
        failures += runCircuit(def, track, 1.0f);
        failures += runCircuit(def, track, 0.88f);
        failures += runHeadToHead(track);
        failures += runIdleHeadToHead(track);
        failures += runIdleCircuit(def, track);
        failures += runPlayerBot(def, track);
        failures += runModuleBounce(track, 0);
        failures += runModuleBounce(track, 2);
        failures += runModuleRace(def, track, 1, 0.0f);
        failures += runModuleRace(def, track, 3, 0.1f);
    }
    printf(failures ? "FAILED: %d\n" : "OK (%d failures)\n", failures);
    return failures ? 1 : 0;
}
