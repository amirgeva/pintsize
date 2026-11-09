#include "game.h"
#include <algorithm>
#include <cstdio>

namespace {
const SDL_Color WHITE{255, 255, 255, 255};
const SDL_Color GREY{150, 158, 176, 255};
const SDL_Color GOLD{255, 208, 70, 255};
const SDL_Color RED{240, 70, 60, 255};
const SDL_Color GREEN{90, 220, 110, 255};
const SDL_Color BG{20, 24, 44, 255};
const SDL_Color SHADE{0, 0, 0, 150};

constexpr int START_LIVES = 3;
constexpr int STREAK_FOR_LIFE = 3;

std::string formatTime(float t) {
    if (t < 0) t = 0;
    int m = (int)(t / 60);
    float s = t - m * 60;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%04.1f", m, s);
    return buf;
}

const char* placeName(int p) {
    static const char* names[] = {"-", "1ST", "2ND", "3RD", "4TH"};
    return names[p >= 0 && p <= 4 ? p : 0];
}
}  // namespace

Game::Game(SDL_Renderer* r, Audio* audio) : gfx_(r), audio_(audio) {
    loadBest();
    audio_->setMusic(true);
}

void Game::loadBest() {
    char* base = SDL_GetPrefPath("PintSize", "Prix");
    if (!base) return;
    std::string path = std::string(base) + "best.dat";
    SDL_free(base);
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        if (std::fread(bestLaps_, sizeof bestLaps_, 1, f) != 1) std::fill(std::begin(bestLaps_), std::end(bestLaps_), 0.0f);
        std::fclose(f);
    }
}

void Game::saveBest() {
    char* base = SDL_GetPrefPath("PintSize", "Prix");
    if (!base) return;
    std::string path = std::string(base) + "best.dat";
    SDL_free(base);
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(bestLaps_, sizeof bestLaps_, 1, f);
        std::fclose(f);
    }
}

void Game::go(Screen s) {
    screen_ = s;
    menu_ = 0;
    bool racing = s == Screen::Racing || s == Screen::Paused;
    audio_->setMusic(!racing);
    if (s != Screen::Racing) {
        audio_->setEngine(0, 0, false);
        audio_->setEngine(1, 0, false);
        audio_->setSkid(0);
    }
}

void Game::showMainMenu() { go(Screen::Main); }

void Game::menuMove(int delta, int count) {
    menu_ = (menu_ + delta + count) % count;
    audio_->play(Sfx::Move);
}

void Game::loadTrack(int index) {
    if (loadedTrack_ == index) return;
    track_ = std::make_unique<Track>(allTracks()[index]);
    gfx_.setTrack(*track_);
    loadedTrack_ = index;
}

void Game::startRace() {
    loadTrack(trackIndex_);
    RaceConfig cfg;
    cfg.laps = track_->def().laps;
    cfg.seed = SDL_GetTicks() | 1u;

    int next = 0;
    auto otherColor = [&](int avoid) {
        if (next == avoid) ++next;
        return next++;
    };

    switch (flow_) {
    case Flow::Tournament:
        cfg.mode = RaceMode::Circuit;
        cfg.numCars = 4;
        cfg.humans = 1;
        cfg.colors[0] = playerColor_;
        for (int i = 1; i < 4; ++i) cfg.colors[i] = otherColor(playerColor_);
        cfg.aiSkill = std::min(1.0f, 0.90f + 0.02f * trackIndex_);
        break;
    case Flow::H2HCpu:
    case Flow::H2HTwo:
        cfg.mode = RaceMode::HeadToHead;
        cfg.numCars = 2;
        cfg.humans = flow_ == Flow::H2HTwo ? 2 : 1;
        cfg.colors[0] = 0;
        cfg.colors[1] = 1;
        cfg.aiSkill = 0.96f;
        break;
    case Flow::Trial:
        cfg.mode = RaceMode::TimeTrial;
        cfg.numCars = 1;
        cfg.humans = 1;
        cfg.colors[0] = playerColor_;
        break;
    }
    race_ = std::make_unique<Race>(*track_, cfg);
    gfx_.resetFx();
    go(Screen::Racing);
}

