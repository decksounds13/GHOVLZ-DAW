#pragma once

#include <atomic>

namespace ghovlz {

// Debug real-time guard (FND-05). The audio callback raises the reader count.
// operator new in the test binary and RtMutex count a hit when that count is
// non-zero. Release builds leave the counters in place so the same binary can
// be checked; they do not abort unless GHOVLZ_RT_ABORT is set.
std::atomic<int>& rtAudioReaders();
std::atomic<int>& rtGuardHits();

inline thread_local int rtDepth = 0;
inline void rtEnter() {
    ++rtDepth;
    rtAudioReaders().fetch_add(1, std::memory_order_acq_rel);
}
inline void rtLeave() {
    --rtDepth;
    rtAudioReaders().fetch_sub(1, std::memory_order_release);
}
inline bool rtGuardActive() { return rtDepth > 0; }

// Tests that deliberately touch a UI-only object from a fake audio scope
// raise this so GHOVLZ_RT_ABORT still counts the hit and does not abort.
inline thread_local int rtGuardSilence = 0;

void rtGuardHit(const char* why);

struct RtGuardSilence {
    RtGuardSilence() { ++rtGuardSilence; }
    ~RtGuardSilence() { --rtGuardSilence; }
};

struct RtScope {
    RtScope() { rtEnter(); }
    ~RtScope() { rtLeave(); }
};

} // namespace ghovlz
