#pragma once
#include "track.h"
#include <random>
#include <vector>

constexpr int MAX_CARS = 4;

struct Input {
    bool left = false, right = false, gas = false, brake = false, fire = false;
};

enum class CarState { Drive, Fall };
enum class RaceMode { Circuit, HeadToHead, TimeTrial };
enum class Phase { Countdown, Running, RoundPause, Finished };

enum class EventType { Bump, Fall, Splash, Respawn, Lap, Beep, Go, Shot, Hit, Jump, Land, Point, Wall };

struct RaceEvent {
    EventType type;
    Vec2 pos;
    int car;
    float strength;
    int other = -1;        // the second car in a bump
};

struct Car {
    int id = 0;
    bool human = false;
    int humanIndex = -1;   // which local player's keys drive it; -1 for computer cars and cars driven elsewhere
    int color = 0;
    const VehicleSpec* spec = nullptr;

    // Network play: each car is driven by one machine, and the others show it from the states it sends.
    int driver = 0;        // the machine that drives this car (0 is the host)
    bool remote = false;   // driven by another machine
    float turnRate = 0;    // heading change per second over the last tick, for extrapolating remote cars
    float sinceUpdate = 0; // seconds since a remote car's last state arrived

    Vec2 pos, vel;
    float heading = 0;     // continuous steering angle
    float z = 0, vz = 0;   // height above the ground, for jumps
    bool wasOnRamp = false;
    CarState state = CarState::Drive;
    float fallTimer = 0;
    bool sinking = false;
    float invuln = 0;
    float spin = 0;
    float gunCooldown = 0;
    float skid = 0;
    Surface surface = Surface::Road;

    int idx = 0;           // nearest centerline sample
    int safeIdx = 0;       // where to respawn
    float progress = 0;    // signed distance travelled along the track
    int lapsDone = -1;     // -1 on the grid, 0 once the line is crossed
    float lapTime = 0, bestLap = 0, finishTime = 0;
    bool finished = false;
    int place = 0;
    float speedMul = 1;

    // Collision response
    float knock = 0;       // seconds left of reduced grip after a hit
    float yawKick = 0;     // rad/s of spin from an off-centre hit
    float bumpSound = 0;   // stops one long scrape from rattling the speaker
    float wedged = 0;      // seconds spent unable to get clear of an object

    // AI
    float aiBias = 0, aiStuck = 0, aiReverse = 0;

    // Statistics, used by the tests
    int falls = 0, strays = 0, rescues = 0;
};

struct Shell {
    Vec2 pos, vel;
    float life;
    int owner;
    int id = 0;            // unique within a race, so every machine can remove the same shell
};

struct RaceConfig {
    RaceMode mode = RaceMode::Circuit;
    int numCars = 4;
    int humans = 1;        // cars 0..humans-1 are driven by people
    int colors[MAX_CARS] = {0, 1, 2, 3};
    float aiSkill = 1.0f;
    int laps = 3;
    uint32_t seed = 1;

    // Network play: driver[i] is the machine that drives car i, and self is this machine. Machine 0
    // is the host, which also referees: it decides finishing places and when the race is over.
    // The defaults drive every car here.
    int driver[MAX_CARS] = {0, 0, 0, 0};
    int self = 0;
};

// The part of a car's state that the machine driving it sends to the others.
struct CarSnapshot {
    int id = 0;
    Vec2 pos, vel;
    float heading = 0, turnRate = 0, z = 0, vz = 0;
    CarState state = CarState::Drive;
    bool sinking = false, wasOnRamp = false, finished = false;
    float fallTimer = 0, invuln = 0, spin = 0, knock = 0, yawKick = 0, skid = 0;
    Surface surface = Surface::Road;
    int idx = 0, safeIdx = 0, lapsDone = -1;
    float progress = 0, lapTime = 0, bestLap = 0, finishTime = 0;
};

// A bounce between a car driven here (from) and one driven elsewhere (to), worked out here. The
// machine driving `to` applies the same change to it, unless it already saw the bump itself.
struct CarContact {
    int from = 0, to = 0;
    Vec2 dvel;             // change to to's velocity
    float knock = 0;       // to's grip loss after the hit
    float yaw = 0;         // change to to's spin
    Vec2 pos;
    float strength = 0;
};

struct ShellShot {
    int id = 0, owner = 0;
    Vec2 pos, vel;
    float life = 0;
};

// Everything one race module tells the others after a tick: the states of the cars it drives, plus
// news that must not be lost (bumps, shells, sounds, results). A link carries it to the other
// modules, by a direct call in the same process (LocalLink) or as network messages (NetSession).
struct RaceNews {
    int from = 0;                      // the sending module (machine)
    uint32_t tick = 0;                 // the sender's tick when it was produced
    std::vector<CarSnapshot> cars;
    std::vector<CarContact> contacts;
    std::vector<ShellShot> shots;
    std::vector<int> shellsGone;
    std::vector<RaceEvent> events;
    bool hasResult = false;            // from the referee, when places change or the race ends
    int places[MAX_CARS] = {};
    bool over = false;

