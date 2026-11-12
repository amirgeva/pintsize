#include "net.h"
#include <enet/enet.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <random>
#include <set>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace {
constexpr uint32_t MAGIC = 0x4E505350;          // "PSPN"
constexpr uint32_t PROBE = 0x51505350;          // "PSPQ", a LAN search
constexpr uint32_t ANSWER = 0x41505350;         // "PSPA", a host's reply
constexpr uint16_t PROTOCOL = 2;                // bump whenever any message changes

constexpr int CHANNELS = 3;
constexpr uint8_t CH_RELIABLE = 0;              // everything that must arrive, in order
constexpr uint8_t CH_STATES = 1;                // car states: unreliable, and older ones are dropped
constexpr uint8_t CH_CLOCK = 2;                 // clock pings

constexpr size_t MAX_PEERS = 8;                 // more than the race holds, so latecomers can be told why not
constexpr double CONNECT_TIMEOUT = 5.0;
constexpr double READY_TIMEOUT = 15.0;          // a machine that takes longer to build the race is dropped
constexpr double GO_DELAY = 0.3;                // the start time is set this far ahead, so everyone hears it in time
constexpr double PING_FAST = 0.1, PING_SLOW = 1.0;
constexpr int FAST_PINGS = 20;
constexpr size_t CLOCK_SAMPLES = 16;
constexpr double SEARCH_INTERVAL = 0.5, SEARCH_EXPIRY = 3.0;
constexpr float AI_SKILL = 0.94f;
constexpr double RESEND_EXTRA = 0.030;          // simulated loss: a reliable packet is resent after a round trip plus this

enum Msg : uint8_t {
    M_HELLO = 1,   // client -> host: who I am
    M_WELCOME,     // host -> client: your machine number
    M_REJECT,      // host -> client: why not
    M_LOBBY,       // host -> all: players, track, settings
    M_START,       // host -> all: a race is starting; build it
    M_READY,       // client -> host: built
    M_GO,          // host -> all: start stepping at this host time
    M_PING,        // client -> host
    M_PONG,        // host -> client
    M_STATES,      // car states (unreliable)
    M_NEWS,        // bumps, shells, sounds, results (reliable)
};

bool netInit() {
    static const bool ok = [] {
        if (enet_initialize() != 0) return false;
        std::atexit(enet_deinitialize);
        return true;
    }();
    return ok;
}

double steadyNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Little-endian, fixed-size fields, so Windows and Linux builds understand each other.
class Writer {
public:
    explicit Writer(uint8_t type) { u8(type); }
    void u8(uint32_t v) { b_.push_back((uint8_t)v); }
    void u16(uint32_t v) { u8(v & 0xFF); u8(v >> 8); }
    void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
    void f32(float f) { uint32_t u; std::memcpy(&u, &f, 4); u32(u); }
    void f64(double d) { uint64_t u; std::memcpy(&u, &d, 8); u32((uint32_t)u); u32((uint32_t)(u >> 32)); }
    void vec(Vec2 v) { f32(v.x); f32(v.y); }
    void str(const std::string& s) {
        size_t n = std::min<size_t>(s.size(), 255);
        u8((uint32_t)n);
        b_.insert(b_.end(), s.begin(), s.begin() + n);
    }
    ENetPacket* packet(enet_uint32 flags) const { return enet_packet_create(b_.data(), b_.size(), flags); }
    const std::vector<uint8_t>& bytes() const { return b_; }

private:
    std::vector<uint8_t> b_;
};

class Reader {
public:
    Reader(const uint8_t* d, size_t n) : d_(d), n_(n) {}
    bool ok() const { return ok_; }
    uint32_t u8() {
        if (p_ >= n_) { ok_ = false; return 0; }
        return d_[p_++];
    }
    uint32_t u16() { uint32_t lo = u8(); return lo | (u8() << 8); }
    uint32_t u32() { uint32_t lo = u16(); return lo | (u16() << 16); }
    float f32() { uint32_t u = u32(); float f; std::memcpy(&f, &u, 4); return f; }
    double f64() {
        uint64_t lo = u32();
        uint64_t hi = u32();
        uint64_t u = lo | (hi << 32);
        double d;
        std::memcpy(&d, &u, 8);
        return d;
    }
    Vec2 vec() { float x = f32(); return {x, f32()}; }
    std::string str() {
        uint32_t n = u8();
        if (p_ + n > n_) { ok_ = false; return {}; }
        std::string s((const char*)d_ + p_, n);
        p_ += n;
        return s;
    }

private:
    const uint8_t* d_;
    size_t n_;
    size_t p_ = 0;
    bool ok_ = true;
};

void putCar(Writer& w, const CarSnapshot& s) {
    w.u8((uint32_t)s.id);
    w.u8((s.state == CarState::Fall ? 1u : 0u) | (s.sinking ? 2u : 0u) | (s.wasOnRamp ? 4u : 0u) | (s.finished ? 8u : 0u));
    w.u8((uint32_t)s.surface);
    w.u8((uint32_t)(uint8_t)(int8_t)std::max(-128, std::min(127, s.lapsDone)));
    w.vec(s.pos);
    w.vec(s.vel);
    for (float f : {s.heading, s.turnRate, s.z, s.vz, s.fallTimer, s.invuln, s.spin, s.knock, s.yawKick, s.skid}) w.f32(f);
    w.u32((uint32_t)s.idx);
    w.u32((uint32_t)s.safeIdx);
    for (float f : {s.progress, s.lapTime, s.bestLap, s.finishTime}) w.f32(f);
}

