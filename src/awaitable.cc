module;
#include <coroutine>
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

  /*
    gets the awaitable result.
  */
  export template <awaitable awaitable_type>
  using await_result_type = decltype(std::declval<awaitable_type>().await_resume());
}