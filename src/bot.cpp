// psp_bot: a computer driver that races other machines over the LAN, with no window or sound.
// It uses the same race module and network code as the game, so it also serves to test them.
#include "net.h"
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

namespace {
volatile std::sig_atomic_t stopRequested = 0;
void onSignal(int) { stopRequested = 1; }

std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

// Every line is flushed at once, so a log being followed with tail -f stays current.
void say(const char* fmt, ...) {
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("[%3d:%04.1f] ", (int)(t / 60), t - 60 * (int)(t / 60));
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
    std::fflush(stdout);
}

std::string formatTime(float t) {
    char buf[32];
    int m = (int)(t / 60);
    std::snprintf(buf, sizeof buf, "%d:%04.1f", m, t - m * 60);
    return buf;
}

const char* placeName(int p) {
    static const char* names[] = {"-", "1st", "2nd", "3rd", "4th"};
    return names[p >= 0 && p <= 4 ? p : 0];
}

void usage() {
    std::printf(
        "psp_bot: a computer driver for Pint-Size Prix LAN races\n"
        "\n"
        "  psp_bot [--join ADDRESS[:PORT]] [options]   join a host; without an address, search the LAN\n"
        "  psp_bot --host [options]                    host races\n"
        "\n"
        "Options:\n"
        "  --name NAME        name shown in the lobby (default BOT-<computer name>)\n"
        "  --skill X          driving speed, 0.5 to 1.1 (default 1.0)\n"
        "  --color N          preferred driver, 0-5\n"
        "  --port N           UDP port (default %d)\n"
        "  --stats SECONDS    print network statistics this often, 0 for never (default 5)\n"
        "  --quiet            only print connections and results\n"
        "\n"
        "Simulated poor connection (this machine's traffic, both directions):\n"
        "  --lag MS           extra delay each way\n"
        "  --jitter MS        random variation of the delay, either way\n"
        "  --loss PERCENT     chance each packet is lost\n"
        "\n"
        "Host options:\n"
        "  --bind ADDRESS     listen on one interface only, e.g. 127.0.0.1\n"
        "  --players N        start when this many machines, the bot included, are in (default 2)\n"
        "  --track N|cycle    course 0-5, or each in turn (default 0)\n"
        "  --cpu N            computer cars the host adds (default 0)\n"
        "  --races N          stop after N races (default: carry on)\n"
        "  --laps N           laps per race (default: the course's own)\n",
        NET_PORT);
}

struct Options {
    bool host = false;
    std::string address;      // empty: search
    uint16_t port = NET_PORT;
    std::string name;
    float skill = 1.0f;
    int color = 1;
    float statsEvery = 5.0f;
    bool quiet = false;
    NetConditions sim;
    std::string bind;
    int players = 2;
    int track = 0;
    bool cycle = false;
    int cpu = 0;
    int races = 0;
    int laps = 0;
};

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto value = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs %s\n", a.c_str(), what); return nullptr; }
            return argv[++i];
        };
        const char* v = nullptr;
        if (a == "--help" || a == "-h") { usage(); std::exit(0); }
        else if (a == "--host") o.host = true;
        else if (a == "--quiet") o.quiet = true;
        else if (a == "--join") {
            // The address is optional: a following option or nothing means search.
            if (i + 1 < argc && argv[i + 1][0] != '-') o.address = argv[++i];
        } else if (a == "--name") { if (!(v = value("a name"))) return false; o.name = v; }
        else if (a == "--skill") { if (!(v = value("a number"))) return false; o.skill = (float)std::atof(v); }
        else if (a == "--color") { if (!(v = value("a number"))) return false; o.color = std::atoi(v); }
        else if (a == "--port") { if (!(v = value("a port"))) return false; o.port = (uint16_t)std::atoi(v); }
        else if (a == "--stats") { if (!(v = value("seconds"))) return false; o.statsEvery = (float)std::atof(v); }
        else if (a == "--lag") { if (!(v = value("milliseconds"))) return false; o.sim.lagMs = (float)std::atof(v); }
        else if (a == "--jitter") { if (!(v = value("milliseconds"))) return false; o.sim.jitterMs = (float)std::atof(v); }
        else if (a == "--loss") { if (!(v = value("a percentage"))) return false; o.sim.lossPercent = (float)std::atof(v); }
        else if (a == "--bind") { if (!(v = value("an address"))) return false; o.bind = v; }
        else if (a == "--players") { if (!(v = value("a number"))) return false; o.players = std::atoi(v); }
        else if (a == "--track") {
            if (!(v = value("a course number or cycle"))) return false;
            if (std::strcmp(v, "cycle") == 0) o.cycle = true;
            else o.track = std::atoi(v);
        } else if (a == "--cpu") { if (!(v = value("a number"))) return false; o.cpu = std::atoi(v); }
        else if (a == "--races") { if (!(v = value("a number"))) return false; o.races = std::atoi(v); }
        else if (a == "--laps") { if (!(v = value("a number"))) return false; o.laps = std::atoi(v); }
        else { std::fprintf(stderr, "unknown option %s (try --help)\n", a.c_str()); return false; }
    }
    const int tracks = (int)allTracks().size();
    if (o.track < 0 || o.track >= tracks) { std::fprintf(stderr, "--track must be 0 to %d\n", tracks - 1); return false; }
    if (o.skill < 0.5f || o.skill > 1.1f) { std::fprintf(stderr, "--skill must be 0.5 to 1.1\n"); return false; }
    o.players = std::max(1, std::min(o.players, MAX_CARS));
    o.color = ((o.color % NUM_DRIVERS) + NUM_DRIVERS) % NUM_DRIVERS;
    if (o.name.empty()) o.name = "BOT-" + netLocalName();
    return true;
}

