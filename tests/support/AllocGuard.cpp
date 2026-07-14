#include <atomic>
#include <cstdlib>
#include <new>
#include <cstdio>

std::atomic<bool> g_inAudioThread{false};
std::atomic<int>  g_allocCount{0};

void* operator new(size_t sz) {
    if (g_inAudioThread.load(std::memory_order_relaxed)) {
        g_allocCount.fetch_add(1);
    }
    if (void* p = std::malloc(sz)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    if (g_inAudioThread.load(std::memory_order_relaxed)) g_allocCount.fetch_add(1);
    std::free(p);
}
void* operator new[](size_t sz) {
    if (g_inAudioThread.load(std::memory_order_relaxed)) g_allocCount.fetch_add(1);
    if (void* p = std::malloc(sz)) return p;
    throw std::bad_alloc();
}
void operator delete[](void* p) noexcept {
    if (g_inAudioThread.load(std::memory_order_relaxed)) g_allocCount.fetch_add(1);
    std::free(p);
}
void* operator new(size_t sz, const std::nothrow_t&) noexcept {
    if (g_inAudioThread.load(std::memory_order_relaxed)) g_allocCount.fetch_add(1);
    return std::malloc(sz);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
    if (g_inAudioThread.load(std::memory_order_relaxed)) g_allocCount.fetch_add(1);
    std::free(p);
}
void* operator new[](size_t sz, const std::nothrow_t&) noexcept {
    if (g_inAudioThread.load(std::memory_order_relaxed)) g_allocCount.fetch_add(1);
    return std::malloc(sz);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
    if (g_inAudioThread.load(std::memory_order_relaxed)) g_allocCount.fetch_add(1);
    std::free(p);
}
void operator delete(void* p, size_t) noexcept {
    if (g_inAudioThread.load(std::memory_order_relaxed)) g_allocCount.fetch_add(1);
    std::free(p);
}
void operator delete[](void* p, size_t) noexcept {
    if (g_inAudioThread.load(std::memory_order_relaxed)) g_allocCount.fetch_add(1);
    std::free(p);
}