void Game::startDemo(int trackIndex, RaceMode mode) {
    demo_ = true;
    trackIndex_ = trackIndex;
    loadTrack(trackIndex);
    RaceConfig cfg;
    cfg.mode = mode;
    cfg.humans = 0;
    cfg.numCars = mode == RaceMode::HeadToHead ? 2 : 4;
    cfg.laps = 3;
    race_ = std::make_unique<Race>(*track_, cfg);
    gfx_.resetFx();
    screen_ = Screen::Racing;
}

void Game::beginFlow(Flow f) {
    flow_ = f;
    switch (f) {
    case Flow::Tournament:
        lives_ = START_LIVES;
        winStreak_ = 0;
        trackIndex_ = 0;
        go(Screen::Driver);
        break;
    case Flow::Trial:
        go(Screen::Driver);
        break;
    default:
        go(Screen::TrackSelect);
        break;
    }
}

void Game::activateMain() {
    audio_->play(Sfx::Select);
    switch (menu_) {
    case 0: beginFlow(Flow::Tournament); break;
    case 1: beginFlow(Flow::H2HCpu); break;
    case 2: beginFlow(Flow::H2HTwo); break;
    case 3: beginFlow(Flow::Trial); break;
    case 4: go(Screen::Controls); break;
    default: quit_ = true; break;
    }
}

void Game::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_KEYDOWN && !e.key.repeat) onKey(e.key.keysym.sym);
}

void Game::onKey(SDL_Keycode key) {
    if (key == SDLK_m) { audio_->toggleMute(); return; }
    const bool confirm = key == SDLK_RETURN || key == SDLK_SPACE || key == SDLK_KP_ENTER;
    const int tracks = (int)allTracks().size();

    switch (screen_) {
    case Screen::Title:
        if (confirm) { audio_->play(Sfx::Select); go(Screen::Main); }
        else if (key == SDLK_ESCAPE) quit_ = true;
        break;
    case Screen::Main:
        if (key == SDLK_UP) menuMove(-1, 6);
        else if (key == SDLK_DOWN) menuMove(1, 6);
        else if (confirm) activateMain();
        else if (key == SDLK_ESCAPE) go(Screen::Title);
        break;
    case Screen::Controls:
        if (confirm || key == SDLK_ESCAPE) go(Screen::Main);
        break;
    case Screen::Driver:
        if (key == SDLK_LEFT) { playerColor_ = (playerColor_ + NUM_DRIVERS - 1) % NUM_DRIVERS; audio_->play(Sfx::Move); }
        else if (key == SDLK_RIGHT) { playerColor_ = (playerColor_ + 1) % NUM_DRIVERS; audio_->play(Sfx::Move); }
        else if (confirm) {
            audio_->play(Sfx::Select);
            go(flow_ == Flow::Tournament ? Screen::Intro : Screen::TrackSelect);
        } else if (key == SDLK_ESCAPE) go(Screen::Main);
        break;
    case Screen::TrackSelect:
        if (key == SDLK_LEFT) { trackIndex_ = (trackIndex_ + tracks - 1) % tracks; audio_->play(Sfx::Move); }
        else if (key == SDLK_RIGHT) { trackIndex_ = (trackIndex_ + 1) % tracks; audio_->play(Sfx::Move); }
        else if (confirm) { audio_->play(Sfx::Select); startRace(); }
        else if (key == SDLK_ESCAPE) go(Screen::Main);
        break;
    case Screen::Intro:
        if (confirm) { audio_->play(Sfx::Select); startRace(); }
        else if (key == SDLK_ESCAPE) go(Screen::Main);
        break;
    case Screen::Racing:
        if (key == SDLK_ESCAPE) go(Screen::Paused);
        break;
    case Screen::Paused:
        if (key == SDLK_UP) menuMove(-1, 3);
        else if (key == SDLK_DOWN) menuMove(1, 3);
        else if (key == SDLK_ESCAPE) go(Screen::Racing);
        else if (confirm) {
            audio_->play(Sfx::Select);
            if (menu_ == 0) go(Screen::Racing);
            else if (menu_ == 1) startRace();
            else go(Screen::Main);
        }
        break;
    case Screen::Result:
        if (confirm) {
            audio_->play(Sfx::Select);
            if (lastPlace_ <= 2) {
                if (trackIndex_ + 1 >= tracks) go(Screen::Champion);
                else { ++trackIndex_; go(Screen::Intro); }
            } else if (lives_ <= 0) {
                go(Screen::GameOver);
            } else {
                go(Screen::Intro);
            }
        }
        break;
    case Screen::H2HResult:
    case Screen::TrialResult:
    case Screen::GameOver:
    case Screen::Champion:
        if (confirm || key == SDLK_ESCAPE) { audio_->play(Sfx::Select); go(Screen::Main); }
        break;
    }
}

