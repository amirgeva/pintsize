#pragma once
// LAN play over ENet (reliable UDP). Nothing here uses SDL, so the headless bot can use it too.
//
// Every machine runs its own race module (a Race whose config().self is that machine) and drives
// only its own cars. After each step it sends its RaceNews; the others apply it. Clients connect
// only to the host (machine 0), which relays news between them and referees the race.
#include "race.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

constexpr uint16_t NET_PORT = 47474;             // game traffic (UDP)
constexpr uint16_t NET_DISCOVERY_PORT = 47475;   // LAN search (UDP broadcast)

struct NetPlayer {
    int slot = 0;          // the machine's number; 0 is the host
    std::string name;
    bool bot = false;      // drives by itself, like psp_bot, rather than from a keyboard
    int color = 0;         // preferred driver colour
};

struct NetLobby {
    int track = 0;
    int cpuCars = 0;       // computer cars the host adds to fill the grid
    bool racing = false;   // a race is under way; anyone joining now waits for the next one
    std::vector<NetPlayer> players;
};

// What a machine needs to build its module of a race the host has started.
struct NetStart {
    int track = 0;
    int samples = 0;       // the host's route sample count, to catch builds that make different tracks
    RaceConfig cfg;        // cfg.self is already this machine
};

// A simulated poor connection, for feeling how the game copes. It applies to this machine's traffic
// in both directions, so setting it on one machine of two is enough.
struct NetConditions {
    float lagMs = 0;          // added to every packet's trip, each way
    float jitterMs = 0;       // the delay varies at random by up to this much either side of lagMs
    float lossPercent = 0;    // chance a packet is lost. Car states and clock pings vanish; reliable
                              // messages arrive late instead, as ENet would resend them
    bool active() const { return lagMs > 0 || jitterMs > 0 || lossPercent > 0; }
};
std::string netDescribe(const NetConditions& c);   // e.g. "80MS +-30MS 5% LOSS", or "NONE"

struct NetStats {
    float rttMs = 0;       // round trip to the host (clients) or the slowest client (host)
    float clockMs = 0;     // how far off the shared clock can be here: half the best round trip (0 on the host)
    int statesIn = 0, statesLost = 0, statesOut = 0;
    double ageSumMs = 0;   // summed age of the car states applied, for averaging
    int ageCount = 0;
};

class NetSession {
public:
    NetSession();
    ~NetSession();
    NetSession(const NetSession&) = delete;
    NetSession& operator=(const NetSession&) = delete;

    // Host: listens on `port`. bindAddress limits it to one interface, e.g. "127.0.0.1"; visible
    // makes it answer LAN searches.
    bool host(const NetPlayer& me, uint16_t port, std::string& error, const char* bindAddress = nullptr, bool visible = true);
    // Client: starts connecting to a host; connected() turns true once it accepts.
    bool join(const NetPlayer& me, const std::string& address, uint16_t port, std::string& error);
    void close();

    // Call every tick, before stepping. Applies race news that has arrived to `race`, which may be
    // null when no race has been built.
    void poll(Race* race);
    // Call after every step of a started race: sends this machine's news.
    void send(const Race& race);

    bool isHost() const;
    bool connected() const;        // host: listening; client: accepted by the host
    bool visible() const;          // host: answering LAN searches
    bool closed() const;           // the session ended; reason() says why
    const std::string& reason() const;
    int self() const;
    const NetLobby& lobby() const;
    const std::string& hostName() const;
    NetStats stats() const;
    double now() const;            // seconds on the host's clock

    void setConditions(const NetConditions& c);   // can change at any time
    const NetConditions& conditions() const;

    // Host only
    void setTrack(int track);
    void setCpuCars(int n);
    int maxCpuCars() const;        // what fits beside the players
    // samples: the route sample count of the host's track; laps: 0 for the course's own number.
    bool startRace(uint32_t seed, int samples, int laps = 0);
    void endRace();                               // back to the lobby

    // Starting a race. takeStart() hands over an announced race once; build it, then call ready().
    // started() turns true at a start time shared by every machine, once all are ready.
    bool takeStart(NetStart& start);
    void ready();
    bool started() const;
    // How many steps the race should have made by now. Every machine makes step N at the same
    // moment on the host's clock (the start time plus N ticks), so a car state from step N can be
    // moved on to any other step exactly, and clocks that run at slightly different speeds do not
    // drift apart. Step until race.ticks() reaches this.
    uint32_t ticksDue() const;
    bool inRace() const;           // a race has been announced and not yet ended

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct NetFoundHost {
    std::string address;           // dotted IPv4
    uint16_t port = NET_PORT;
    std::string name;
    int players = 0;
    int track = 0;
    bool racing = false;
};

// Looks for hosts on the LAN by broadcasting on NET_DISCOVERY_PORT, and on this machine.
class NetSearch {
public:
    NetSearch();
    ~NetSearch();
    NetSearch(const NetSearch&) = delete;
    NetSearch& operator=(const NetSearch&) = delete;

    void poll();                   // call every tick
    bool ok() const;               // false if the search socket could not be opened
    const std::vector<NetFoundHost>& hosts() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::string netLocalName();        // this computer's name, upper case and short, for the lobby
// Splits "host" or "host:port". Returns false if the port is not a number.
bool netParseAddress(const std::string& text, std::string& host, uint16_t& port);
