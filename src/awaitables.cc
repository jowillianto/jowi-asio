module;
#include <chrono>
#include <coroutine>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <thread>
export module jowi.asio:awaitables;
import :awaitable;

namespace jowi::asio {
  /*
    Asychronous Sleeping
  */
  /*
    Sleeps the current thread for a certain time
  */
  export struct sleep_awaitable {
  private:
    std::chrono::system_clock::time_point __end;

  public:
    static constexpr auto is_defer_awaitable = true;
    sleep_awaitable(std::chrono::system_clock::time_point tp) noexcept : __end{std::move(tp)} {}
    bool await_ready() const noexcept {
      return __end <= std::chrono::system_clock::now();
    }
    auto await_suspend(std::coroutine_handle<void> h) const noexcept {
      std::this_thread::sleep_until(__end);
      return h;
    }
    void await_resume() const noexcept {}
  };

  /*
    Factory Functions, this could look prettier.
  */
  export sleep_awaitable sleep_until(std::chrono::system_clock::time_point tp) noexcept {
    return sleep_awaitable{tp};
  }
  export sleep_awaitable sleep_for(std::chrono::milliseconds dur) {
    return sleep_awaitable{std::chrono::system_clock::now() + dur};
  }
  export sleep_awaitable sleep_for(unsigned int dur) {
    return sleep_for(std::chrono::milliseconds{dur});
  }

  /*
    Asynchronous Mutexes
  */
  template <class mutex_type>
  concept unique_lockable = requires(mutex_type m) {
    { m.try_lock() } -> std::same_as<bool>;
    { m.lock() };
    { m.unlock() };
  };
  template <class mutex_type>
  concept shared_lockable = requires(mutex_type m) {
    { m.try_lock_shared() } -> std::same_as<bool>;
    { m.lock_shared() };
    { m.unlock_shared() };
  };

  export template <unique_lockable mutex_type> struct awaitable_lock {
  private:
    std::reference_wrapper<mutex_type> __m;
    std::optional<std::unique_lock<mutex_type>> __res;

  public:
    static constexpr auto is_defer_awaitable = true;
    awaitable_lock(mutex_type &m) noexcept : __m{std::ref(m)} {}
    bool await_ready() {
      bool is_locked = __m.get().try_lock();
      if (is_locked) {
        __res.emplace(__m, std::adopt_lock);
        return false;
      }
      return true;
    }
    std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> h) {
      __res.emplace(__m);
      return h;
    }
    std::unique_lock<mutex_type> await_resume() noexcept {
      return std::move(__res).value();
    }
  };

  export template <shared_lockable mutex_type> struct awaitable_lock_shared {
  private:
    std::reference_wrapper<mutex_type> __m;
    std::optional<std::shared_lock<mutex_type>> __res;

  public:
    awaitable_lock_shared(mutex_type &m) noexcept : __m{std::ref(m)} {}
    static constexpr auto is_defer_awaitable = true;
    bool await_ready() {
      bool is_locked = __m.get().try_lock_shared();
      if (is_locked) {
        __res.emplace(__m, std::adopt_lock);
        return true;
      }
      return false;
    }

    std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> h) {
      __res.emplace(__m);
      return h;
    }

    std::shared_lock<mutex_type> await_resume() noexcept {
      return std::move(__res).value();
    }
  };

  template struct awaitable_lock<std::mutex>;
  template struct awaitable_lock<std::shared_mutex>;
  template struct awaitable_lock_shared<std::shared_mutex>;
}