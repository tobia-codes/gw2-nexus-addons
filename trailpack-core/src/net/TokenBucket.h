#pragma once
// TokenBucket.h — PURE request budget (PLAN D10). Every outgoing request takes one token;
// tokens refill continuously up to `burst`. Core stays far below the API limit (300 burst,
// 5/s per IP, shared with every other tool on the network): default 2/s, burst 20.
// A 429 / 5xx answer pauses the bucket with an exponential backoff.

#include <cstdint>

namespace Trailpack { namespace Core {

class TokenBucket {
public:
    TokenBucket(double perSecond = 2.0, double burst = 20.0);

    // Take a token at `nowMs` if one is available and no backoff is running.
    bool TryTake(int64_t nowMs);
    // Earliest time a token could be taken (>= nowMs).
    int64_t NextTokenAt(int64_t nowMs) const;

    // Server asked us to slow down (429) or failed (5xx / network): pause, doubling each time
    // (1 s, 2 s, 4 s ... capped at `maxBackoffMs`). Success() resets the doubling.
    void Backoff(int64_t nowMs);
    void Success();

    int64_t PausedUntil() const { return pausedUntil_; }

    static constexpr int64_t kFirstBackoffMs = 1000;
    static constexpr int64_t kMaxBackoffMs   = 5 * 60 * 1000;

private:
    void Refill(int64_t nowMs);

    double  rate_;          // tokens per ms
    double  burst_;
    double  tokens_;
    int64_t lastMs_ = -1;
    int64_t pausedUntil_ = 0;
    int64_t backoffMs_ = 0;
};

}} // namespace Trailpack::Core
