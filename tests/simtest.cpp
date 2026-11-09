// Headless check that computer drivers can complete every track and that the game modes terminate.
#include "race.h"
#include <cstdio>

static int runCircuit(const TrackDef& def, const Track& track, float skill) {
    RaceConfig cfg;
    cfg.humans = 0;
    cfg.aiSkill = skill;
    cfg.laps = def.laps;
    Race race(track, cfg);
    Input none[2];
    int ticks = 0;
    const int limit = 60 * 60 * 6;
    while (race.phase() != Phase::Finished && ticks < limit) { race.step(none); ++ticks; }

    int failures = 0;
    printf("  skill %.2f:", skill);
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
        if (respawnCount(race) != respawns) { printf("  %d ticks: car left the track, ignored\n", ticks); continue; }
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

int main() {
    int failures = 0;
    printf("STEERING\n");
    failures += runSteeringFeel(0);
    failures += runSteeringFeel(2);
    for (const TrackDef& def : allTracks()) {
        Track track(def);
        printf("%s (%dx%d, %.0f px/lap)\n", def.name, track.width(), track.height(), track.length());
        failures += runLayoutChecks(track);
        failures += runCircuit(def, track, 1.0f);
        failures += runCircuit(def, track, 0.88f);
        failures += runHeadToHead(track);
        failures += runIdleHeadToHead(track);
        failures += runIdleCircuit(def, track);
        failures += runPlayerBot(def, track);
    }
    printf(failures ? "FAILED: %d\n" : "OK (%d failures)\n", failures);
    return failures ? 1 : 0;
}
