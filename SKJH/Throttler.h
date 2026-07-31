#pragma once
/*
 * Throttler.h — 轻量级精确节流器 (i3 4代优化)
 *
 * 参考 PUBG_DMA 项目，用 sleep_until 替代 Sleep(N)
 * Windows 默认计时器粒度 15.6ms，Sleep(8) 实际睡眠 15-16ms
 * 配合 timeBeginPeriod(1) 可精确到 1-2ms
 */
#include <chrono>
#include <optional>
#include <thread>

class Throttler {
public:
    // Keep a small floor for zero/negative intervals.  A zero interval used
    // to turn a fast DMA pass into a busy loop and starve the other readers.
    // The floor is a worker scheduling guard, not an overlay frame cap.
    void sleepUntilNext(std::chrono::microseconds interval) {
        using clock = std::chrono::steady_clock;
        constexpr auto kMinimumInterval = std::chrono::milliseconds(1);
        if (interval < kMinimumInterval)
            interval = kMinimumInterval;
        if (lastTime_.has_value()) {
            auto target = lastTime_.value() + interval;
            auto now = clock::now();
            if (now < target)
                std::this_thread::sleep_until(target);
        }
        lastTime_ = clock::now();
    }

    void reset() { lastTime_.reset(); }

private:
    std::optional<std::chrono::steady_clock::time_point> lastTime_;
};