Input Game::readPlayer(int player) const {
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    Input in;
    const bool two = race_ && race_->config().humans == 2;
    if (!two) {
        // Alone at the keyboard, either side of it works.
        in.left = k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A];
        in.right = k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D];
        in.gas = k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W] || k[SDL_SCANCODE_X];
        in.brake = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S] || k[SDL_SCANCODE_Z];
        in.fire = k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_LSHIFT];
    } else if (player == 0) {
        in.left = k[SDL_SCANCODE_LEFT];
        in.right = k[SDL_SCANCODE_RIGHT];
        in.gas = k[SDL_SCANCODE_UP];
        in.brake = k[SDL_SCANCODE_DOWN];
        in.fire = k[SDL_SCANCODE_RSHIFT];
    } else {
        in.left = k[SDL_SCANCODE_A];
        in.right = k[SDL_SCANCODE_D];
        in.gas = k[SDL_SCANCODE_W];
        in.brake = k[SDL_SCANCODE_S];
        in.fire = k[SDL_SCANCODE_LSHIFT];
    }
    return in;
}

void Game::playEvents() {
    for (const RaceEvent& e : race_->events()) {
        switch (e.type) {
        case EventType::Bump:    audio_->play(Sfx::Bump, e.strength); break;
        case EventType::Wall:    audio_->play(Sfx::Wall, e.strength); break;
        case EventType::Fall:    audio_->play(Sfx::Fall); break;
        case EventType::Splash:  audio_->play(Sfx::Splash); break;
        case EventType::Respawn: audio_->play(Sfx::Respawn); break;
        case EventType::Lap:     audio_->play(e.strength > 1.5f ? Sfx::Finish : Sfx::Lap); break;
        case EventType::Beep:    audio_->play(Sfx::Beep); break;
        case EventType::Go:      audio_->play(Sfx::Go); break;
        case EventType::Shot:    audio_->play(Sfx::Shot); break;
        case EventType::Hit:     audio_->play(Sfx::Hit); break;
        case EventType::Jump:    audio_->play(Sfx::Jump); break;
        case EventType::Land:    audio_->play(Sfx::Land, e.strength); break;
        case EventType::Point:   audio_->play(Sfx::Point); break;
        }
    }

    float skid = 0;
    for (const Car& c : race_->cars()) {
        if (!c.human) continue;
        bool on = c.state == CarState::Drive;
        audio_->setEngine(c.humanIndex, clampf(len(c.vel) / c.spec->topSpeed, 0, 1.2f), on);
        if (on && c.z <= 0 && c.surface != Surface::Rough && c.skid > 62.0f) skid = 1;
    }
    audio_->setSkid(skid);
}

void Game::tick() {
    ++frame_;
    if (screen_ != Screen::Racing || !race_) return;
    Input in[2] = {readPlayer(0), readPlayer(1)};
    if (demo_) in[0] = in[1] = Input{};
    race_->step(in);
    gfx_.updateFx(*race_);
    if (demo_) return;
    playEvents();

    if (race_->phase() == Phase::Finished && race_->sinceFinished() > 2.5f) finishRace();
}

