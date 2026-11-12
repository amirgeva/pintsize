#pragma once
#include "audio.h"
#include "gfx.h"
#include "net.h"
#include <memory>
#include <string>
#include <vector>

class Game {
public:
    Game(SDL_Renderer* r, Audio* audio);

    void handleEvent(const SDL_Event& e);
    void tick();     // one fixed 60 Hz step
    void frame();    // once per drawn frame: steps a LAN race in time with the host's clock
    void draw();     // draws the VIEW_W x VIEW_H frame to the current render target
    bool wantsQuit() const { return quit_; }

    // Used by --shot: an all-computer race with no screen transitions.
    void startDemo(int trackIndex, RaceMode mode);
    void showMainMenu();

    // LAN play, also reachable from the command line.
    void startLanHost();
    void startLanJoin(const std::string& address);   // "host" or "host:port"
    void setNetConditions(const NetConditions& c);

private:
    enum class Screen { Title, Main, Controls, Driver, TrackSelect, Intro, Racing, Paused, Result, H2HResult, TrialResult, GameOver, Champion,
                        LanMenu, LanSearch, LanLobby, LanPaused, LanResult, LanMessage };
    enum class Flow { Tournament, H2HCpu, H2HTwo, Trial, Lan };

    void go(Screen s);
    void onKey(SDL_Keycode key);
    void menuMove(int delta, int count);
    void activateMain();
    void beginFlow(Flow f);
    void loadTrack(int index);
    void startRace();
    void finishRace();
    void playEvents();
    Input readPlayer(int player) const;
    void loadBest();
    void saveBest();

    bool lanRacing() const { return flow_ == Flow::Lan && race_ && (screen_ == Screen::Racing || screen_ == Screen::LanPaused); }
    void lanTick();
    void lanJoin(const std::string& host, uint16_t port);
    void lanBeginRace(const NetStart& s);
    void lanLeave(const std::string& message);
    void lanLobbyKey(SDL_Keycode key, bool confirm);
    std::string lanCarName(int car) const;

    void drawMenu(const char* title, const std::vector<std::string>& items);
    void drawTitle();
    void drawControls();
    void drawDriver();
    void drawTrackSelect();
    void drawIntro();
    void drawHud();
    void drawCountdown();
    void drawResult();
    void drawPanel(const char* heading, SDL_Color c);
    void drawLanSearch();
    void drawLanLobby();
    void drawLanResult();
    void drawNetInfo();

    Gfx gfx_;
    Audio* audio_;
    Screen screen_ = Screen::Title;
    Flow flow_ = Flow::Tournament;
    bool quit_ = false;
    bool demo_ = false;
    int menu_ = 0;
    int frame_ = 0;

    int playerColor_ = 0;
    int trackIndex_ = 0;
    int lives_ = 3;
    int winStreak_ = 0;
    int lastPlace_ = 0;
    bool extraLifeAwarded_ = false;
    std::vector<int> lastOrder_;
    float bestLaps_[16] = {};

    std::unique_ptr<Track> track_;
    int loadedTrack_ = -1;
    std::unique_ptr<Race> race_;

    // LAN play
    std::unique_ptr<NetSession> net_;
    std::unique_ptr<NetSearch> search_;
    std::string lanMessage_;
    std::string lanAddress_;       // the host being joined, for the lobby screen
    bool lanLive_ = false;         // the race has reached its shared start time
    NetConditions netSim_;         // simulated poor connection (F9)
    int netPreset_ = 0;
    bool netInfo_ = false;         // network statistics overlay (F10)
    NetStats netLast_;
    float netRateIn_ = 0, netLostRate_ = 0, netAge_ = 0;
    int netStatsFrame_ = 0;
};
