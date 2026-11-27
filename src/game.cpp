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
constexpr int MAX_CATCH_UP = 8;   // steps a LAN race may take in one frame to catch up with the shared clock

// F9 cycles through these simulated connections during LAN play.
const NetConditions NET_PRESETS[] = {{0, 0, 0}, {20, 5, 1}, {50, 15, 3}, {100, 30, 5}, {200, 60, 10}};
constexpr int NUM_NET_PRESETS = (int)(sizeof NET_PRESETS / sizeof NET_PRESETS[0]);

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
    bool racing = s == Screen::Racing || s == Screen::Paused || s == Screen::LanPaused;
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
    case 4: go(Screen::LanMenu); break;
    case 5: go(Screen::Controls); break;
    default: quit_ = true; break;
    }
}

void Game::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_KEYDOWN && !e.key.repeat) onKey(e.key.keysym.sym);
}

void Game::onKey(SDL_Keycode key) {
    if (key == SDLK_m) { audio_->toggleMute(); return; }
    if (key == SDLK_F9) {   // cycle the simulated connection
        netPreset_ = (netPreset_ + 1) % NUM_NET_PRESETS;
        setNetConditions(NET_PRESETS[netPreset_]);
        return;
    }
    if (key == SDLK_F10) { netInfo_ = !netInfo_; return; }
    const bool confirm = key == SDLK_RETURN || key == SDLK_SPACE || key == SDLK_KP_ENTER;
    const int tracks = (int)allTracks().size();

    switch (screen_) {
    case Screen::Title:
        if (confirm) { audio_->play(Sfx::Select); go(Screen::Main); }
        else if (key == SDLK_ESCAPE) quit_ = true;
        break;
    case Screen::Main:
        if (key == SDLK_UP) menuMove(-1, 7);
        else if (key == SDLK_DOWN) menuMove(1, 7);
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
        // A LAN race cannot stop for one player, so Esc only offers a way out.
        if (key == SDLK_ESCAPE) go(flow_ == Flow::Lan ? Screen::LanPaused : Screen::Paused);
        break;
    case Screen::LanMenu:
        if (key == SDLK_UP) menuMove(-1, 3);
        else if (key == SDLK_DOWN) menuMove(1, 3);
        else if (key == SDLK_ESCAPE) go(Screen::Main);
        else if (confirm) {
            audio_->play(Sfx::Select);
            if (menu_ == 0) startLanHost();
            else if (menu_ == 1) { search_ = std::make_unique<NetSearch>(); go(Screen::LanSearch); }
            else go(Screen::Main);
        }
        break;
    case Screen::LanSearch: {
        const int found = search_ ? (int)search_->hosts().size() : 0;
        if (key == SDLK_UP && found) menuMove(-1, found);
        else if (key == SDLK_DOWN && found) menuMove(1, found);
        else if (key == SDLK_ESCAPE) { search_.reset(); go(Screen::LanMenu); }
        else if (confirm && menu_ < found) {
            audio_->play(Sfx::Select);
            const NetFoundHost h = search_->hosts()[menu_];
            search_.reset();
            lanJoin(h.address, h.port);
        }
        break;
    }
    case Screen::LanLobby:
        lanLobbyKey(key, confirm);
        break;
    case Screen::LanPaused:
        if (key == SDLK_UP) menuMove(-1, 2);
        else if (key == SDLK_DOWN) menuMove(1, 2);
        else if (key == SDLK_ESCAPE) go(Screen::Racing);
        else if (confirm) {
            audio_->play(Sfx::Select);
            if (menu_ == 0) go(Screen::Racing);
            else lanLeave("");
        }
        break;
    case Screen::LanResult:
        if (confirm) {
            audio_->play(Sfx::Select);
            if (net_ && net_->isHost()) net_->endRace();
            go(Screen::LanLobby);
        } else if (key == SDLK_ESCAPE) {
            lanLeave("");
        }
        break;
    case Screen::LanMessage:
        if (confirm || key == SDLK_ESCAPE) { audio_->play(Sfx::Select); go(Screen::Main); }
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
    int local = 0;   // players at this keyboard; in a LAN race the others are on other machines
    if (race_) for (const Car& c : race_->cars()) local += c.humanIndex >= 0 ? 1 : 0;
    const bool two = local == 2;
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
        if (c.humanIndex < 0) continue;   // engines are for the players at this machine
        bool on = c.state == CarState::Drive;
        audio_->setEngine(c.humanIndex, clampf(len(c.vel) / c.spec->topSpeed, 0, 1.2f), on);
        if (on && c.z <= 0 && c.surface != Surface::Rough && c.skid > 62.0f) skid = 1;
    }
    audio_->setSkid(skid);
}