void Game::finishRace() {
    lastOrder_ = race_->standings();
    switch (flow_) {
    case Flow::Tournament: {
        lastPlace_ = 4;
        for (size_t i = 0; i < lastOrder_.size(); ++i)
            if (race_->cars()[lastOrder_[i]].human) lastPlace_ = (int)i + 1;
        extraLifeAwarded_ = false;
        if (lastPlace_ == 1) {
            if (++winStreak_ >= STREAK_FOR_LIFE) { ++lives_; winStreak_ = 0; extraLifeAwarded_ = true; }
        } else {
            winStreak_ = 0;
        }
        if (lastPlace_ > 2) --lives_;
        go(Screen::Result);
        break;
    }
    case Flow::H2HCpu:
    case Flow::H2HTwo:
        go(Screen::H2HResult);
        break;
    case Flow::Trial: {
        const Car& c = race_->cars()[0];
        float& best = bestLaps_[trackIndex_];
        if (c.bestLap > 0 && (best <= 0 || c.bestLap < best)) { best = c.bestLap; saveBest(); }
        go(Screen::TrialResult);
        break;
    }
    }
}

// ---------------------------------------------------------------- drawing

void Game::draw() {
    switch (screen_) {
    case Screen::Title: drawTitle(); break;
    case Screen::Main:
        drawMenu("PINT-SIZE PRIX", {"TOURNAMENT", "HEAD TO HEAD VS CPU", "HEAD TO HEAD 2 PLAYERS", "TIME TRIAL", "CONTROLS", "QUIT"});
        break;
    case Screen::Controls: drawControls(); break;
    case Screen::Driver: drawDriver(); break;
    case Screen::TrackSelect: drawTrackSelect(); break;
    case Screen::Intro: drawIntro(); break;
    case Screen::Racing:
        gfx_.drawRace(*race_);
        drawHud();
        if (race_->phase() == Phase::Countdown) drawCountdown();
        break;
    case Screen::Paused:
        gfx_.drawRace(*race_);
        gfx_.fill(0, 0, VIEW_W, VIEW_H, SHADE);
        gfx_.text(VIEW_W / 2, 70, "PAUSED", GOLD, 3, 1);
        {
            const char* items[3] = {"RESUME", "RESTART RACE", "QUIT TO MENU"};
            for (int i = 0; i < 3; ++i) gfx_.text(VIEW_W / 2, 120 + i * 18, items[i], i == menu_ ? GOLD : WHITE, 1, 1);
        }
        break;
    default: drawResult(); break;
    }
}

void Game::drawMenu(const char* title, const std::vector<std::string>& items) {
    gfx_.clear(BG);
    gfx_.text(VIEW_W / 2, 34, title, GOLD, 4, 1);
    for (int i = 0; i < (int)items.size(); ++i) {
        bool sel = i == menu_;
        int y = 100 + i * 22;
        if (sel) gfx_.fill(VIEW_W / 2 - 110, y - 5, 220, 18, SDL_Color{60, 70, 120, 255});
        gfx_.text(VIEW_W / 2, y, items[i].c_str(), sel ? GOLD : WHITE, 1, 1);
    }
    gfx_.text(VIEW_W / 2, VIEW_H - 16, "UP/DOWN  ENTER  ESC    M: MUTE    F11: FULLSCREEN", GREY, 1, 1);
}

void Game::drawTitle() {
    gfx_.clear(BG);
    gfx_.text(VIEW_W / 2, 66, "PINT-SIZE", GOLD, 6, 1);
    gfx_.text(VIEW_W / 2, 118, "PRIX", WHITE, 6, 1);
    float t = frame_ * 0.02f;
    for (int i = 0; i < NUM_DRIVERS; ++i) {
        float x = std::fmod(t * 60.0f + i * 80.0f, (float)VIEW_W + 40.0f) - 20.0f;
        gfx_.drawCar((VehicleKind)(i % (int)VehicleKind::Count), i, (int)x, 196, 0.0f);
    }
    if ((frame_ / 30) & 1) gfx_.text(VIEW_W / 2, 224, "PRESS ENTER", WHITE, 2, 1);
}

