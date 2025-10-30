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
    { std::declval<typename poll_type::value_type>() };
    {
      p.poll()
    } -> std::same_as<std::conditional_t<
      std::same_as<typename poll_type::value_type, void>,
      bool,
      std::optional<typename poll_type::value_type>>>;
  };

  export template <awaitable awaitable_type>
  using await_result_type = decltype(std::declval<awaitable_type>().await_resume());

  template <class poll_type>
  concept block_poller = repeat_poller<poll_type> && requires(poll_type p) {
    { p.poll_block() } -> std::same_as<typename poll_type::value_type>;
  };

  export template <repeat_poller poll_type> struct infinite_awaiter {
  private:
    using result_type = std::conditional_t<
      std::same_as<typename poll_type::value_type, void>,
      bool,
      std::optional<typename poll_type::value_type>>;
    poll_type __p;
    result_type __res;

  public:
    static constexpr bool is_defer_awaitable = true;
    template <class... Args>
      requires(std::constructible_from<poll_type, Args...>)
    infinite_awaiter(Args &&...args) : __p{std::forward<Args>(args)...} {}

    bool await_ready() {
      __res = __p.poll();
      return __res;
    }

    std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> h) {
      if constexpr (block_poller<poll_type>) {
        if constexpr (!std::same_as<result_type, bool>) {
          __res.emplace(__p.poll_block());
        }
      } else {
        while (!await_ready()) {
          std::this_thread::yield();
        }
      }
      return h;
    }

    typename poll_type::value_type await_resume() {
      if constexpr (!std::same_as<result_type, bool>) {
        return std::move(__res).value();
      }
    }
  };

  export template <repeat_poller poll_type, class clock_type = std::chrono::steady_clock>
  struct timed_awaiter {
  private:
    poll_type __p;
    clock_type::time_point __end_tp;
    using result_type = std::conditional_t<
      std::same_as<typename poll_type::value_type, void>,
      bool,
      std::optional<typename poll_type::value_type>>;
    result_type __res;

  public:
    static constexpr bool is_defer_awaitable = true;
    template <class... Args>
      requires(std::constructible_from<poll_type, Args...>)
    timed_awaiter(std::chrono::milliseconds duration, Args &&...args) :
      __p{std::forward<Args>(args)...}, __end_tp{clock_type::now() + duration} {}

    bool await_ready() {
      __res = __p.poll();
      bool is_overtime = clock_type::now() >= __end_tp;
      return __res || is_overtime;
    }

    std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> h) {
      while (!await_ready()) {
        std::this_thread::yield();
      }
      return h;
    }

    result_type await_resume() {
      return std::move(__res);
    }
  };
}
