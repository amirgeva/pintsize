#pragma once
#include <SDL.h>
#include <atomic>

enum class Sfx { Bump, Wall, Fall, Splash, Respawn, Lap, Finish, Beep, Go, Shot, Hit, Jump, Land, Point, Move, Select };

// All sound is synthesised at runtime; there are no audio assets.
class Audio {
public:
    ~Audio();
    bool init();
    void play(Sfx s, float strength = 1.0f);
    void setEngine(int channel, float speed01, bool on);
    void setSkid(float amount);
    void setMusic(bool on) { music_ = on; }
    void toggleMute() { muted_ = !muted_; }

private:
    struct Voice {
        bool active = false;
        int wave = 0;   // 0 square, 1 noise, 2 triangle
        float phase = 0, freq = 0, freqEnd = 0, t = 0, dur = 0, vol = 0;
        float noiseHold = 0, noiseCount = 0;
    };
    static void callback(void* self, Uint8* stream, int bytes);
    void mix(Sint16* out, int count);
    void start(int wave, float f0, float f1, float dur, float vol);
    float nextNoise();

    SDL_AudioDeviceID dev_ = 0;
    static constexpr int MAX_VOICES = 16;
    Voice voices_[MAX_VOICES];
    std::atomic<float> engineSpeed_[2] = {{0.0f}, {0.0f}};
    std::atomic<bool> engineOn_[2] = {{false}, {false}};
    std::atomic<float> skid_{0.0f};
    std::atomic<bool> music_{false};
    std::atomic<bool> muted_{false};
    float enginePhase_[2] = {0, 0};
    float engineVol_[2] = {0, 0};
    float skidVol_ = 0;
    uint32_t noiseState_ = 0x12345;
    int musicStep_ = 0;
    int musicCounter_ = 0;
    float bassPhase_ = 0, leadPhase_ = 0, bassFreq_ = 0, leadFreq_ = 0, leadEnv_ = 0;
};