CarSnapshot getCar(Reader& r) {
    CarSnapshot s;
    s.id = (int)r.u8();
    const uint32_t flags = r.u8();
    s.state = (flags & 1) ? CarState::Fall : CarState::Drive;
    s.sinking = (flags & 2) != 0;
    s.wasOnRamp = (flags & 4) != 0;
    s.finished = (flags & 8) != 0;
    const uint32_t surface = r.u8();
    s.surface = surface <= (uint32_t)Surface::Sticky ? (Surface)surface : Surface::Road;
    s.lapsDone = (int8_t)(uint8_t)r.u8();
    s.pos = r.vec();
    s.vel = r.vec();
    for (float* f : {&s.heading, &s.turnRate, &s.z, &s.vz, &s.fallTimer, &s.invuln, &s.spin, &s.knock, &s.yawKick, &s.skid}) *f = r.f32();
    s.idx = (int)r.u32();
    s.safeIdx = (int)r.u32();
    for (float* f : {&s.progress, &s.lapTime, &s.bestLap, &s.finishTime}) *f = r.f32();
    return s;
}

void putEvent(Writer& w, const RaceEvent& e) {
    w.u8((uint32_t)e.type);
    w.u8((uint32_t)(uint8_t)e.car);
    w.u8((uint32_t)(uint8_t)e.other);
    w.vec(e.pos);
    w.f32(e.strength);
}

bool getEvent(Reader& r, RaceEvent& e) {
    const uint32_t type = r.u8();
    e.type = (EventType)std::min<uint32_t>(type, (uint32_t)EventType::Wall);
    e.car = (int8_t)(uint8_t)r.u8();
    e.other = (int8_t)(uint8_t)r.u8();
    e.pos = r.vec();
    e.strength = r.f32();
    return type <= (uint32_t)EventType::Wall;
}

std::string clean(std::string s) {
    // The game's font has upper case, digits and a little punctuation.
    if (s.size() > 15) s.resize(15);
    for (char& ch : s) {
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        if (ch < 32 || ch > 90) ch = '-';
    }
    return s;
}

std::string ipText(const ENetAddress& a) {
    char buf[64] = {};
    enet_address_get_host_ip(&a, buf, sizeof buf);
    return buf;
}
}  // namespace

// ---------------------------------------------------------------- session

struct NetSession::Impl {
    ENetHost* host = nullptr;
    ENetPeer* server = nullptr;            // client: the host
    bool isHost = false, connected = false, closed = false;
    std::string reason;
    NetPlayer me;
    int self = 0;
    NetLobby lobby;
    std::string hostName;
    ENetSocket discovery = ENET_SOCKET_NULL;
    uint16_t port = NET_PORT;

    // Clock: host time is local time minus epoch on the host, local time plus offset elsewhere.
    double epoch = 0;
    double offset = 0;
    struct ClockSample { double rtt, offset; };
    std::deque<ClockSample> clockSamples;
    double nextPing = 0;
    int pings = 0;
    double connectDeadline = 0;

    // Race lifecycle
    int raceId = 0;
    bool announced = false;                // a race has been announced and not ended
    bool haveStart = false;                // announced but not yet handed to the game
    NetStart start;
    bool built = false;                    // this machine built the announced race
    double goTime = -1;
    double announcedAt = 0;
    std::set<int> members, readySlots;     // host: machines in the race, and those that have built it
    std::vector<int> leftSlots;            // host: machines that left mid-race, whose cars it takes over
    std::map<int, ENetPeer*> peers;        // host: machine number -> peer
    std::map<int, uint32_t> lastStateTick; // per sending machine, to count lost states
    std::map<int, float> clientRtt;        // host: each client's round trip as it measures it (ms)
    NetStats stats;

    double now() const { return isHost ? steadyNow() - epoch : steadyNow() + offset; }

    void fail(const std::string& why) {
        if (closed) return;
        closed = true;
        reason = why;
    }

    // ---- simulated conditions
    NetConditions sim;
    std::mt19937 simRng{0x5eed};
    struct Held {
        double release;
        ENetPeer* peer;
        uint8_t channel;
        ENetPacket* packet;
    };
    std::vector<Held> outgoing, incoming;
    // When each channel's last held packet is let through. Packets wait in order behind it, as in a
    // real network queue: jitter makes them bunch up rather than overtake each other.
    std::map<std::pair<ENetPeer*, int>, double> queueOut, queueIn;

    // When a packet held back by the simulation is let through, or 0 with drop set if it is lost.
    double simRelease(ENetPacket* p, ENetPeer* peer, uint8_t channel, std::map<std::pair<ENetPeer*, int>, double>& order, bool& drop) {
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        const bool reliable = (p->flags & ENET_PACKET_FLAG_RELIABLE) != 0;
        double delay = (sim.lagMs + sim.jitterMs * (unit(simRng) * 2.0 - 1.0)) / 1000.0;
        delay = std::max(0.0, delay);
        drop = false;
        if (unit(simRng) * 100.0 < sim.lossPercent) {
            if (!reliable) { drop = true; return 0; }
            delay += 2.0 * sim.lagMs / 1000.0 + RESEND_EXTRA;
        }
        double& last = order[{peer, (int)channel}];
        const double release = std::max(steadyNow() + delay, last);
        last = release;
        return release;
    }

    // Sends a packet made for this one peer, through the simulation when it is on.
    void transmit(ENetPeer* peer, uint8_t channel, ENetPacket* p) {
        if (!peer) { enet_packet_destroy(p); return; }
        if (sim.active()) {
            bool drop;
            const double release = simRelease(p, peer, channel, queueOut, drop);
            if (drop) { enet_packet_destroy(p); return; }
            outgoing.push_back({release, peer, channel, p});
            return;
        }
        if (enet_peer_send(peer, channel, p) < 0 && p->referenceCount == 0) enet_packet_destroy(p);
    }

    void releaseOutgoing() {
        if (outgoing.empty()) return;
        std::stable_sort(outgoing.begin(), outgoing.end(), [](const Held& a, const Held& b) { return a.release < b.release; });
        const double t = steadyNow();
        size_t n = 0;
        for (; n < outgoing.size() && outgoing[n].release <= t; ++n) {
            Held& h = outgoing[n];
            if (h.peer->state != ENET_PEER_STATE_CONNECTED || enet_peer_send(h.peer, h.channel, h.packet) < 0) {
                if (h.packet->referenceCount == 0) enet_packet_destroy(h.packet);
            }
        }
        outgoing.erase(outgoing.begin(), outgoing.begin() + (long)n);
    }

