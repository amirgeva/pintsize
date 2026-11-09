#include "audio.h"
#include <cmath>

static constexpr int RATE = 44100;
static constexpr float TWO_PI = 6.2831853f;

Audio::~Audio() {
    if (dev_) SDL_CloseAudioDevice(dev_);
}

bool Audio::init() {
    SDL_AudioSpec want{}, have{};
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = &Audio::callback;
    want.userdata = this;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev_) return false;
    SDL_PauseAudioDevice(dev_, 0);
    return true;
}

void Audio::callback(void* self, Uint8* stream, int bytes) {
    static_cast<Audio*>(self)->mix(reinterpret_cast<Sint16*>(stream), bytes / 2);
}

float Audio::nextNoise() {
    noiseState_ ^= noiseState_ << 13;
    noiseState_ ^= noiseState_ >> 17;
    noiseState_ ^= noiseState_ << 5;
    return (float)(noiseState_ & 0xffff) / 32768.0f - 1.0f;
}

void Audio::start(int wave, float f0, float f1, float dur, float vol) {
    for (Voice& v : voices_) {
        if (v.active) continue;
        v = Voice{};
        v.active = true;
        v.wave = wave;
        v.freq = f0;
        v.freqEnd = f1;
        v.dur = dur;
        v.vol = vol;
        return;
    }
}

void Audio::play(Sfx s, float strength) {
    if (!dev_) return;
    float k = strength < 0.2f ? 0.2f : (strength > 1.5f ? 1.5f : strength);
    SDL_LockAudioDevice(dev_);
    switch (s) {
    case Sfx::Bump:    start(1, 900, 200, 0.09f, 0.28f * k); start(0, 140, 70, 0.08f, 0.16f * k); break;
    case Sfx::Wall:    start(1, 500, 120, 0.10f, 0.26f * k); break;
    case Sfx::Fall:    start(2, 900, 90, 0.75f, 0.32f); break;
    case Sfx::Splash:  start(1, 2400, 300, 0.5f, 0.3f); break;
    case Sfx::Respawn: start(0, 330, 660, 0.14f, 0.14f); break;
    case Sfx::Lap:     start(0, 660, 660, 0.09f, 0.18f); start(0, 880, 880, 0.18f, 0.12f); break;
    case Sfx::Finish:  start(0, 523, 1046, 0.5f, 0.2f); start(2, 262, 523, 0.5f, 0.2f); break;
    case Sfx::Beep:    start(0, 440, 440, 0.16f, 0.22f); break;
    case Sfx::Go:      start(0, 880, 880, 0.42f, 0.24f); break;
    case Sfx::Shot:    start(1, 1800, 150, 0.2f, 0.34f); break;
    case Sfx::Hit:     start(1, 700, 60, 0.4f, 0.4f); break;
    case Sfx::Jump:    start(2, 220, 620, 0.22f, 0.22f); break;
    case Sfx::Land:    start(1, 300, 80, 0.12f, 0.24f * k); break;
    case Sfx::Point:   start(0, 523, 784, 0.3f, 0.22f); start(2, 392, 523, 0.3f, 0.2f); break;
    case Sfx::Move:    start(0, 520, 520, 0.04f, 0.12f); break;
    case Sfx::Select:  start(0, 520, 1040, 0.12f, 0.16f); break;
    }
    SDL_UnlockAudioDevice(dev_);
}

void Audio::setEngine(int ch, float speed01, bool on) {
    engineSpeed_[ch] = speed01;
    engineOn_[ch] = on;
}

void Audio::setSkid(float amount) { skid_ = amount; }

static float midiHz(int n) { return 440.0f * std::pow(2.0f, (float)(n - 69) / 12.0f); }

void Audio::mix(Sint16* out, int count) {
    // A short original chord loop for the menus.
    static const int roots[4] = {45, 41, 48, 43};
    static const int arp[8] = {0, 7, 12, 7, 16, 12, 7, 12};
    const int stepLen = RATE / 8;
    const float dt = 1.0f / RATE;
    const bool musicOn = music_;
    const bool muted = muted_;

    for (int i = 0; i < count; ++i) {
        float s = 0;

        for (int e = 0; e < 2; ++e) {
            float target = engineOn_[e] ? 0.055f : 0.0f;
            engineVol_[e] += (target - engineVol_[e]) * 0.002f;
            float sp = engineSpeed_[e];
            float f = (e == 0 ? 62.0f : 50.0f) + sp * (e == 0 ? 210.0f : 180.0f);
            enginePhase_[e] += f * dt;
            if (enginePhase_[e] >= 1) enginePhase_[e] -= 1;
            float saw = 2 * enginePhase_[e] - 1;
            float pulse = enginePhase_[e] < 0.3f ? 1.0f : -1.0f;
            s += (saw * 0.6f + pulse * 0.4f) * engineVol_[e];
        }

        float skidTarget = skid_;
        skidVol_ += (skidTarget - skidVol_) * 0.004f;
        if (skidVol_ > 0.001f) s += nextNoise() * skidVol_ * 0.07f;

        for (Voice& v : voices_) {
            if (!v.active) continue;
            float p = v.t / v.dur;
            float f = v.freq + (v.freqEnd - v.freq) * p;
            float env = 1.0f - p;
            float w = 0;
            if (v.wave == 1) {
                v.noiseCount += f * dt;
                if (v.noiseCount >= 1) { v.noiseCount -= 1; v.noiseHold = nextNoise(); }
                w = v.noiseHold;
            } else {
                v.phase += f * dt;
                if (v.phase >= 1) v.phase -= 1;
                w = v.wave == 0 ? (v.phase < 0.5f ? 1.0f : -1.0f) : (4 * std::fabs(v.phase - 0.5f) - 1);
            }
            s += w * env * v.vol;
            v.t += dt;
            if (v.t >= v.dur) v.active = false;
        }

        if (musicOn) {
            if (musicCounter_-- <= 0) {
                musicCounter_ = stepLen;
                int bar = (musicStep_ / 16) % 4;
                int root = roots[bar];
                int beat = musicStep_ % 16;
                bassFreq_ = midiHz(root - 12 + ((beat % 8) == 6 ? 7 : 0));
                leadFreq_ = midiHz(root + 12 + arp[beat % 8]);
                leadEnv_ = 1;
                ++musicStep_;
            }
            bassPhase_ += bassFreq_ * dt; if (bassPhase_ >= 1) bassPhase_ -= 1;
            leadPhase_ += leadFreq_ * dt; if (leadPhase_ >= 1) leadPhase_ -= 1;
            leadEnv_ *= 0.99985f;
            s += (4 * std::fabs(bassPhase_ - 0.5f) - 1) * 0.14f;
            s += (leadPhase_ < 0.25f ? 1.0f : -1.0f) * 0.06f * leadEnv_;
        }

        if (muted) s = 0;
        if (s > 1) s = 1;
        if (s < -1) s = -1;
        out[i] = (Sint16)(s * 30000);
    }
}