// Finds a host on the LAN, for --join without an address.
bool search(std::string& address, uint16_t& port) {
    NetSearch finder;
    if (!finder.ok()) { say("cannot open a socket to search the LAN"); return false; }
    say("searching the LAN for a host...");
    for (int i = 0; i < 50 && !stopRequested; ++i) {
        finder.poll();
        if (!finder.hosts().empty()) {
            const NetFoundHost& h = finder.hosts().front();
            say("found %s at %s:%u", h.name.c_str(), h.address.c_str(), h.port);
            address = h.address;
            port = h.port;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    say("no host answered; start one, or give its address with --join ADDRESS");
    return false;
}

struct Bot {
    Options opt;
    NetSession net;
    std::unique_ptr<Track> track;
    int trackIndex = -1;
    std::unique_ptr<Race> race;
    bool live = false;           // the race has started and is being stepped
    bool reported = false;       // the result has been printed
    int myCar = -1;              // the car this bot drives as a player
    int lastLaps = -1;
    int racesDone = 0;
    double lobbySince = 0;       // host: when the last race ended or enough players arrived
    double overSince = -1;       // host: when the race was over
    NetStats lastStats;
    double nextStats = 0;

    std::string carLabel(int car) const {
        const Car& c = race->cars()[car];
        std::string who = "CPU";
        if (carOfSlot(c.driver) == car)
            for (const NetPlayer& p : net.lobby().players)
                if (p.slot == c.driver) who = p.name;
        return std::string(driverName(c.color)) + " (" + who + ")";
    }
    int carOfSlot(int slot) const {
        // A machine's player car is the first car it drives; the host's later ones are fillers.
        for (const Car& c : race->cars()) if (c.driver == slot) return c.id;
        return -1;
    }

    void build(const NetStart& s) {
        if (trackIndex != s.track) {
            const auto t0 = std::chrono::steady_clock::now();
            track = std::make_unique<Track>(allTracks()[s.track]);
            trackIndex = s.track;
            say("built %s in %.1f s", allTracks()[s.track].name,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        }
        if (track->count() != s.samples) {
            say("this course differs from the host's (%d route samples here, %d there): different game versions?", track->count(), s.samples);
            net.close();
            std::exit(2);
        }
        RaceConfig cfg = s.cfg;
        cfg.aiSkill = opt.skill;   // only the cars driven here use it
        race = std::make_unique<Race>(*track, cfg);
        live = reported = false;
        overSince = -1;
        myCar = carOfSlot(net.self());
        lastLaps = -1;
        say("race %d on %s: %d cars, %d lap%s; I drive car %d (%s)", racesDone + 1, allTracks()[s.track].name, cfg.numCars, cfg.laps,
            cfg.laps == 1 ? "" : "s", myCar, myCar >= 0 ? driverName(race->cars()[myCar].color) : "-");
        net.ready();
    }

    void report() {
        say("race over:");
        const auto order = race->standings();
        for (size_t i = 0; i < order.size(); ++i) {
            const Car& c = race->cars()[order[i]];
            say("  %s %-24s %s%s", placeName((int)i + 1), carLabel(c.id).c_str(), c.finished && c.finishTime > 0 ? formatTime(c.finishTime).c_str() : "  -  ",
                c.id == myCar ? "   <- me" : "");
        }
        reported = true;
        ++racesDone;
    }

    void watchMyCar() {
        if (myCar < 0 || opt.quiet) return;
        const Car& c = race->cars()[myCar];
        if (c.lapsDone > lastLaps) {
            if (lastLaps >= 0 && c.lapsDone > 0) {
                const auto order = race->standings();
                int place = 1;
                for (size_t i = 0; i < order.size(); ++i) if (order[i] == myCar) place = (int)i + 1;
                say("lap %d/%d done, best lap %s, running %s", c.lapsDone, race->config().laps, formatTime(c.bestLap).c_str(), placeName(place));
            }
            lastLaps = c.lapsDone;
        }
    }

    void printStats() {
        if (opt.statsEvery <= 0 || !net.connected()) return;
        const double t = net.now();
        if (t < nextStats) return;
        nextStats = t + opt.statsEvery;
        const NetStats s = net.stats();
        const int in = s.statesIn - lastStats.statesIn;
        const int lost = s.statesLost - lastStats.statesLost;
        const int ages = s.ageCount - lastStats.ageCount;
        const double age = ages > 0 ? (s.ageSumMs - lastStats.ageSumMs) / ages : 0;
        lastStats = s;
        if (!live) return;
        say("net: round trip %.1f ms, clock within %.1f ms, states in %d (%d lost, %.1f ms old on average), out %d",
            s.rttMs, s.clockMs, in, lost, age, s.statesOut);
    }

    // Host: start races once enough players are in, and go back to the lobby after each.
    void hostLogic() {
        const double t = net.now();
        if (!net.inRace()) {
            if (opt.races > 0 && racesDone >= opt.races) { stopRequested = 1; return; }
            const int have = (int)net.lobby().players.size();
            if (have < opt.players) { lobbySince = t; return; }
            if (t - lobbySince < 3.0) return;   // give a joining player a moment in the lobby
            int index = opt.cycle ? racesDone % (int)allTracks().size() : opt.track;
            net.setTrack(index);
            net.setCpuCars(opt.cpu);
            if (trackIndex != index) {
                track = std::make_unique<Track>(allTracks()[index]);
                trackIndex = index;
            }
            say("starting a race with %d player%s", have, have == 1 ? "" : "s");
            net.startRace((uint32_t)std::chrono::steady_clock::now().time_since_epoch().count() | 1u, track->count(), opt.laps);
            return;
        }
        if (race && race->phase() == Phase::Finished) {
            if (overSince < 0) overSince = t;
            if (t - overSince > 4.0) {
                net.endRace();
                race.reset();
                live = false;
                lobbySince = t;
            }
        }
    }

    int run() {
        std::string error;
        net.setConditions(opt.sim);
        if (opt.sim.active()) say("simulating a poor connection: %s", netDescribe(opt.sim).c_str());
        NetPlayer me;
        me.name = opt.name;
        me.bot = true;
        me.color = opt.color;
        if (opt.host) {
            if (!net.host(me, opt.port, error, opt.bind.empty() ? nullptr : opt.bind.c_str())) { say("cannot host: %s", error.c_str()); return 1; }
            say("hosting as %s on UDP port %u%s%s; waiting for %d player%s", net.hostName().c_str(), opt.port,
                opt.bind.empty() ? "" : " of ", opt.bind.c_str(), opt.players, opt.players == 1 ? "" : "s");
            if (!net.visible()) say("(another host on this machine answers LAN searches, so this one cannot be found by searching)");
        } else {
            std::string address = opt.address;
            uint16_t port = opt.port;
            if (!address.empty() && !netParseAddress(opt.address, address, port)) { say("bad address %s", opt.address.c_str()); return 1; }
            if (address.empty() && !search(address, port)) return 1;
            if (!net.join(me, address, port, error)) { say("cannot join: %s", error.c_str()); return 1; }
            say("connecting to %s:%u as %s...", address.c_str(), port, opt.name.c_str());
        }

        using clock = std::chrono::steady_clock;
        const auto tick = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(DT));
        auto next = clock::now();
        bool wasConnected = false;
        size_t lastPlayers = 0;
        Input none[2];
        while (!stopRequested) {
            net.poll(race.get());
            if (net.closed()) { say("session ended: %s", net.reason().c_str()); break; }
            if (net.connected() && !wasConnected) {
                wasConnected = true;
                if (!opt.host) say("joined %s as machine %d", net.hostName().c_str(), net.self());
            }
            if (net.lobby().players.size() != lastPlayers) {
                lastPlayers = net.lobby().players.size();
                std::string names;
                for (const NetPlayer& p : net.lobby().players) names += (names.empty() ? "" : ", ") + p.name + (p.bot ? " (bot)" : "");
                say("lobby: %s", names.c_str());
            }

            NetStart start;
            if (net.takeStart(start)) build(start);
            if (race && !net.inRace()) { race.reset(); live = false; }   // the host went back to the lobby
            if (race && !live && net.started()) { live = true; say("go!"); }
            if (race && live) {
                // Keep to the shared clock, catching up a few steps at a time after a stall.
                for (int i = 0; i < 8 && race->ticks() < net.ticksDue(); ++i) {
                    race->step(none);
                    net.send(*race);
                }
                watchMyCar();
                if (race->phase() == Phase::Finished && !reported) report();
            }
            if (opt.host) hostLogic();
            printStats();

            if (race && live) {
                // Wake often, so news is applied and steps are made close to when they are due.
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                next = clock::now();
            } else {
                next += tick;
                const auto now = clock::now();
                if (next < now - std::chrono::milliseconds(250)) next = now;
                std::this_thread::sleep_until(next);
            }
        }
        net.close();
        return 0;
    }
};
}  // namespace

int main(int argc, char** argv) {
    Bot bot;
    if (!parse(argc, argv, bot.opt)) return 1;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    return bot.run();
}