    void forgetPeer(ENetPeer* peer) {
        for (std::vector<Held>* q : {&outgoing, &incoming}) {
            for (Held& h : *q)
                if (h.peer == peer) { enet_packet_destroy(h.packet); h.packet = nullptr; }
            q->erase(std::remove_if(q->begin(), q->end(), [](const Held& h) { return !h.packet; }), q->end());
        }
        for (auto* order : {&queueOut, &queueIn})
            for (auto it = order->begin(); it != order->end();) it = it->first.first == peer ? order->erase(it) : std::next(it);
    }

    void sendTo(ENetPeer* peer, const Writer& w, uint8_t channel, enet_uint32 flags) {
        if (peer) transmit(peer, channel, w.packet(flags));
    }
    void sendToClients(const Writer& w, uint8_t channel, enet_uint32 flags, int except = -1) {
        for (auto& kv : peers)
            if (kv.first != except) transmit(kv.second, channel, w.packet(flags));
    }

    void tunePeer(ENetPeer* peer) {
        // ENet slows unreliable traffic when round trips vary, which on a LAN with sub-millisecond
        // pings throws away car states for no reason. Never throttle, and notice a dead peer in seconds.
        enet_peer_throttle_configure(peer, ENET_PEER_PACKET_THROTTLE_INTERVAL, ENET_PEER_PACKET_THROTTLE_SCALE, 0);
        enet_peer_timeout(peer, 0, 3000, 6000);
    }

    void broadcastLobby() {
        Writer w(M_LOBBY);
        w.u8((uint32_t)lobby.track);
        w.u8((uint32_t)lobby.cpuCars);
        w.u8(lobby.racing ? 1 : 0);
        w.u8((uint32_t)lobby.players.size());
        for (const NetPlayer& p : lobby.players) {
            w.u8((uint32_t)p.slot);
            w.str(p.name);
            w.u8(p.bot ? 1 : 0);
            w.u8((uint32_t)p.color);
        }
        sendToClients(w, CH_RELIABLE, ENET_PACKET_FLAG_RELIABLE);
    }

    void reject(ENetPeer* peer, const std::string& why) {
        Writer w(M_REJECT);
        w.str(why);
        sendTo(peer, w, CH_RELIABLE, ENET_PACKET_FLAG_RELIABLE);
        enet_peer_disconnect_later(peer, 0);
    }

    int slotOf(ENetPeer* peer) const { return (int)(intptr_t)peer->data; }

    // ---- host
    void onHello(ENetPeer* peer, Reader& r) {
        const uint32_t magic = r.u32();
        const uint32_t protocol = r.u16();
        const uint32_t tracks = r.u16();
        NetPlayer p;
        p.name = clean(r.str());
        p.bot = r.u8() != 0;
        p.color = (int)r.u8();
        if (!r.ok() || magic != MAGIC) { reject(peer, "NOT A PINT-SIZE PRIX GAME"); return; }
        if (protocol != PROTOCOL || tracks != allTracks().size()) { reject(peer, "DIFFERENT GAME VERSIONS"); return; }
        if (slotOf(peer) > 0) return;   // already in
        if ((int)lobby.players.size() >= MAX_CARS) { reject(peer, "THE RACE IS FULL"); return; }
        int slot = 1;
        while (peers.count(slot)) ++slot;
        p.slot = slot;
        // Two machines can prefer the same driver: give a newcomer the first one free.
        auto taken = [&](int c) {
            for (const NetPlayer& o : lobby.players) if (o.color == c) return true;
            return false;
        };
        if (p.color < 0 || p.color >= NUM_DRIVERS || taken(p.color))
            for (int k = 0; k < NUM_DRIVERS; ++k)
                if (!taken(k)) { p.color = k; break; }
        if (p.name.empty()) p.name = p.bot ? "BOT" : "PLAYER";
        peer->data = (void*)(intptr_t)slot;
        peers[slot] = peer;
        lobby.players.push_back(p);
        lobby.cpuCars = std::min(lobby.cpuCars, MAX_CARS - (int)lobby.players.size());

        Writer w(M_WELCOME);
        w.u8((uint32_t)slot);
        w.str(me.name);
        w.f64(now());
        sendTo(peer, w, CH_RELIABLE, ENET_PACKET_FLAG_RELIABLE);
        broadcastLobby();
    }

    void onLeave(int slot) {
        clientRtt.erase(slot);
        auto it = peers.find(slot);
        if (it != peers.end()) {
            it->second->data = nullptr;   // its disconnect event, if one still comes, is then ignored
            peers.erase(it);
        }
        lobby.players.erase(std::remove_if(lobby.players.begin(), lobby.players.end(), [&](const NetPlayer& p) { return p.slot == slot; }),
                            lobby.players.end());
        if (announced && members.count(slot)) {
            members.erase(slot);
            readySlots.erase(slot);
            leftSlots.push_back(slot);
        }
        broadcastLobby();   // poll() rechecks whether the race can start without it
    }

    void checkGo() {
        if (!isHost || !announced || goTime >= 0 || !built) return;
        bool all = true;
        for (int s : members) all = all && (s == 0 || readySlots.count(s));
        if (!all && now() - announcedAt < READY_TIMEOUT) return;
        if (!all) {
            // Whoever is still building is too slow: drop them, and drive their cars here.
            for (int s : std::set<int>(members))
                if (s != 0 && !readySlots.count(s) && peers.count(s)) {
                    reject(peers[s], "TOOK TOO LONG TO START");
                    onLeave(s);
                }
        }
        goTime = now() + GO_DELAY;
        Writer w(M_GO);
        w.u8((uint32_t)raceId);
        w.f64(goTime);
        sendToClients(w, CH_RELIABLE, ENET_PACKET_FLAG_RELIABLE);
    }

