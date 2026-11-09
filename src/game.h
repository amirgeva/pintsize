#pragma once
#include "audio.h"
#include "gfx.h"
#include <memory>
#include <string>
#include <vector>

class Game {
public:
    Game(SDL_Renderer* r, Audio* audio);

    void handleEvent(const SDL_Event& e);
    void tick();     // one fixed 60 Hz step
    void draw();     // draws the VIEW_W x VIEW_H frame to the current render target
    bool wantsQuit() const { return quit_; }

    // Used by --shot: an all-computer race with no screen transitions.
    void startDemo(int trackIndex, RaceMode mode);
    void showMainMenu();

private:
    enum class Screen { Title, Main, Controls, Driver, TrackSelect, Intro, Racing, Paused, Result, H2HResult, TrialResult, GameOver, Champion };
    enum class Flow { Tournament, H2HCpu, H2HTwo, Trial };

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
};
