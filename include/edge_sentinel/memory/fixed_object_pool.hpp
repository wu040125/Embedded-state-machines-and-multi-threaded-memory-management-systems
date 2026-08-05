#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace edge_sentinel::memory {

enum class PoolReleaseResult {
    released,
    foreign_pointer,
    double_release,
};

struct PoolStats {
    std::size_t capacity{0};
    std::size_t in_use{0};
    std::size_t high_watermark{0};
    std::size_t successful_allocations{0};
    std::size_t successful_releases{0};
    std::size_t allocation_failures{0};
    std::size_t invalid_releases{0};
};

template <typename T, std::size_t Capacity>
class FixedObjectPool final {
    static_assert(Capacity > 0);

public:
    class Handle final {
    public:
        Handle() noexcept = default;

        ~Handle() {
            reset();
        }

        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        Handle(Handle&& other) noexcept
            : pool_(std::exchange(other.pool_, nullptr)),
              object_(std::exchange(other.object_, nullptr)) {}

        Handle& operator=(Handle&& other) noexcept {
            if (this != &other) {
                reset();
                pool_ = std::exchange(other.pool_, nullptr);
                object_ = std::exchange(other.object_, nullptr);
            }
            return *this;
        }

        [[nodiscard]] T* get() const noexcept {
            return object_;
        }

        [[nodiscard]] T& operator*() const noexcept {
            return *object_;
        }

        [[nodiscard]] T* operator->() const noexcept {
            return object_;
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return object_ != nullptr;
        }

        void reset() noexcept {
            T* object = std::exchange(object_, nullptr);
            FixedObjectPool* pool = std::exchange(pool_, nullptr);
            if (object != nullptr && pool != nullptr) {
                static_cast<void>(pool->destroy(object));
            }
        }

        [[nodiscard]] T* release() noexcept {
            pool_ = nullptr;
            return std::exchange(object_, nullptr);
        }

    private:
        friend class FixedObjectPool;

        Handle(FixedObjectPool* pool, T* object) noexcept : pool_(pool), object_(object) {}

        FixedObjectPool* pool_{nullptr};
        T* object_{nullptr};
    };

    FixedObjectPool() noexcept {
        for (std::size_t index = 0; index < Capacity; ++index) {
            slots_[index].next = index + 1;
        }
        slots_[Capacity - 1].next = kNoSlot;
    }

    ~FixedObjectPool() {
        for (Slot& slot : slots_) {
            if (slot.occupied) {
                std::destroy_at(slot.object());
            }
        }
    }

    FixedObjectPool(const FixedObjectPool&) = delete;
    FixedObjectPool& operator=(const FixedObjectPool&) = delete;
    FixedObjectPool(FixedObjectPool&&) = delete;
    FixedObjectPool& operator=(FixedObjectPool&&) = delete;

    template <typename... Arguments>
    [[nodiscard]] Handle try_create(Arguments&&... arguments) {
        std::lock_guard lock(mutex_);
        if (free_head_ == kNoSlot) {
            ++stats_.allocation_failures;
            return {};
        }

        Slot& slot = slots_[free_head_];
        T* object = std::construct_at(slot.object(), std::forward<Arguments>(arguments)...);
        free_head_ = slot.next;
        slot.next = kNoSlot;
        slot.occupied = true;

        ++stats_.in_use;
        ++stats_.successful_allocations;
        if (stats_.in_use > stats_.high_watermark) {
            stats_.high_watermark = stats_.in_use;
        }

        return Handle{this, object};
    }

    [[nodiscard]] PoolReleaseResult destroy(T* object) noexcept {
        std::lock_guard lock(mutex_);
        const std::size_t index = find_slot(object);
        if (index == kNoSlot) {
            ++stats_.invalid_releases;
            return PoolReleaseResult::foreign_pointer;
        }

        Slot& slot = slots_[index];
        if (!slot.occupied) {
            ++stats_.invalid_releases;
            return PoolReleaseResult::double_release;
        }

        std::destroy_at(slot.object());
        slot.occupied = false;
        slot.next = free_head_;
        free_head_ = index;
        --stats_.in_use;
        ++stats_.successful_releases;
        return PoolReleaseResult::released;
    }

    [[nodiscard]] PoolStats stats() const noexcept {
        std::lock_guard lock(mutex_);
        PoolStats snapshot = stats_;
        snapshot.capacity = Capacity;
        return snapshot;
    }

    [[nodiscard]] std::size_t outstanding() const noexcept {
        return stats().in_use;
    }

private:
    static constexpr std::size_t kNoSlot = Capacity;

    struct Slot {
        [[nodiscard]] T* object() noexcept {
            return reinterpret_cast<T*>(storage.data());
        }

        [[nodiscard]] const T* object() const noexcept {
            return reinterpret_cast<const T*>(storage.data());
        }

        alignas(T) std::array<std::byte, sizeof(T)> storage{};
        std::size_t next{kNoSlot};
        bool occupied{false};
    };

    [[nodiscard]] std::size_t find_slot(const T* object) const noexcept {
        if (object == nullptr) {
            return kNoSlot;
        }

        for (std::size_t index = 0; index < Capacity; ++index) {
            if (slots_[index].object() == object) {
                return index;
            }
        }
        return kNoSlot;
    }

    mutable std::mutex mutex_;
    std::array<Slot, Capacity> slots_{};
    std::size_t free_head_{0};
    PoolStats stats_{};
};

}  // namespace edge_sentinel::memory
