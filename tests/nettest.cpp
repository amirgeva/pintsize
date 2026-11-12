// LAN sessions over real sockets on the loopback interface, all in one process. A host and two
// clients race one lap with one computer car. Client B has a simulated poor connection and leaves
// part way, so the host has to take over its car. Runs in real time: about half a minute.
#include "net.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

namespace {
struct Machine {
    const char* label;
    NetSession net;
    std::unique_ptr<Race> race;
    bool live = false, gone = false;
    std::vector<float> errors;   // how far this machine shows other machines' cars from where they are
};

float percentile(std::vector<float> v, float p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, (size_t)(p * (v.size() - 1)))];
}

void pump(Machine* ms[], int n) {
    for (int i = 0; i < n; ++i)
        if (!ms[i]->gone) ms[i]->net.poll(ms[i]->race.get());
}
}  // namespace

int main() {
    const uint16_t port = 47490;
    const int trackIndex = 0;
    const Track track(allTracks()[trackIndex]);
    Machine host{"host"}, a{"client A"}, b{"client B"};
    Machine* all[3] = {&host, &a, &b};
    int failures = 0;
    std::string error;

    NetPlayer me;
    me.name = "HOST";
    me.bot = true;
    if (!host.net.host(me, port, error, "127.0.0.1", false)) { printf("cannot host: %s\n", error.c_str()); return 1; }
    NetConditions poor;
    poor.lagMs = 50;
    poor.jitterMs = 25;
    poor.lossPercent = 10;
    b.net.setConditions(poor);
    me.name = "A";
    me.color = 1;
    if (!a.net.join(me, "127.0.0.1", port, error)) { printf("A cannot join: %s\n", error.c_str()); return 1; }
    me.name = "B";
    me.color = 2;
    if (!b.net.join(me, "127.0.0.1", port, error)) { printf("B cannot join: %s\n", error.c_str()); return 1; }
    printf("client B simulates %s\n", netDescribe(poor).c_str());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (host.net.lobby().players.size() < 3 || a.net.lobby().players.size() < 3 || b.net.lobby().players.size() < 3) {
        pump(all, 3);
        if (std::chrono::steady_clock::now() > deadline) { printf("WRONG: the lobby never filled (%d players)\n", (int)host.net.lobby().players.size()); return 1; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    printf("lobby: %d players; A is machine %d, B is machine %d\n", (int)host.net.lobby().players.size(), a.net.self(), b.net.self());

    host.net.setTrack(trackIndex);
    host.net.setCpuCars(1);
    host.net.startRace(1234, track.count(), 1);

    using clock = std::chrono::steady_clock;
    const auto limit = clock::now() + std::chrono::seconds(90);
    int bCar = -1;
    Input none[2];
    while (clock::now() < limit) {
        pump(all, 3);
        bool stepped = false;
        for (Machine* m : all) {
            if (m->gone) continue;
            if (m->net.closed()) { printf("WRONG: %s's session closed: %s\n", m->label, m->net.reason().c_str()); return 1; }
            NetStart s;
            if (m->net.takeStart(s)) {
                if (s.samples != track.count()) { printf("WRONG: %s got %d samples\n", m->label, s.samples); ++failures; }
                m->race = std::make_unique<Race>(track, s.cfg);
                m->net.ready();
            }
            if (m->race && !m->live && m->net.started()) m->live = true;
            while (m->live && m->race->ticks() < m->net.ticksDue()) {
                m->race->step(none);
                m->net.send(*m->race);
                stepped = true;
            }
        }
        if (!stepped) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (b.race && bCar < 0)
            for (const Car& c : b.race->cars()) if (!c.remote && bCar < 0) bCar = c.id;

        // B walks out eight seconds into the race.
        if (!b.gone && b.live && b.race->clock() > 8.0f) {
            b.net.close();
            b.gone = true;
            printf("B left at %.1f s\n", host.race->clock());
        }

        // Compare each machine's view of every car with the car as the machine driving it has it,
        // when both have made the same number of steps.
        for (Machine* viewer : all) {
            if (viewer->gone || !viewer->live || viewer->race->phase() != Phase::Running) continue;
            for (const Car& seen : viewer->race->cars()) {
                if (!seen.remote) continue;
                for (Machine* owner : all) {
                    if (owner == viewer || owner->gone || !owner->live || owner->race->ticks() != viewer->race->ticks()) continue;
                    const Car& real = owner->race->cars()[seen.id];
                    if (real.remote || real.state != CarState::Drive || seen.state != CarState::Drive) continue;
                    viewer->errors.push_back(len(real.pos - seen.pos));
                }
            }
        }

        if (host.live && a.live && host.race->phase() == Phase::Finished && a.race->phase() == Phase::Finished) break;
    }

    const bool finished = host.live && a.live && host.race->phase() == Phase::Finished && a.race->phase() == Phase::Finished;
    printf("race %s after %.1f s\n", finished ? "finished" : "DID NOT FINISH", host.race ? host.race->clock() : 0.0f);
    if (!finished) ++failures;
    if (finished) {
        const bool agree = host.race->standings() == a.race->standings();
        printf("results %s:", agree ? "agree" : "DIFFER");
        for (int id : host.race->standings()) printf(" car %d (%.1f s)", id, host.race->cars()[id].finishTime);
        printf("\n");
        if (!agree) ++failures;
        for (const Car& c : host.race->cars())
            if (!c.finished) { printf("WRONG: car %d did not finish\n", c.id); ++failures; }
        const bool tookOver = bCar >= 0 && !host.race->cars()[bCar].remote;
        printf("B's car %d %s\n", bCar, tookOver ? "was taken over by the host" : "WAS NOT TAKEN OVER");
        if (!tookOver) ++failures;
    }

    // A and the host see each other over a clean link and B over a poor one, so their worst cases
    // are B's; the medians show the clean case.
    for (Machine* m : {&host, &a, &b}) {
        const NetStats s = m->net.stats();
        const float median = percentile(m->errors, 0.5f), p99 = percentile(m->errors, 0.99f);
        printf("%-8s states in %5d (%3d lost), out %5d, %.1f ms old on average; others' cars off by median %.2f / 99%% %.2f px\n",
               m->label, s.statesIn, s.statesLost, s.statesOut, s.ageCount ? s.ageSumMs / s.ageCount : 0.0, median, p99);
        const bool ok = m->errors.empty() || (median <= 1.0f && p99 <= 20.0f);
        if (!ok) { printf("  WRONG: too far off\n"); ++failures; }
        if (m != &b && s.statesIn == 0) { printf("  WRONG: no states arrived\n"); ++failures; }
    }

    printf(failures ? "FAILED: %d\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