    bool hasReliablePart() const {
        return !contacts.empty() || !shots.empty() || !shellsGone.empty() || !events.empty() || hasResult;
    }
    void clear() {
        cars.clear(); contacts.clear(); shots.clear(); shellsGone.clear(); events.clear();
        hasResult = false; over = false;
        for (int& p : places) p = 0;
    }
};

// How far two cars' collision boxes overlap, or 0 if they do not touch.
float carOverlapDepth(const Car& a, const Car& b);

class Race {
public:
    static constexpr int WIN_POINTS = 8;
    static constexpr float COUNTDOWN_TIME = 3.2f;

    Race(const Track& track, const RaceConfig& cfg);

    // Advances one fixed tick. inputs[0..1] are the local human players.
    void step(const Input inputs[2]);

    const Track& track() const { return track_; }
    const RaceConfig& config() const { return cfg_; }
    const std::vector<Car>& cars() const { return cars_; }
    const std::vector<Shell>& shells() const { return shells_; }
    const std::vector<RaceEvent>& events() const { return events_; }
    Phase phase() const { return phase_; }
    float clock() const { return clock_; }
    float countdown() const { return timer_; }
    float sinceFinished() const { return sinceFinished_; }
    Vec2 camera() const { return cam_; }
    int focus() const { return focus_; }     // the car the camera and HUD follow in a circuit race
    int points(int player) const { return points_[player]; }
    int leader() const { return leader_; }
    int matchWinner() const { return matchWinner_; }
    bool suddenDeath() const { return suddenDeath_; }
    std::vector<int> standings() const;
    uint32_t ticks() const { return tick_; }

    // ---- Modules. A race drives the cars whose driver is config().self and shows the rest from the
    // news other modules send. The race never touches the network itself.
    bool networked() const { return networked_; }
    bool referee() const { return cfg_.self == 0; }
    const RaceNews& news() const { return out_; }    // what the last step produced, for the other modules
    // Applies news from another module. `age` is how long ago the sender produced it (seconds);
    // call this between steps, before the next one.
    void apply(const RaceNews& news, float age);
    void takeOver(int car);                          // the module driving it left: drive it here

    // Lets the tests stage a collision by placing cars directly.
    Car& carForTest(int i) { return cars_[i]; }

private:
    void advance(const Input inputs[2]);
    void stepCar(Car& c, const Input& in);
    void coast(Car& c, float t);
    void startFall(Car& c, bool sink);
    void respawn(Car& c);
    void placeCar(Car& c, int idx, float lateral);
    void updateProgress(Car& c);
    void resolveWalls(Car& c, Vec2 prevPos);
    bool touchesWall(const Car& c, Vec2 pos) const;
    bool spotIsClear(const Car& c, int idx, float lateral) const;
    void collideCars();
    void separateCars(Car& a, Car& b, Vec2 n, float depth);
    bool bounceCars(Car& a, Car& b, Vec2 n);
    void stepShells();
    Input aiInput(Car& c);
    void buildSpeedProfile();
    void updateRubberBand();
    void updateCamera(bool snap);
    void updateHeadToHead();
    void award(int winner);
    void resetRound();
    void assignPlaces();
    void checkRaceEnd();
    void collectNews();
    CarSnapshot snapshot(const Car& c) const;
    void applySnapshot(const CarSnapshot& s, float ahead);
    void applyContact(const CarContact& c);
    void applyShot(const ShellShot& s, float ahead);
    void removeShell(int id);
    void applyResult(const int places[MAX_CARS], bool over);
    void emit(EventType t, Vec2 p, int car, float s = 1, int other = -1) { events_.push_back({t, p, car, s, other}); }

    const Track& track_;
    RaceConfig cfg_;
    std::vector<Car> cars_;
    std::vector<Shell> shells_;
    std::vector<RaceEvent> events_;
    std::vector<RaceEvent> pendingEvents_;
    std::vector<float> speedProfile_;
    std::mt19937 rng_;
    Phase phase_ = Phase::Countdown;
    float timer_ = COUNTDOWN_TIME;
    float clock_ = 0;
    float sinceFinished_ = 0;
    int lastBeep_ = 4;
    int nextPlace_ = 1;
    Vec2 cam_;
    int focus_ = 0;
    int points_[2] = {WIN_POINTS / 2, WIN_POINTS / 2};
    int leader_ = 0;
    int matchWinner_ = -1;
    float roundGrace_ = 0;
    bool suddenDeath_ = false;

    uint32_t tick_ = 0;
    bool networked_ = false;
    RaceNews out_;
    int shellSeq_ = 0;
    int lastContact_[MAX_CARS][MAX_CARS];   // tick of each pair's last bounce between modules
    int reportedPlaces_[MAX_CARS] = {};     // the referee's places as last sent
    bool reportedOver_ = false;
    int snapFrom_[MAX_CARS] = {-1, -1, -1, -1};   // which module sent each car's last state, and when
    uint32_t snapTick_[MAX_CARS] = {};
};