void Game::tick() {
    ++frame_;
    if (net_) lanTick();
    if (search_) search_->poll();
    if (flow_ == Flow::Lan) return;   // LAN races step in frame(), in time with the host's clock
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
    case Flow::Lan:
        go(Screen::LanResult);
        break;
    }
}

// ---------------------------------------------------------------- LAN play

void Game::setNetConditions(const NetConditions& c) {
    netSim_ = c;
    if (net_) net_->setConditions(c);
}

void Game::startLanHost() {
    search_.reset();
    net_ = std::make_unique<NetSession>();
    net_->setConditions(netSim_);
    NetPlayer me;
    me.name = netLocalName();
    me.color = playerColor_;
    std::string error;
    if (!net_->host(me, NET_PORT, error)) { lanLeave("CANNOT HOST: " + error); return; }
    flow_ = Flow::Lan;
    net_->setTrack(trackIndex_);
    go(Screen::LanLobby);
}

void Game::startLanJoin(const std::string& address) {
    std::string host;
    uint16_t port = NET_PORT;
    if (!netParseAddress(address, host, port)) { lanLeave("BAD ADDRESS " + address); return; }
    lanJoin(host, port);
}

void Game::lanJoin(const std::string& host, uint16_t port) {
    net_ = std::make_unique<NetSession>();
    net_->setConditions(netSim_);
    NetPlayer me;
    me.name = netLocalName();
    me.color = playerColor_;
    std::string error;
    if (!net_->join(me, host, port, error)) { lanLeave(error); return; }
    flow_ = Flow::Lan;
    lanAddress_ = host;
    go(Screen::LanLobby);
}

void Game::lanLeave(const std::string& message) {
    net_.reset();
    search_.reset();
    race_.reset();
    lanLive_ = false;
    flow_ = Flow::Tournament;
    if (message.empty()) { go(Screen::Main); return; }
    lanMessage_ = message;
    go(Screen::LanMessage);
}

// Runs every tick in any LAN screen: keeps the session alive and follows the host.
void Game::lanTick() {
    net_->poll(lanRacing() ? race_.get() : nullptr);
    if (net_->closed()) { lanLeave(net_->reason()); return; }
    NetStart s;
    if (net_->takeStart(s)) { lanBeginRace(s); return; }
    // The host went back to the lobby before this race finished here: show what there is.
    if (lanRacing() && !net_->inRace()) finishRace();

    // Rates for the F10 overlay, once a second.
    if (++netStatsFrame_ >= 60) {
        netStatsFrame_ = 0;
        const NetStats st = net_->stats();
        netRateIn_ = (float)(st.statesIn - netLast_.statesIn);
        netLostRate_ = (float)(st.statesLost - netLast_.statesLost);
        const int ages = st.ageCount - netLast_.ageCount;
        netAge_ = ages > 0 ? (float)((st.ageSumMs - netLast_.ageSumMs) / ages) : 0.0f;
        netLast_ = st;
    }
}

void Game::lanBeginRace(const NetStart& s) {
    trackIndex_ = s.track;
    loadTrack(s.track);   // can take a moment; the host waits until every machine says it is ready
    if (track_->count() != s.samples) { lanLeave("THIS COURSE DIFFERS FROM THE HOST'S - DIFFERENT VERSIONS?"); return; }
    race_ = std::make_unique<Race>(*track_, s.cfg);
    gfx_.resetFx();
    lanLive_ = false;
    net_->ready();
    go(Screen::Racing);
}