    // ---- client
    void onWelcome(Reader& r) {
        const int slot = (int)r.u8();
        std::string name = r.str();
        const double hostNow = r.f64();
        if (!r.ok()) return;
        self = slot;
        hostName = name;
        offset = hostNow - steadyNow();   // refined by the pings
        connected = true;
        nextPing = 0;
    }

    void onLobby(Reader& r) {
        NetLobby l;
        l.track = (int)r.u8();
        l.cpuCars = (int)r.u8();
        l.racing = r.u8() != 0;
        const int n = (int)r.u8();
        for (int i = 0; i < n && r.ok(); ++i) {
            NetPlayer p;
            p.slot = (int)r.u8();
            p.name = clean(r.str());
            p.bot = r.u8() != 0;
            p.color = (int)r.u8();
            l.players.push_back(p);
        }
        if (!r.ok()) return;
        if (l.track >= (int)allTracks().size()) l.track = 0;
        if (announced && !l.racing) {   // the host went back to the lobby
            announced = haveStart = built = false;
            goTime = -1;
        }
        lobby = l;
    }

    void onStart(Reader& r) {
        NetStart s;
        const int id = (int)r.u8();
        s.track = (int)r.u8();
        s.samples = (int)r.u32();
        RaceConfig& c = s.cfg;
        c.mode = RaceMode::Circuit;
        c.numCars = (int)r.u8();
        c.humans = (int)r.u8();
        c.laps = (int)r.u8();
        c.seed = r.u32();
        c.aiSkill = r.f32();
        if (c.numCars < 1 || c.numCars > MAX_CARS || s.track >= (int)allTracks().size()) return;
        for (int i = 0; i < c.numCars; ++i) c.colors[i] = (int)r.u8();
        for (int i = 0; i < c.numCars; ++i) c.driver[i] = (int)r.u8();
        if (!r.ok()) return;
        c.self = self;
        raceId = id;
        start = s;
        announced = haveStart = true;
        built = false;
        goTime = -1;
        lastStateTick.clear();
    }

    void onPong(Reader& r) {
        const double t0 = r.f64(), t1 = r.f64();
        if (!r.ok()) return;
        const double t3 = steadyNow();
        const double rtt = t3 - t0;
        if (rtt < 0 || rtt > 2.0) return;
        clockSamples.push_back({rtt, t1 + rtt * 0.5 - t3});
        if (clockSamples.size() > CLOCK_SAMPLES) clockSamples.pop_front();
        // The quickest round trip has the least room for the request and reply to have taken different times.
        const ClockSample* best = &clockSamples.front();
        for (const ClockSample& s : clockSamples) if (s.rtt < best->rtt) best = &s;
        offset = best->offset;
        stats.rttMs = (float)(rtt * 1000.0);
        stats.clockMs = (float)(best->rtt * 500.0);
    }

    // ---- race news, both ways
    void onRaceNews(uint8_t type, Reader& r, ENetPacket* raw, int fromSlot, Race* race) {
        RaceNews n;
        const int id = (int)r.u8();
        n.from = (int)r.u8();
        n.tick = r.u32();
        const double stamp = r.f64();
        if (!r.ok() || id != raceId || !announced) return;
        if (isHost) {
            if (n.from != fromSlot) return;   // a client speaks only for itself
            // Pass it on to the other clients as it came.
            for (auto& kv : peers)
                if (kv.first != fromSlot)
                    transmit(kv.second, type == M_STATES ? CH_STATES : CH_RELIABLE,
                             enet_packet_create(raw->data, raw->dataLength, raw->flags & ENET_PACKET_FLAG_RELIABLE));
        }
        if (type == M_STATES) {
            const int count = (int)r.u8();
            for (int i = 0; i < count && r.ok(); ++i) n.cars.push_back(getCar(r));
        } else {
            int count = (int)r.u8();
            for (int i = 0; i < count && r.ok(); ++i) {
                CarContact c;
                c.from = (int)r.u8();
                c.to = (int)r.u8();
                c.dvel = r.vec();
                c.knock = r.f32();
                c.yaw = r.f32();
                c.pos = r.vec();
                c.strength = r.f32();
                n.contacts.push_back(c);
            }
            count = (int)r.u8();
            for (int i = 0; i < count && r.ok(); ++i) {
                ShellShot s;
                s.id = (int)r.u32();
                s.owner = (int)r.u8();
                s.pos = r.vec();
                s.vel = r.vec();
                s.life = r.f32();
                n.shots.push_back(s);
            }
            count = (int)r.u8();
            for (int i = 0; i < count && r.ok(); ++i) n.shellsGone.push_back((int)r.u32());
            count = (int)r.u8();
            for (int i = 0; i < count && r.ok(); ++i) {
                RaceEvent e{};
                if (getEvent(r, e)) n.events.push_back(e);
            }
            n.hasResult = r.u8() != 0;
            if (n.hasResult) {
                n.over = r.u8() != 0;
                for (int& p : n.places) p = (int)r.u8();
            }
        }
        if (!r.ok() || !race || !built) return;

        const double age = std::max(0.0, now() - stamp);
        if (type == M_STATES) {
            auto it = lastStateTick.find(n.from);
            if (it != lastStateTick.end()) {
                if (n.tick <= it->second) return;   // ENet drops stale states, but be sure
                stats.statesLost += (int)std::min<uint32_t>(n.tick - it->second - 1, 1000);
            }
            lastStateTick[n.from] = n.tick;
            ++stats.statesIn;
            stats.ageSumMs += age * 1000.0;
            ++stats.ageCount;
        }
        // Measured in steps, which every machine makes at the same moments: the news is as old as
        // the steps between the one that made it and the one this machine makes next.
        const double ticksOld = (double)race->ticks() + 1.0 - (double)n.tick;
        race->apply(n, (float)std::max(-1.0, std::min(ticksOld * DT, 1.0)));
    }

