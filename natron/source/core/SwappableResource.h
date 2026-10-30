#pragma once

#include <atomic>
#include <memory>
#include <vector>

// Lets the message thread replace data the audio thread reads (a wavetable, a sample, a compiled
// formula) without locks, allocation, or freeing on the audio thread.
//
// The audio thread calls acquire() once per block and uses the returned pointer for that block.
// It publishes what it is using (a hazard pointer) and re-checks, so the message thread never frees
// an object the audio thread might be reading. Replaced objects are freed by collect() on the
// message thread once the audio thread has moved on.
template <typename T>
class SwappableResource {
public:
    SwappableResource() = default;
    SwappableResource(const SwappableResource&) = delete;
    SwappableResource& operator=(const SwappableResource&) = delete;

    ~SwappableResource() {
        delete current.load();
        for (auto* item : retired) {
            delete item;
        }
    }

    // Message thread.
    void set(std::unique_ptr<T> next) {
        T* old = current.exchange(next.release());
        if (old != nullptr) {
            retired.push_back(old);
        }
        collect();
    }

    // Message thread: frees replaced objects the audio thread no longer uses.
    void collect() {
        const T* busy = inUse.load();
        for (auto it = retired.begin(); it != retired.end();) {
            if (*it != busy) {
                delete *it;
                it = retired.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Message thread: the latest object (for saving, or UI).
    const T* get() const { return current.load(); }

    // Audio thread: the object to use for this block (may be nullptr).
    const T* acquire() {
        T* item = current.load();
        for (;;) {
            inUse.store(item);
            T* check = current.load();
            if (check == item) {
                return item;
            }
            item = check;
        }
    }

    size_t numRetired() const { return retired.size(); }

private:
    std::atomic<T*> current { nullptr };
    std::atomic<T*> inUse { nullptr };
    std::vector<T*> retired;
};