void Game::drawControls() {
    gfx_.clear(BG);
    gfx_.text(VIEW_W / 2, 18, "CONTROLS", GOLD, 3, 1);
    int y = 60;
    auto row = [&](const char* a, const char* b, const char* c) {
        gfx_.text(40, y, a, GREY);
        gfx_.text(170, y, b, WHITE);
        gfx_.text(310, y, c, WHITE);
        y += 14;
    };
    row("", "PLAYER 1", "PLAYER 2");
    y += 4;
    row("STEER", "LEFT / RIGHT", "A / D");
    row("ACCELERATE", "UP", "W");
    row("BRAKE/REVERSE", "DOWN", "S");
    row("FIRE (TANKS)", "RIGHT SHIFT", "LEFT SHIFT");
    y += 8;
    gfx_.text(40, y, "PLAYING ALONE: EITHER SET WORKS. ALSO X = GAS, Z = BRAKE, SPACE = FIRE", GREY);
    y += 20;
    gfx_.text(40, y, "TOURNAMENT: FINISH 1ST OR 2ND TO ADVANCE, OR LOSE A LIFE.", WHITE); y += 12;
    gfx_.text(40, y, "THREE WINS IN A ROW EARNS AN EXTRA LIFE.", WHITE); y += 12;
    gfx_.text(40, y, "HEAD TO HEAD: LEAVE YOUR RIVAL OFF SCREEN TO TAKE A POINT.", WHITE); y += 12;
    gfx_.text(40, y, "FIRST TO ALL 8 WINS, OR MOST POINTS AFTER 3 LAPS.", WHITE);
    gfx_.text(VIEW_W / 2, VIEW_H - 16, "ENTER: BACK", GREY, 1, 1);
}

void Game::drawDriver() {
    gfx_.clear(BG);
    gfx_.text(VIEW_W / 2, 30, "CHOOSE YOUR DRIVER", GOLD, 2, 1);
    for (int i = 0; i < NUM_DRIVERS; ++i) {
        int x = VIEW_W / 2 + (i - NUM_DRIVERS / 2) * 66 + 33;
        bool sel = i == playerColor_;
        if (sel) gfx_.fill(x - 28, 90, 56, 74, SDL_Color{60, 70, 120, 255});
        gfx_.drawCar(VehicleKind::Coupe, i, x, 118, sel ? frame_ * 0.05f : -PI / 2, sel ? 1.5f : 1.0f);
        gfx_.text(x, 150, driverName(i), sel ? GOLD : WHITE, 1, 1);
    }
    gfx_.text(VIEW_W / 2, VIEW_H - 16, "LEFT/RIGHT  ENTER  ESC", GREY, 1, 1);
}

void Game::drawTrackSelect() {
    const TrackDef& def = allTracks()[trackIndex_];
    gfx_.clear(BG);
    gfx_.text(VIEW_W / 2, 16, "SELECT TRACK", GOLD, 2, 1);
    gfx_.drawTrackPreview(def, SDL_Rect{130, 46, 220, 130}, GREEN);
    gfx_.text(VIEW_W / 2, 190, def.name, WHITE, 2, 1);
    std::string sub = std::string(themeInfo(def.theme).name) + " - " + vehicleSpec(def.vehicle).name;
    gfx_.text(VIEW_W / 2, 212, sub.c_str(), GREY, 1, 1);
    if (flow_ == Flow::Trial && bestLaps_[trackIndex_] > 0) {
        std::string best = "BEST LAP " + formatTime(bestLaps_[trackIndex_]);
        gfx_.text(VIEW_W / 2, 228, best.c_str(), GOLD, 1, 1);
    }
    gfx_.text(VIEW_W / 2, VIEW_H - 16, "LEFT/RIGHT  ENTER  ESC", GREY, 1, 1);
}

