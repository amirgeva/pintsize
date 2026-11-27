#include "link.h"

void LocalLink::exchange() {
    for (Race* r : races_) {
        queue_.push_back({loop_, r, r->news()});
        ++sent_;
    }

    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    while (!queue_.empty() && queue_.front().sentAt + delay_ <= loop_) {
        Parcel& p = queue_.front();
        if (loss_ > 0 && roll(rng_) < loss_) {
            p.news.cars.clear();
            ++dropped_;
        }
        // News made after step k and applied before step k + 1 + delay is that many ticks old by
        // the time the receiver's next step shows it.
        const float age = (loop_ + 1 - p.sentAt) * DT;
        for (Race* r : races_)
            if (r != p.from) r->apply(p.news, age);
        queue_.pop_front();
    }
    ++loop_;
}