    void onReceive(ENetPeer* peer, ENetPacket* packet, Race* race) {
        Reader r(packet->data, packet->dataLength);
        const uint8_t type = (uint8_t)r.u8();
        if (isHost) {
            const int slot = slotOf(peer);
            if (type == M_HELLO) { onHello(peer, r); return; }
            if (slot <= 0) return;   // not introduced yet
            switch (type) {
            case M_READY:
                if ((int)r.u8() == raceId && r.ok() && announced) { readySlots.insert(slot); checkGo(); }
                break;
            case M_PING: {
                const double t0 = r.f64();
                const float rtt = r.f32();   // the client's own measurement, which includes any simulated lag
                if (!r.ok()) break;
                clientRtt[slot] = rtt;
                Writer w(M_PONG);
                w.f64(t0);
                w.f64(now());
                sendTo(peer, w, CH_CLOCK, ENET_PACKET_FLAG_UNSEQUENCED);
                break;
            }
            case M_STATES:
            case M_NEWS: onRaceNews(type, r, packet, slot, race); break;
            default: break;
            }
            return;
        }
        switch (type) {
        case M_WELCOME: onWelcome(r); break;
        case M_REJECT: fail(r.str()); break;
        case M_LOBBY: onLobby(r); break;
        case M_START: onStart(r); break;
        case M_GO:
            if ((int)r.u8() == raceId) {
                const double t = r.f64();
                if (r.ok() && announced) goTime = t;
            }
            break;
        case M_PONG: onPong(r); break;
        case M_STATES:
        case M_NEWS: onRaceNews(type, r, packet, 0, race); break;
        default: break;
        }
    }

    void serviceDiscovery() {
        if (discovery == ENET_SOCKET_NULL) return;
        for (int i = 0; i < 16; ++i) {
            uint8_t buf[64];
            ENetBuffer b;
            b.data = buf;
            b.dataLength = sizeof buf;
            ENetAddress from;
            const int got = enet_socket_receive(discovery, &from, &b, 1);
            if (got <= 0) break;
            Reader r(buf, (size_t)got);
            if (r.u32() != PROBE || r.u16() != PROTOCOL || !r.ok()) continue;
            Writer w(0);
            w.u32(ANSWER);
            w.u16(PROTOCOL);
            w.u16(port);
            w.str(me.name);
            w.u8((uint32_t)lobby.players.size());
            w.u8((uint32_t)lobby.track);
            w.u8(lobby.racing ? 1 : 0);
            ENetBuffer out;
            out.data = (void*)(w.bytes().data() + 1);   // skip the message type byte Writer starts with
            out.dataLength = w.bytes().size() - 1;
            enet_socket_send(discovery, &from, &out, 1);
        }
    }

    void shutdown() {
        if (discovery != ENET_SOCKET_NULL) { enet_socket_destroy(discovery); discovery = ENET_SOCKET_NULL; }
        for (std::vector<Held>* q : {&outgoing, &incoming})
            for (Held& h : *q) enet_packet_destroy(h.packet);
        outgoing.clear();
        incoming.clear();
        queueOut.clear();
        queueIn.clear();
        if (!host) return;
        // Say goodbye so the others notice at once instead of after a timeout.
        for (size_t i = 0; i < host->peerCount; ++i) {
            ENetPeer* p = &host->peers[i];
            if (p->state == ENET_PEER_STATE_CONNECTED) enet_peer_disconnect_now(p, 0);
        }
        enet_host_flush(host);
        enet_host_destroy(host);
        host = nullptr;
        server = nullptr;
        peers.clear();
    }
};

NetSession::NetSession() : impl_(std::make_unique<Impl>()) {}
NetSession::~NetSession() { close(); }

bool NetSession::host(const NetPlayer& me, uint16_t port, std::string& error, const char* bindAddress, bool visible) {
    close();
    Impl& m = *impl_;
    const NetConditions sim = m.sim;   // the simulated connection outlives sessions
    m = Impl();
    m.sim = sim;
    if (!netInit()) { error = "NETWORK UNAVAILABLE"; return false; }
    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;
    if (bindAddress && enet_address_set_host(&addr, bindAddress) != 0) { error = "BAD ADDRESS"; return false; }
    m.host = enet_host_create(&addr, MAX_PEERS, CHANNELS, 0, 0);
    if (!m.host) { error = "PORT " + std::to_string(port) + " IS IN USE"; return false; }
    m.isHost = m.connected = true;
    m.port = port;
    m.epoch = steadyNow();
    m.me = me;
    m.me.slot = 0;
    m.me.name = clean(me.name.empty() ? "HOST" : me.name);
    m.self = 0;
    m.hostName = m.me.name;
    m.lobby.players.push_back(m.me);
    m.lobby.cpuCars = 2;

    if (visible) {
        m.discovery = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
        if (m.discovery != ENET_SOCKET_NULL) {
            ENetAddress d;
            d.host = ENET_HOST_ANY;
            d.port = NET_DISCOVERY_PORT;
            enet_socket_set_option(m.discovery, ENET_SOCKOPT_NONBLOCK, 1);
            if (enet_socket_bind(m.discovery, &d) != 0) {   // another host on this machine already answers
                enet_socket_destroy(m.discovery);
                m.discovery = ENET_SOCKET_NULL;
            }
        }
    }
    return true;
}