void Game::frame() {
    if (!net_ || !lanRacing()) return;
    net_->poll(race_.get());
    if (net_->closed()) { lanLeave(net_->reason()); return; }
    if (!lanLive_ && net_->started()) lanLive_ = true;
    if (!lanLive_) return;

    Input in[2] = {readPlayer(0), readPlayer(1)};
    if (screen_ == Screen::LanPaused) in[0] = in[1] = Input{};   // the menu is up: let go of the controls
    for (int i = 0; i < MAX_CATCH_UP && race_->ticks() < net_->ticksDue(); ++i) {
        race_->step(in);
        net_->send(*race_);
        gfx_.updateFx(*race_);
        playEvents();
    }
    if (race_->phase() == Phase::Finished && race_->sinceFinished() > 2.5f) finishRace();
}

void Game::lanLobbyKey(SDL_Keycode key, bool confirm) {
    if (key == SDLK_ESCAPE) { audio_->play(Sfx::Select); lanLeave(""); return; }
    if (!net_ || !net_->isHost() || net_->inRace()) return;   // only the host changes the race
    const int tracks = (int)allTracks().size();
    const NetLobby& lobby = net_->lobby();
    if (key == SDLK_LEFT || key == SDLK_RIGHT) {
        trackIndex_ = (lobby.track + (key == SDLK_LEFT ? tracks - 1 : 1)) % tracks;
        net_->setTrack(trackIndex_);
        audio_->play(Sfx::Move);
    } else if (key == SDLK_UP || key == SDLK_DOWN) {
        net_->setCpuCars(lobby.cpuCars + (key == SDLK_UP ? 1 : -1));
        audio_->play(Sfx::Move);
    } else if (confirm) {
        audio_->play(Sfx::Select);
        loadTrack(lobby.track);
        net_->startRace(SDL_GetTicks() | 1u, track_->count());
    }
}

// The player's name for a machine's own car, or CPU for the host's computer cars and for cars
// whose machine has left.
std::string Game::lanCarName(int car) const {
    if (!race_ || !net_) return "CPU";
    const Car& c = race_->cars()[car];
    for (const Car& o : race_->cars()) {
        if (o.driver != c.driver) continue;
        if (o.id != car) return "CPU";   // not the machine's first car
        break;
    }
    for (const NetPlayer& p : net_->lobby().players)
        if (p.slot == c.driver) return p.name;
    return "CPU";
}

// ---------------------------------------------------------------- drawing

