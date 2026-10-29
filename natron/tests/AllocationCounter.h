#pragma once

// Counts heap allocations and frees made on the current thread while a ScopedAllocationCounter is alive.
// Used to prove the audio path never touches the heap. (Global operator new is replaced in
// AllocationCounter.cpp for the test binary only.)

#include <cstddef>

namespace AllocationCounter {
    void start();
    std::size_t stop(); // returns allocations + frees since start()
}

struct ScopedAllocationCounter {
    ScopedAllocationCounter() { AllocationCounter::start(); }
    ~ScopedAllocationCounter() { AllocationCounter::stop(); }
};
