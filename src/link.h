#pragma once
#include "race.h"
#include <deque>
#include <random>
#include <vector>

// Connects race modules that run in one process. Each module's news reaches the others by a direct
// call, after an optional delay. Car states can also be dropped the way a network drops packets;
// the rest of the news always arrives, as it does over a reliable channel.
class LocalLink {
public:
    explicit LocalLink(int delayTicks = 0, float stateLoss = 0.0f, uint32_t seed = 1)
        : delay_(delayTicks), loss_(stateLoss), rng_(seed) {}

    void add(Race& race) { races_.push_back(&race); }

    // Call once every module has stepped: takes each one's news and delivers whatever is due.
    void exchange();

    int sent() const { return sent_; }
    int dropped() const { return dropped_; }

private:
    struct Parcel {
        int sentAt;
        Race* from;
        RaceNews news;
    };
    std::vector<Race*> races_;
    std::deque<Parcel> queue_;
    int delay_;
    float loss_;
    std::mt19937 rng_;
    int loop_ = 0;
    int sent_ = 0, dropped_ = 0;
};
