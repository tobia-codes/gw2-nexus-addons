// TokenBucket.cpp — see TokenBucket.h.
#include "net/TokenBucket.h"

namespace Trailpack { namespace Core {

TokenBucket::TokenBucket(double perSecond, double burst)
    : rate_(perSecond / 1000.0), burst_(burst), tokens_(burst) {}

void TokenBucket::Refill(int64_t nowMs)
{
    if (lastMs_ >= 0 && nowMs > lastMs_) {
        tokens_ += static_cast<double>(nowMs - lastMs_) * rate_;
        if (tokens_ > burst_) tokens_ = burst_;
    }
    if (nowMs > lastMs_) lastMs_ = nowMs;
}

bool TokenBucket::TryTake(int64_t nowMs)
{
    Refill(nowMs);
    if (nowMs < pausedUntil_ || tokens_ < 1.0) return false;
    tokens_ -= 1.0;
    return true;
}

int64_t TokenBucket::NextTokenAt(int64_t nowMs) const
{
    int64_t at = nowMs;
    double tokens = tokens_;
    if (lastMs_ >= 0 && nowMs > lastMs_) tokens += static_cast<double>(nowMs - lastMs_) * rate_;
    if (tokens < 1.0) at = nowMs + static_cast<int64_t>((1.0 - tokens) / rate_) + 1;
    return at < pausedUntil_ ? pausedUntil_ : at;
}

void TokenBucket::Backoff(int64_t nowMs)
{
    backoffMs_ = backoffMs_ == 0 ? kFirstBackoffMs : backoffMs_ * 2;
    if (backoffMs_ > kMaxBackoffMs) backoffMs_ = kMaxBackoffMs;
    pausedUntil_ = nowMs + backoffMs_;
}

void TokenBucket::Success()
{
    backoffMs_ = 0;
}

}} // namespace Trailpack::Core
