#include "AllocationCounter.h"

#include <cstdlib>
#include <new>

namespace {
    thread_local bool counting = false;
    thread_local std::size_t count = 0;

    void release(void* p) noexcept {
        if (counting && p != nullptr) {
            count++;
        }
        std::free(p);
    }

    void* allocate(std::size_t size) {
        if (counting) {
            count++;
        }
        if (void* p = std::malloc(size == 0 ? 1 : size)) {
            return p;
        }
        throw std::bad_alloc();
    }
}

void AllocationCounter::start() {
    count = 0;
    counting = true;
}

std::size_t AllocationCounter::stop() {
    counting = false;
    return count;
}

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return allocate(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return allocate(size); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
