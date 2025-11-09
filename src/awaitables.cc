module;
#include <chrono>
#include <mutex>
#include <optional>
#include <semaphore>
#include <shared_mutex>
#include <thread>
export module jowi.asio:awaitables;
import :awaitable;

namespace jowi::asio {
  /*
    Sleeps the current thread for a certain time
  */
  export template <class clock_type = std::chrono::steady_clock> struct SleepPoller {
  private:
    clock_type::time_point __end_tp;

  public:
    using ValueType = void;
    using value_type = ValueType;
    SleepPoller(clock_type::time_point tp) noexcept : __end_tp{std::move(tp)} {}

    bool poll() {
      return clock_type::now() >= __end_tp;
    }

    void poll_block() {
      std::this_thread::sleep_until(__end_tp);
    }
  };

  /*
    Factory Functions, this could look prettier.
  */
  export template <class clock_type = std::chrono::steady_clock>
  InfiniteAwaiter<SleepPoller<clock_type>> sleep_until(
    typename clock_type::time_point tp
  ) noexcept {
    return InfiniteAwaiter<SleepPoller<clock_type>>{tp};
  }
  export template <class clock_type = std::chrono::steady_clock>
  InfiniteAwaiter<SleepPoller<clock_type>> sleep_for(std::chrono::milliseconds dur) {
    return SleepPoller{clock_type::now() + dur};
  }
  export template <class clock_type = std::chrono::steady_clock>
  InfiniteAwaiter<SleepPoller<clock_type>> sleep_for(unsigned int dur) {
    return sleep_for(std::chrono::milliseconds{dur});
  }

  /*
    Asynchronous Mutexes
  */
  template <class mutex_type>
  concept unique_lockable = requires(mutex_type m) {
    { m.try_lock() } -> std::same_as<bool>; // non blocking call
    { m.lock() }; // blocking call
    { m.unlock() }; // non blocking call
  };
  template <class mutex_type>
  concept shared_lockable = requires(mutex_type m) {
    { m.try_lock_shared() } -> std::same_as<bool>; // non blocking call
    { m.lock_shared() }; // blocking call
    { m.unlock_shared() }; // non blocking call
  };

  template <class semaphore_type>
  concept semaphore = requires(semaphore_type s) {
    { s.try_acquire() } -> std::same_as<bool>; // non blocking call
    { s.release() }; // non blocking call.
    { s.acquire() }; // blocking call
  };

  export template <unique_lockable mutex_type> struct LockPoller {
  private:
    mutex_type &__m;

  public:
    using ValueType = std::unique_lock<mutex_type>;
    using value_type = ValueType;
    LockPoller(mutex_type &m) noexcept : __m{m} {}

    std::optional<std::unique_lock<mutex_type>> poll() {
      bool is_locked = __m.try_lock();
      if (is_locked) {
        return std::unique_lock{__m, std::adopt_lock};
      }
      return std::nullopt;
    }

    std::unique_lock<mutex_type> poll_block() {
      return std::unique_lock{__m};
    }
  };

  export template <shared_lockable mutex_type> struct SharedLockPoller {
  private:
    mutex_type &__m;

  public:
    using ValueType = std::shared_lock<mutex_type>;
    using value_type = ValueType;
    SharedLockPoller(mutex_type &m) noexcept : __m{m} {}

    std::optional<std::shared_lock<mutex_type>> poll() {
      bool is_locked = __m.try_lock();
      if (is_locked) {
        return std::shared_lock{__m, std::adopt_lock};
      }
      return std::nullopt;
    }

    std::shared_lock<mutex_type> poll_block() {
      return std::shared_lock{__m};
    }
  };

  export template <semaphore semaphore_type> struct SemaAcquirePoller {
  private:
    semaphore_type &__m;

  public:
    using ValueType = void;
    using value_type = ValueType;
    SemaAcquirePoller(semaphore_type &m) noexcept : __m{m} {}

    bool poll() {
      return __m.try_acquire();
    }

    void poll_block() {
      __m.acquire();
    }
  };

  template struct SemaAcquirePoller<std::binary_semaphore>;
  template struct LockPoller<std::mutex>;
  template struct LockPoller<std::shared_mutex>;
  template struct SharedLockPoller<std::shared_mutex>;
  template struct SleepPoller<std::chrono::steady_clock>;

  template InfiniteAwaiter<SleepPoller<std::chrono::steady_clock>>
  sleep_until<std::chrono::steady_clock>(std::chrono::steady_clock::time_point tp) noexcept;
  template InfiniteAwaiter<SleepPoller<std::chrono::steady_clock>>
  sleep_for<std::chrono::steady_clock>(std::chrono::milliseconds dur);
  template InfiniteAwaiter<SleepPoller<std::chrono::steady_clock>>
  sleep_for<std::chrono::steady_clock>(unsigned int dur);

}
