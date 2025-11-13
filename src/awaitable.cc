module;
#include <chrono>
#include <concepts>
#include <coroutine>
#include <optional>
#include <thread>
export module jowi.asio:awaitable;

namespace jowi::asio {
  /*
    The return value constraint from await_suspend. This should either reschedule, bool, void or a
    new coroutine to handle.
  */
  template <class T>
  concept awaitable_suspend_value = std::same_as<T, bool> ||
    std::convertible_to<T, std::coroutine_handle<void>> || std::same_as<T, void>;

  /*
    poll awaitable version, this is useful for system calls that support checking if something is
    ready. This includes lock, .... Makes things not blockable. A poll awaitable is always
    awaitable, while not all awaitables are poll awaitable. Try to design your code in a way that
    it is poll awaitable.
  */
  export template <class awaitable_type>
  concept poll_awaitable = requires(awaitable_type t) {
    { t.await_ready() } -> std::same_as<bool>;
    { t.await_resume() };
    { awaitable_type::is_defer_awaitable } -> std::convertible_to<bool>;
  };

  /*
    This is a standard awaitable as defined by std.
  */
  export template <class awaitable_type>
  concept awaitable = requires(awaitable_type t, std::coroutine_handle<void> h) {
    { t.await_ready() } -> std::same_as<bool>;
    { t.await_suspend(h) } -> awaitable_suspend_value;
    { t.await_resume() };
  };

  /*
    Checks if a certain awaitable type is poll awaitable.
  */
  export template <awaitable awaitable_type> consteval bool is_poll_awaitable() {
    if constexpr (poll_awaitable<awaitable_type>) {
      return awaitable_type::is_defer_awaitable;
    } else {
      return false;
    }
  }

  template <class poll_type>
  concept repeat_poller = requires(poll_type p) {
    { std::declval<typename poll_type::ValueType>() };
    {
      p.poll()
    } -> std::same_as<std::conditional_t<
      std::same_as<typename poll_type::ValueType, void>,
      bool,
      std::optional<typename poll_type::ValueType>>>;
  };

  export template <awaitable awaitable_type>
  using AwaitResultType = decltype(std::declval<awaitable_type>().await_resume());

  template <class poll_type>
  concept bLockPoller = repeat_poller<poll_type> && requires(poll_type p) {
    { p.poll_block() } -> std::same_as<typename poll_type::ValueType>;
  };

  export template <repeat_poller poll_type> struct InfiniteAwaiter {
  private:
    using ResultType = std::conditional_t<
      std::same_as<typename poll_type::ValueType, void>,
      bool,
      std::optional<typename poll_type::ValueType>>;
    poll_type __p;
    ResultType __res;

  public:
    static constexpr bool is_defer_awaitable = true;

    InfiniteAwaiter(poll_type p) : __p(std::forward<poll_type>(p)) {}
    template <class... Args> requires(std::constructible_from<poll_type, Args...>)
    InfiniteAwaiter(Args &&...args) : InfiniteAwaiter(poll_type{std::forward<Args>(args)...}) {}

    bool await_ready() {
      __res = __p.poll();
      return static_cast<bool>(__res);
    }

    std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> h) {
      if constexpr (bLockPoller<poll_type>) {
        if constexpr (!std::same_as<ResultType, bool>) {
          __res.emplace(__p.poll_block());
        }
      } else {
        while (!await_ready()) {
          std::this_thread::yield();
        }
      }
      return h;
    }

    typename poll_type::ValueType await_resume() {
      if constexpr (!std::same_as<ResultType, bool>) {
        return std::move(__res).value();
      }
    }
  };

  export template <repeat_poller poll_type, class clock_type = std::chrono::steady_clock>
  struct TimedAwaiter {
  private:
    poll_type __p;
    typename clock_type::time_point __end_tp;
    using ResultType = std::conditional_t<
      std::same_as<typename poll_type::ValueType, void>,
      bool,
      std::optional<typename poll_type::ValueType>>;
    ResultType __res;

  public:
    static constexpr bool is_defer_awaitable = true;

    TimedAwaiter(std::chrono::milliseconds dur, poll_type p) :
      __p{std::forward<poll_type>(p)}, __end_tp{clock_type::now() + dur} {}
    template <class... Args> requires(std::constructible_from<poll_type, Args...>)
    TimedAwaiter(std::chrono::milliseconds dur, Args &&...args) :
      TimedAwaiter(dur, poll_type{std::forward<Args>(args)...}) {}

    /*
     * this allows timed_awaiter to have the same semantics as infinite awaiter
     */
    template <class... Args> requires(std::constructible_from<poll_type, Args...>)
    TimedAwaiter(Args &&...args) : TimedAwaiter(0, poll_type{std::forward<Args>(args)...}) {}

    TimedAwaiter &set_timeout(std::chrono::milliseconds dur) {
      __end_tp = clock_type::now() + dur;
      return *this;
    }

    bool await_ready() {
      __res = __p.poll();
      bool is_overtime = clock_type::now() >= __end_tp;
      return static_cast<bool>(__res) || is_overtime;
    }

    std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> h) {
      while (!await_ready()) {
        std::this_thread::yield();
      }
      return h;
    }

    ResultType await_resume() {
      return static_cast<bool>(std::move(__res));
    }
  };
}