bool NetSession::join(const NetPlayer& me, const std::string& address, uint16_t port, std::string& error) {
    close();
    Impl& m = *impl_;
    const NetConditions sim = m.sim;   // the simulated connection outlives sessions
    m = Impl();
    m.sim = sim;
    if (!netInit()) { error = "NETWORK UNAVAILABLE"; return false; }
    ENetAddress addr;
    if (enet_address_set_host(&addr, address.c_str()) != 0) { error = "UNKNOWN HOST " + clean(address); return false; }
    addr.port = port;
    m.host = enet_host_create(nullptr, 1, CHANNELS, 0, 0);
    if (!m.host) { error = "NETWORK UNAVAILABLE"; return false; }
    m.server = enet_host_connect(m.host, &addr, CHANNELS, 0);
    if (!m.server) { error = "COULD NOT CONNECT"; close(); return false; }
    m.me = me;
    m.me.name = clean(me.name);
    m.connectDeadline = steadyNow() + CONNECT_TIMEOUT;
    m.hostName = clean(address);
    return true;
}

void NetSession::close() { impl_->shutdown(); }

void NetSession::poll(Race* race) {
    Impl& m = *impl_;
    if (!m.host || m.closed) return;
    m.serviceDiscovery();
    m.releaseOutgoing();

    ENetEvent ev;
    while (!m.closed && enet_host_service(m.host, &ev, 0) > 0) {
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT:
            m.tunePeer(ev.peer);
            if (!m.isHost) {
                Writer w(M_HELLO);
                w.u32(MAGIC);
                w.u16(PROTOCOL);
                w.u16((uint32_t)allTracks().size());
                w.str(m.me.name);
                w.u8(m.me.bot ? 1 : 0);
                w.u8((uint32_t)m.me.color);
                m.sendTo(ev.peer, w, CH_RELIABLE, ENET_PACKET_FLAG_RELIABLE);
            }
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            m.forgetPeer(ev.peer);
            if (m.isHost) {
                const int slot = m.slotOf(ev.peer);
                ev.peer->data = nullptr;
                if (slot > 0) m.onLeave(slot);
            } else {
                m.fail(m.connected ? "THE HOST LEFT" : "COULD NOT REACH THE HOST");
            }
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            if (m.sim.active()) {
                bool drop;
                const double release = m.simRelease(ev.packet, ev.peer, ev.channelID, m.queueIn, drop);
                if (drop) enet_packet_destroy(ev.packet);
                else m.incoming.push_back({release, ev.peer, ev.channelID, ev.packet});
                break;
            }
            m.onReceive(ev.peer, ev.packet, race);
            enet_packet_destroy(ev.packet);
            break;
        default: break;
        }
    }
    if (!m.incoming.empty() && !m.closed) {
        std::stable_sort(m.incoming.begin(), m.incoming.end(), [](const Impl::Held& a, const Impl::Held& b) { return a.release < b.release; });
        const double t = steadyNow();
        size_t n = 0;
        while (n < m.incoming.size() && m.incoming[n].release <= t && !m.closed) {
            Impl::Held h = m.incoming[n++];
            m.onReceive(h.peer, h.packet, race);
            enet_packet_destroy(h.packet);
        }
        m.incoming.erase(m.incoming.begin(), m.incoming.begin() + (long)n);
    }
    if (m.closed) { m.shutdown(); return; }

    const double t = steadyNow();
    if (!m.isHost) {
        if (!m.connected && t > m.connectDeadline) { m.fail("COULD NOT REACH THE HOST"); m.shutdown(); return; }
        if (m.connected && t >= m.nextPing) {
            Writer w(M_PING);
            w.f64(t);
            w.f32(m.stats.rttMs);
            m.sendTo(m.server, w, CH_CLOCK, ENET_PACKET_FLAG_UNSEQUENCED);
            m.nextPing = t + (m.pings++ < FAST_PINGS ? PING_FAST : PING_SLOW);
        }
    } else {
        m.checkGo();
        if (race && m.built && !m.leftSlots.empty()) {
            for (int slot : m.leftSlots)
                for (const Car& c : race->cars())
                    if (c.driver == slot) race->takeOver(c.id);
            m.leftSlots.clear();
        }
        float worst = 0;
        for (auto& kv : m.peers) {
            auto it = m.clientRtt.find(kv.first);
            worst = std::max(worst, it != m.clientRtt.end() ? it->second : (float)kv.second->roundTripTime);
        }
        m.stats.rttMs = worst;
    }
    enet_host_flush(m.host);
}

void NetSession::send(const Race& race) {
    Impl& m = *impl_;
    if (!m.host || m.closed || !m.announced) return;
    const RaceNews& n = race.news();
    const double stamp = m.now();
    auto header = [&](Writer& w) {
        w.u8((uint32_t)m.raceId);
        w.u8((uint32_t)m.self);
        w.u32(n.tick);
        w.f64(stamp);
    };
    auto deliver = [&](const Writer& w, uint8_t channel, enet_uint32 flags) {
        if (m.isHost) m.sendToClients(w, channel, flags);
        else m.sendTo(m.server, w, channel, flags);
    };

    if (!n.cars.empty()) {
        Writer w(M_STATES);
        header(w);
        w.u8((uint32_t)n.cars.size());
        for (const CarSnapshot& s : n.cars) putCar(w, s);
        deliver(w, CH_STATES, 0);
        ++m.stats.statesOut;
    }
    if (n.hasReliablePart()) {
        Writer w(M_NEWS);
        header(w);
        w.u8((uint32_t)std::min<size_t>(n.contacts.size(), 255));
        for (size_t i = 0; i < n.contacts.size() && i < 255; ++i) {
            const CarContact& c = n.contacts[i];
            w.u8((uint32_t)c.from);
            w.u8((uint32_t)c.to);
            w.vec(c.dvel);
            w.f32(c.knock);
            w.f32(c.yaw);
            w.vec(c.pos);
            w.f32(c.strength);
        }
        w.u8((uint32_t)std::min<size_t>(n.shots.size(), 255));
        for (size_t i = 0; i < n.shots.size() && i < 255; ++i) {
            const ShellShot& s = n.shots[i];
            w.u32((uint32_t)s.id);
            w.u8((uint32_t)s.owner);
            w.vec(s.pos);
            w.vec(s.vel);
            w.f32(s.life);
        }
        w.u8((uint32_t)std::min<size_t>(n.shellsGone.size(), 255));
        for (size_t i = 0; i < n.shellsGone.size() && i < 255; ++i) w.u32((uint32_t)n.shellsGone[i]);
        w.u8((uint32_t)std::min<size_t>(n.events.size(), 255));
        for (size_t i = 0; i < n.events.size() && i < 255; ++i) putEvent(w, n.events[i]);
        w.u8(n.hasResult ? 1 : 0);
        if (n.hasResult) {
            w.u8(n.over ? 1 : 0);
            for (int p : n.places) w.u8((uint32_t)p);
        }
        deliver(w, CH_RELIABLE, ENET_PACKET_FLAG_RELIABLE);
    }
    m.releaseOutgoing();
    enet_host_flush(m.host);
}