void Game::drawIntro() {
    const TrackDef& def = allTracks()[trackIndex_];
    gfx_.clear(BG);
    char round[32];
    std::snprintf(round, sizeof round, "ROUND %d OF %d", trackIndex_ + 1, (int)allTracks().size());
    gfx_.text(VIEW_W / 2, 16, round, GOLD, 2, 1);
    gfx_.drawTrackPreview(def, SDL_Rect{130, 46, 220, 120}, GREEN);
    gfx_.text(VIEW_W / 2, 178, def.name, WHITE, 2, 1);
    std::string sub = std::string(themeInfo(def.theme).name) + " - " + vehicleSpec(def.vehicle).name;
    gfx_.text(VIEW_W / 2, 200, sub.c_str(), GREY, 1, 1);
    for (int i = 0; i < lives_; ++i) gfx_.drawCar(VehicleKind::Coupe, playerColor_, VIEW_W / 2 - (lives_ - 1) * 12 + i * 24, 226, -PI / 2);
    gfx_.text(VIEW_W / 2, VIEW_H - 16, "FINISH 1ST OR 2ND - PRESS ENTER", WHITE, 1, 1);
}

void Game::drawHud() {
    const auto& cars = race_->cars();
    const RaceConfig& cfg = race_->config();
    char buf[64];

    if (cfg.mode == RaceMode::HeadToHead) {
        const int w = 12, gap = 3;
        const int total = Race::WIN_POINTS * (w + gap) - gap;
        int x = VIEW_W / 2 - total / 2;
        gfx_.fill(x - 4, 4, total + 8, 16, SHADE);
        for (int i = 0; i < Race::WIN_POINTS; ++i)
            gfx_.fill(x + i * (w + gap), 8, w, 8, driverColor(i < race_->points(0) ? cars[0].color : cars[1].color));
        int lap = std::min(cfg.laps, std::max(1, cars[race_->leader()].lapsDone + 1));
        std::snprintf(buf, sizeof buf, "LAP %d/%d", lap, cfg.laps);
        gfx_.text(VIEW_W / 2, 24, race_->suddenDeath() ? "SUDDEN DEATH" : buf, WHITE, 1, 1);
        if (race_->phase() == Phase::RoundPause) {
            int w2 = race_->matchWinner() >= 0 ? race_->matchWinner() : race_->leader();
            std::snprintf(buf, sizeof buf, "POINT TO %s", driverName(cars[w2].color));
            gfx_.text(VIEW_W / 2, 110, buf, GOLD, 3, 1);
        }
        return;
    }

    const Car& me = cars[0];
    int lap = std::min(cfg.laps, std::max(1, me.lapsDone + 1));
    gfx_.fill(4, 4, 96, 30, SHADE);
    std::snprintf(buf, sizeof buf, "LAP %d/%d", lap, cfg.laps);
    gfx_.text(10, 8, buf, WHITE);
    gfx_.text(10, 20, formatTime(race_->clock()).c_str(), GOLD);

    if (cfg.mode == RaceMode::TimeTrial) {
        if (bestLaps_[trackIndex_] > 0 || me.bestLap > 0) {
            float best = bestLaps_[trackIndex_];
            if (me.bestLap > 0 && (best <= 0 || me.bestLap < best)) best = me.bestLap;
            std::string s = "BEST " + formatTime(best);
            gfx_.fill(VIEW_W - 100, 4, 96, 16, SHADE);
            gfx_.text(VIEW_W - 8, 8, s.c_str(), GOLD, 1, 2);
        }
        return;
    }

    auto order = race_->standings();
    int myPlace = 1;
    for (size_t i = 0; i < order.size(); ++i) if (order[i] == 0) myPlace = (int)i + 1;
    gfx_.fill(VIEW_W - 76, 4, 72, 30, SHADE);
    gfx_.text(VIEW_W - 8, 8, placeName(myPlace), myPlace <= 2 ? GREEN : RED, 1, 2);
    for (size_t i = 0; i < order.size(); ++i)
        gfx_.fill(VIEW_W - 70 + (int)i * 15, 22, 12, 8, driverColor(cars[order[i]].color));
}