void Game::draw() {
    switch (screen_) {
    case Screen::Title: drawTitle(); break;
    case Screen::Main:
        drawMenu("PINT-SIZE PRIX", {"TOURNAMENT", "HEAD TO HEAD VS CPU", "HEAD TO HEAD 2 PLAYERS", "TIME TRIAL", "LAN RACE", "CONTROLS", "QUIT"});
        break;
    case Screen::LanMenu:
        drawMenu("LAN RACE", {"HOST A RACE", "JOIN A RACE", "BACK"});
        break;
    case Screen::LanSearch: drawLanSearch(); break;
    case Screen::LanLobby: drawLanLobby(); break;
    case Screen::LanResult: drawLanResult(); break;
    case Screen::LanMessage:
        drawPanel("LAN RACE", RED);
        gfx_.text(VIEW_W / 2, 110, lanMessage_.c_str(), WHITE, 1, 1);
        gfx_.text(VIEW_W / 2, VIEW_H - 18, "PRESS ENTER", GREY, 1, 1);
        break;
    case Screen::LanPaused:
        gfx_.drawRace(*race_);
        drawHud();
        gfx_.fill(0, 0, VIEW_W, VIEW_H, SHADE);
        gfx_.text(VIEW_W / 2, 70, "THE RACE GOES ON", GOLD, 2, 1);
        {
            const char* items[2] = {"BACK TO THE RACE", "LEAVE THE RACE"};
            for (int i = 0; i < 2; ++i) gfx_.text(VIEW_W / 2, 120 + i * 18, items[i], i == menu_ ? GOLD : WHITE, 1, 1);
        }
        break;
    case Screen::Controls: drawControls(); break;
    case Screen::Driver: drawDriver(); break;
    case Screen::TrackSelect: drawTrackSelect(); break;
    case Screen::Intro: drawIntro(); break;
    case Screen::Racing:
        gfx_.drawRace(*race_);
        drawHud();
        if (race_->phase() == Phase::Countdown) drawCountdown();
        if (flow_ == Flow::Lan && !lanLive_) gfx_.text(VIEW_W / 2, 100, "WAITING FOR EVERYONE", WHITE, 2, 1);
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
    if (lanRacing()) drawNetInfo();
    if ((screen_ == Screen::LanLobby || screen_ == Screen::LanSearch) && netSim_.active()) {
        const std::string sim = "SIMULATED CONNECTION " + netDescribe(netSim_) + " - F9 TO CHANGE";
        gfx_.text(VIEW_W / 2, 240, sim.c_str(), RED, 1, 1);
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

    const Car& me = cars[race_->focus()];
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
    for (size_t i = 0; i < order.size(); ++i) if (order[i] == me.id) myPlace = (int)i + 1;
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

void Game::drawLanSearch() {
    gfx_.clear(BG);
    gfx_.text(VIEW_W / 2, 24, "JOIN A RACE", GOLD, 3, 1);
    const std::vector<NetFoundHost> none;
    const auto& hosts = search_ ? search_->hosts() : none;
    if (search_ && !search_->ok()) {
        gfx_.text(VIEW_W / 2, 110, "CANNOT SEARCH THE NETWORK", RED, 1, 1);
    } else if (hosts.empty()) {
        const char* dots[] = {"", ".", "..", "..."};
        std::string s = std::string("LOOKING FOR RACES ON YOUR NETWORK") + dots[(frame_ / 20) % 4];
        gfx_.text(VIEW_W / 2 - 100, 100, s.c_str(), WHITE);
        gfx_.text(VIEW_W / 2, 140, "NOTHING FOUND? START THE GAME WITH", GREY, 1, 1);
        gfx_.text(VIEW_W / 2, 152, "--JOIN <ADDRESS> TO JOIN A HOST DIRECTLY", GREY, 1, 1);
    }
    for (int i = 0; i < (int)hosts.size() && i < 6; ++i) {
        const NetFoundHost& h = hosts[i];
        const bool sel = i == menu_;
        const int y = 70 + i * 26;
        if (sel) gfx_.fill(60, y - 5, VIEW_W - 120, 24, SDL_Color{60, 70, 120, 255});
        char line[96];
        std::snprintf(line, sizeof line, "%s - %s - %d/%d%s", h.name.c_str(), allTracks()[h.track].name, h.players, MAX_CARS,
                      h.racing ? " - RACING" : "");
        gfx_.text(VIEW_W / 2, y, line, sel ? GOLD : WHITE, 1, 1);
        std::snprintf(line, sizeof line, "%s:%u", h.address.c_str(), h.port);
        gfx_.text(VIEW_W / 2, y + 10, line, GREY, 1, 1);
    }
    gfx_.text(VIEW_W / 2, VIEW_H - 16, "UP/DOWN  ENTER: JOIN  ESC: BACK", GREY, 1, 1);
}

void Game::drawLanLobby() {
    gfx_.clear(BG);
    gfx_.text(VIEW_W / 2, 10, "LAN RACE", GOLD, 2, 1);
    if (!net_) return;
    const NetLobby& lobby = net_->lobby();
    const bool host = net_->isHost();

    const TrackDef& def = allTracks()[lobby.track];
    gfx_.drawTrackPreview(def, SDL_Rect{20, 36, 200, 118}, GREEN);
    gfx_.text(120, 162, def.name, WHITE, 1, 1);
    std::string sub = std::string(themeInfo(def.theme).name) + " - " + vehicleSpec(def.vehicle).name;
    gfx_.text(120, 174, sub.c_str(), GREY, 1, 1);
    if (host) gfx_.text(120, 188, "< LEFT/RIGHT >", GREY, 1, 1);

    gfx_.text(250, 40, "PLAYERS", GREY);
    for (int i = 0; i < (int)lobby.players.size(); ++i) {
        const NetPlayer& p = lobby.players[i];
        const int y = 56 + i * 18;
        gfx_.drawCar(def.vehicle, p.color % NUM_DRIVERS, 260, y + 3, 0.0f);
        const bool me = p.slot == net_->self();
        gfx_.text(276, y, p.name.c_str(), me ? GOLD : WHITE);
        const char* tag = p.slot == 0 ? (p.bot ? "HOST BOT" : "HOST") : (p.bot ? "BOT" : "");
        if (me) tag = p.slot == 0 ? "YOU, HOST" : "YOU";
        gfx_.text(472, y, tag, GREY, 1, 2);
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "CPU CARS %d", lobby.cpuCars);
    gfx_.text(250, 56 + 4 * 18 + 6, buf, WHITE);
    if (host) gfx_.text(250, 56 + 4 * 18 + 18, "UP/DOWN TO CHANGE", GREY);

    const char* status;
    std::string line;
    if (!net_->connected()) {
        line = "CONNECTING TO " + lanAddress_ + "...";
        status = line.c_str();
    } else if (net_->inRace()) {
        status = "STARTING...";
    } else if (lobby.racing) {
        status = "A RACE IS ON - YOU JOIN THE NEXT ONE";
    } else if (host) {
        line = lobby.players.size() < 2 ? "WAITING FOR PLAYERS TO JOIN" : "PRESS ENTER TO START";
        status = line.c_str();
    } else {
        line = "WAITING FOR " + net_->hostName() + " TO START";
        status = line.c_str();
    }
    gfx_.text(VIEW_W / 2, 214, status, WHITE, 1, 1);
    if (host) {
        std::snprintf(buf, sizeof buf, "HOSTING AS %s ON PORT %u%s", net_->hostName().c_str(), NET_PORT,
                      net_->visible() ? "" : " - NOT FOUND BY SEARCHES");
        gfx_.text(VIEW_W / 2, 228, buf, GREY, 1, 1);
    }
    gfx_.text(VIEW_W / 2, VIEW_H - 16, host ? "ENTER: START   ESC: CLOSE THE RACE" : "ESC: LEAVE", GREY, 1, 1);
}

void Game::drawLanResult() {
    drawPanel("RACE RESULT", GOLD);
    if (!race_) return;
    const auto& cars = race_->cars();
    for (size_t i = 0; i < lastOrder_.size(); ++i) {
        const Car& c = cars[lastOrder_[i]];
        const bool mine = !c.remote && c.humanIndex >= 0;
        const SDL_Color col = mine ? GOLD : WHITE;
        const int y = 80 + (int)i * 22;
        gfx_.text(110, y, placeName((int)i + 1), col);
        gfx_.drawCar(race_->track().def().vehicle, c.color, 152, y + 3, 0.0f);
        gfx_.text(170, y, driverName(c.color), col);
        gfx_.text(220, y, lanCarName(c.id).c_str(), mine ? GOLD : GREY);
        if (c.finished && c.finishTime > 0) gfx_.text(370, y, formatTime(c.finishTime).c_str(), col, 1, 2);
    }
    gfx_.text(VIEW_W / 2, VIEW_H - 18, "ENTER: BACK TO THE LOBBY   ESC: LEAVE", GREY, 1, 1);
}

// The simulated connection, and with F10 the network statistics.
void Game::drawNetInfo() {
    std::vector<std::string> lines;
    if (netSim_.active()) lines.push_back("SIMULATED " + netDescribe(netSim_) + " (F9)");
    if (netInfo_) {
        if (!netSim_.active()) lines.push_back("NO SIMULATED LAG (F9)");
        if (net_) {
            const NetStats st = net_->stats();
            char buf[96];
            std::snprintf(buf, sizeof buf, "ROUND TRIP %.1fMS  CLOCK WITHIN %.1fMS", st.rttMs, st.clockMs);
            lines.push_back(buf);
            std::snprintf(buf, sizeof buf, "STATES %.0f/S  LOST %.0f/S  AGE %.0fMS", netRateIn_, netLostRate_, netAge_);
            lines.push_back(buf);
        }
    }
    if (lines.empty()) return;
    const int h = (int)lines.size() * 10 + 4;
    const int y0 = VIEW_H - 4 - h;
    gfx_.fill(4, y0, 230, h, SHADE);
    for (size_t i = 0; i < lines.size(); ++i) gfx_.text(8, y0 + 3 + (int)i * 10, lines[i].c_str(), i == 0 && netSim_.active() ? RED : WHITE);
}