bool NetSession::isHost() const { return impl_->isHost; }
bool NetSession::connected() const { return impl_->connected && !impl_->closed; }
bool NetSession::visible() const { return impl_->discovery != ENET_SOCKET_NULL; }
bool NetSession::closed() const { return impl_->closed; }
const std::string& NetSession::reason() const { return impl_->reason; }
int NetSession::self() const { return impl_->self; }
const NetLobby& NetSession::lobby() const { return impl_->lobby; }
const std::string& NetSession::hostName() const { return impl_->hostName; }
NetStats NetSession::stats() const { return impl_->stats; }
double NetSession::now() const { return impl_->now(); }

void NetSession::setConditions(const NetConditions& c) {
    NetConditions& sim = impl_->sim;
    sim.lagMs = std::max(0.0f, c.lagMs);
    sim.jitterMs = std::max(0.0f, c.jitterMs);
    sim.lossPercent = std::max(0.0f, std::min(100.0f, c.lossPercent));
}

const NetConditions& NetSession::conditions() const { return impl_->sim; }

void NetSession::setTrack(int track) {
    Impl& m = *impl_;
    if (!m.isHost || m.announced) return;
    m.lobby.track = std::max(0, std::min(track, (int)allTracks().size() - 1));
    m.broadcastLobby();
}

void NetSession::setCpuCars(int n) {
    Impl& m = *impl_;
    if (!m.isHost || m.announced) return;
    m.lobby.cpuCars = std::max(0, std::min(n, maxCpuCars()));
    m.broadcastLobby();
}

int NetSession::maxCpuCars() const { return MAX_CARS - (int)impl_->lobby.players.size(); }

bool NetSession::startRace(uint32_t seed, int samples, int laps) {
    Impl& m = *impl_;
    if (!m.isHost || m.announced || !m.host) return false;
    ++m.raceId;
    m.raceId &= 0xFF;

    // People first, because cars 0..humans-1 are the human ones; then bots; then the host's fillers.
    std::vector<NetPlayer> order = m.lobby.players;
    std::stable_sort(order.begin(), order.end(), [](const NetPlayer& a, const NetPlayer& b) { return !a.bot && b.bot; });
    NetStart s;
    s.track = m.lobby.track;
    s.samples = samples;
    RaceConfig& c = s.cfg;
    c.mode = RaceMode::Circuit;
    c.laps = laps > 0 ? std::min(laps, 99) : allTracks()[s.track].laps;
    c.seed = seed;
    c.aiSkill = AI_SKILL;
    c.humans = 0;
    c.numCars = 0;
    bool used[8] = {};
    for (const NetPlayer& p : order) {
        c.driver[c.numCars] = p.slot;
        c.humans += p.bot ? 0 : 1;
        int color = p.color >= 0 && p.color < NUM_DRIVERS && !used[p.color] ? p.color : -1;
        for (int k = 0; color < 0 && k < NUM_DRIVERS; ++k) if (!used[k]) color = k;
        used[color] = true;
        c.colors[c.numCars++] = color;
    }
    const int fill = std::min(m.lobby.cpuCars, MAX_CARS - c.numCars);
    for (int i = 0; i < fill; ++i) {
        int color = 0;
        while (used[color]) ++color;
        used[color] = true;
        c.driver[c.numCars] = 0;
        c.colors[c.numCars++] = color;
    }

    m.members.clear();
    for (const NetPlayer& p : order) m.members.insert(p.slot);
    m.readySlots.clear();
    m.leftSlots.clear();
    m.lastStateTick.clear();
    m.announced = m.haveStart = true;
    m.built = false;
    m.goTime = -1;
    m.announcedAt = m.now();
    m.lobby.racing = true;
    c.self = 0;
    m.start = s;

    Writer w(M_START);
    w.u8((uint32_t)m.raceId);
    w.u8((uint32_t)s.track);
    w.u32((uint32_t)samples);
    w.u8((uint32_t)c.numCars);
    w.u8((uint32_t)c.humans);
    w.u8((uint32_t)c.laps);
    w.u32(c.seed);
    w.f32(c.aiSkill);
    for (int i = 0; i < c.numCars; ++i) w.u8((uint32_t)c.colors[i]);
    for (int i = 0; i < c.numCars; ++i) w.u8((uint32_t)c.driver[i]);
    m.sendToClients(w, CH_RELIABLE, ENET_PACKET_FLAG_RELIABLE);
    m.broadcastLobby();
    enet_host_flush(m.host);
    return true;
}

void NetSession::endRace() {
    Impl& m = *impl_;
    if (!m.isHost || !m.announced) return;
    m.announced = m.haveStart = m.built = false;
    m.goTime = -1;
    m.lobby.racing = false;
    m.lobby.cpuCars = std::min(m.lobby.cpuCars, maxCpuCars());
    m.broadcastLobby();
    if (m.host) enet_host_flush(m.host);
}

bool NetSession::takeStart(NetStart& start) {
    Impl& m = *impl_;
    if (!m.haveStart) return false;
    m.haveStart = false;
    start = m.start;
    return true;
}

