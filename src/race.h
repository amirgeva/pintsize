#pragma once
#include "track.h"
#include <random>
#include <vector>

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
};

struct Car {
    int id = 0;
    bool human = false;
    int humanIndex = -1;
    int color = 0;
    const VehicleSpec* spec = nullptr;

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
};

struct RaceConfig {
    RaceMode mode = RaceMode::Circuit;
    int numCars = 4;
    int humans = 1;
    int colors[4] = {0, 1, 2, 3};
    float aiSkill = 1.0f;
    int laps = 3;
    uint32_t seed = 1;
};

// How far two cars' collision boxes overlap, or 0 if they do not touch.
float carOverlapDepth(const Car& a, const Car& b);

class Race {
public:
    static constexpr int WIN_POINTS = 8;
    static constexpr float COUNTDOWN_TIME = 3.2f;

    Race(const Track& track, const RaceConfig& cfg);

    // Advances one fixed tick. inputs[0..1] are the human players.
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
    int points(int player) const { return points_[player]; }
    int leader() const { return leader_; }
    int matchWinner() const { return matchWinner_; }
    bool suddenDeath() const { return suddenDeath_; }
    std::vector<int> standings() const;

    // Lets the tests stage a collision by placing cars directly.
    Car& carForTest(int i) { return cars_[i]; }

private:
    void stepCar(Car& c, const Input& in);
    void startFall(Car& c, bool sink);
    void respawn(Car& c);
    void placeCar(Car& c, int idx, float lateral);
    void updateProgress(Car& c);
    void resolveWalls(Car& c, Vec2 prevPos);
    bool touchesWall(const Car& c, Vec2 pos) const;
    bool spotIsClear(const Car& c, int idx, float lateral) const;
    void collideCars();
    void separateCars(Car& a, Car& b, Vec2 n, float depth);
    void bounceCars(Car& a, Car& b, Vec2 n);
    void stepShells();
    Input aiInput(Car& c);
    void buildSpeedProfile();
    void updateRubberBand();
    void updateCamera(bool snap);
    void updateHeadToHead();
    void award(int winner);
    void resetRound();
    void checkRaceEnd();
    void emit(EventType t, Vec2 p, int car, float s = 1) { events_.push_back({t, p, car, s}); }

    const Track& track_;
    RaceConfig cfg_;
    std::vector<Car> cars_;
    std::vector<Shell> shells_;
    std::vector<RaceEvent> events_;
    std::vector<float> speedProfile_;
    std::mt19937 rng_;
    Phase phase_ = Phase::Countdown;
    float timer_ = COUNTDOWN_TIME;
    float clock_ = 0;
    float sinceFinished_ = 0;
    int lastBeep_ = 4;
    int nextPlace_ = 1;
    Vec2 cam_;
    int points_[2] = {WIN_POINTS / 2, WIN_POINTS / 2};
    int leader_ = 0;
    int matchWinner_ = -1;
    float roundGrace_ = 0;
    bool suddenDeath_ = false;
};