void Game::drawCountdown() {
    float each = Race::COUNTDOWN_TIME / 4.0f;
    int lit = 4 - (int)std::ceil(race_->countdown() / each);
    gfx_.fill(VIEW_W / 2 - 46, 50, 92, 32, SHADE);
    for (int i = 0; i < 3; ++i) {
        SDL_Color c = i < lit ? RED : SDL_Color{70, 30, 30, 255};
        gfx_.fill(VIEW_W / 2 - 38 + i * 28, 56, 20, 20, c);
    }
}

void Game::drawPanel(const char* heading, SDL_Color c) {
    gfx_.clear(BG);
    gfx_.text(VIEW_W / 2, 30, heading, c, 3, 1);
}

void Game::drawResult() {
    char buf[64];
    const char* prompt = "PRESS ENTER";
    switch (screen_) {
    case Screen::Result: {
        drawPanel(lastPlace_ <= 2 ? "QUALIFIED!" : "NOT THIS TIME", lastPlace_ <= 2 ? GREEN : RED);
        const auto& cars = race_->cars();
        for (size_t i = 0; i < lastOrder_.size(); ++i) {
            const Car& c = cars[lastOrder_[i]];
            int y = 84 + (int)i * 22;
            gfx_.text(150, y, placeName((int)i + 1), c.human ? GOLD : WHITE);
            gfx_.drawCar(race_->track().def().vehicle, c.color, 200, y + 3, 0.0f);
            gfx_.text(226, y, driverName(c.color), c.human ? GOLD : WHITE);
        }
        std::snprintf(buf, sizeof buf, "LIVES %d", lives_);
        gfx_.text(VIEW_W / 2, 188, buf, WHITE, 2, 1);
        if (extraLifeAwarded_) gfx_.text(VIEW_W / 2, 212, "THREE WINS IN A ROW - EXTRA LIFE!", GOLD, 1, 1);
        break;
    }
    case Screen::H2HResult: {
        int w = race_->matchWinner();
        const char* headline = w < 0 ? "NOBODY WINS!"
            : race_->config().humans == 2 ? (w == 0 ? "PLAYER 1 WINS!" : "PLAYER 2 WINS!")
            : (w == 0 ? "YOU WIN!" : "CPU WINS!");
        drawPanel(headline, GOLD);
        std::snprintf(buf, sizeof buf, "%d - %d", race_->points(0), race_->points(1));
        gfx_.text(VIEW_W / 2, 110, buf, WHITE, 4, 1);
        break;
    }
    case Screen::TrialResult: {
        const Car& c = race_->cars()[0];
        drawPanel("TIME TRIAL", GOLD);
        gfx_.text(VIEW_W / 2, 90, ("TOTAL " + formatTime(c.finishTime)).c_str(), WHITE, 2, 1);
        gfx_.text(VIEW_W / 2, 120, ("BEST LAP " + formatTime(c.bestLap)).c_str(), WHITE, 2, 1);
        gfx_.text(VIEW_W / 2, 150, ("RECORD " + formatTime(bestLaps_[trackIndex_])).c_str(), GOLD, 2, 1);
        break;
    }
    case Screen::GameOver:
        drawPanel("GAME OVER", RED);
        gfx_.text(VIEW_W / 2, 110, allTracks()[trackIndex_].name, WHITE, 2, 1);
        break;
    case Screen::Champion:
        drawPanel("CHAMPION!", GOLD);
        gfx_.drawCar(VehicleKind::Coupe, playerColor_, VIEW_W / 2, 124, frame_ * 0.06f, 2.0f);
        gfx_.text(VIEW_W / 2, 170, driverName(playerColor_), WHITE, 3, 1);
        break;
    default: break;
    }
    gfx_.text(VIEW_W / 2, VIEW_H - 18, prompt, GREY, 1, 1);
}