void NetSession::ready() {
    Impl& m = *impl_;
    if (!m.announced || m.built) return;
    m.built = true;
    if (m.isHost) {
        m.checkGo();
    } else {
        Writer w(M_READY);
        w.u8((uint32_t)m.raceId);
        m.sendTo(m.server, w, CH_RELIABLE, ENET_PACKET_FLAG_RELIABLE);
    }
    if (m.host) enet_host_flush(m.host);
}

bool NetSession::started() const { return impl_->announced && impl_->goTime >= 0 && impl_->now() >= impl_->goTime; }

uint32_t NetSession::ticksDue() const {
    if (!started()) return 0;
    return (uint32_t)std::floor((impl_->now() - impl_->goTime) / DT) + 1;
}
bool NetSession::inRace() const { return impl_->announced; }

// ---------------------------------------------------------------- LAN search

struct NetSearch::Impl {
    ENetSocket sock = ENET_SOCKET_NULL;
    double nextProbe = 0;
    std::vector<NetFoundHost> hosts;
    std::vector<double> seen;
};

NetSearch::NetSearch() : impl_(std::make_unique<Impl>()) {
    if (!netInit()) return;
    Impl& m = *impl_;
    m.sock = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (m.sock == ENET_SOCKET_NULL) return;
    enet_socket_set_option(m.sock, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(m.sock, ENET_SOCKOPT_BROADCAST, 1);
    ENetAddress any;
    any.host = ENET_HOST_ANY;
    any.port = ENET_PORT_ANY;
    if (enet_socket_bind(m.sock, &any) != 0) {
        enet_socket_destroy(m.sock);
        m.sock = ENET_SOCKET_NULL;
    }
}

NetSearch::~NetSearch() {
    if (impl_->sock != ENET_SOCKET_NULL) enet_socket_destroy(impl_->sock);
}

bool NetSearch::ok() const { return impl_->sock != ENET_SOCKET_NULL; }
const std::vector<NetFoundHost>& NetSearch::hosts() const { return impl_->hosts; }

void NetSearch::poll() {
    Impl& m = *impl_;
    if (m.sock == ENET_SOCKET_NULL) return;
    const double t = steadyNow();
    if (t >= m.nextProbe) {
        m.nextProbe = t + SEARCH_INTERVAL;
        uint8_t probe[6];
        for (int i = 0; i < 4; ++i) probe[i] = (uint8_t)(PROBE >> (8 * i));
        probe[4] = (uint8_t)(PROTOCOL & 0xFF);
        probe[5] = (uint8_t)(PROTOCOL >> 8);
        ENetBuffer b;
        b.data = probe;
        b.dataLength = sizeof probe;
        ENetAddress to;
        to.port = NET_DISCOVERY_PORT;
        to.host = ENET_HOST_BROADCAST;
        enet_socket_send(m.sock, &to, &b, 1);
        enet_address_set_host_ip(&to, "127.0.0.1");   // a host on this machine, which broadcasts may not reach
        to.port = NET_DISCOVERY_PORT;
        enet_socket_send(m.sock, &to, &b, 1);
    }

    for (int i = 0; i < 32; ++i) {
        uint8_t buf[512];
        ENetBuffer b;
        b.data = buf;
        b.dataLength = sizeof buf;
        ENetAddress from;
        const int got = enet_socket_receive(m.sock, &from, &b, 1);
        if (got <= 0) break;
        Reader r(buf, (size_t)got);
        if (r.u32() != ANSWER || r.u16() != PROTOCOL) continue;
        NetFoundHost h;
        h.port = (uint16_t)r.u16();
        h.name = clean(r.str());
        h.players = (int)r.u8();
        h.track = (int)r.u8();
        h.racing = r.u8() != 0;
        if (!r.ok()) continue;
        if (h.track >= (int)allTracks().size()) h.track = 0;
        h.address = ipText(from);
        // A host on this machine answers both the broadcast and the loopback probe: list it once.
        bool known = false;
        for (size_t k = 0; k < m.hosts.size(); ++k) {
            NetFoundHost& o = m.hosts[k];
            const bool same = o.port == h.port && (o.address == h.address || (o.name == h.name && (o.address == "127.0.0.1" || h.address == "127.0.0.1")));
            if (!same) continue;
            if (h.address == "127.0.0.1") h.address = o.address;   // prefer the LAN address
            o = h;
            m.seen[k] = t;
            known = true;
            break;
        }
        if (!known) { m.hosts.push_back(h); m.seen.push_back(t); }
    }

    for (size_t k = 0; k < m.hosts.size();) {
        if (t - m.seen[k] > SEARCH_EXPIRY) {
            m.hosts.erase(m.hosts.begin() + (long)k);
            m.seen.erase(m.seen.begin() + (long)k);
        } else {
            ++k;
        }
    }
}

// ---------------------------------------------------------------- helpers

std::string netDescribe(const NetConditions& c) {
    if (!c.active()) return "NONE";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0fMS +-%.0fMS %.0f%% LOSS", c.lagMs, c.jitterMs, c.lossPercent);
    return buf;
}

std::string netLocalName() {
    netInit();   // Windows needs the socket library started before gethostname
    char buf[256] = {};
    if (gethostname(buf, sizeof buf - 1) != 0 || !buf[0]) return "PLAYER";
    std::string s = buf;
    const size_t dot = s.find('.');
    if (dot != std::string::npos) s.resize(dot);
    return clean(s);
}

bool netParseAddress(const std::string& text, std::string& host, uint16_t& port) {
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos) { host = text; return !host.empty(); }
    host = text.substr(0, colon);
    const std::string p = text.substr(colon + 1);
    if (host.empty() || p.empty() || p.size() > 5 || p.find_first_not_of("0123456789") != std::string::npos) return false;
    const long v = std::strtol(p.c_str(), nullptr, 10);
    if (v <= 0 || v > 65535) return false;
    port = (uint16_t)v;
    return true;
}
