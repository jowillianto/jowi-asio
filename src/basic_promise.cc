module;
#include <atomic>
#include <concepts>
#include <coroutine>
#include <exception>
#include <expected>
#include <optional>
export module jowi.asio:basic_promise;
import :task;

namespace jowi::asio {
  template <class value_type> struct promise_state {
  private:
    std::atomic_flag __complete;
    std::optional<std::expected<value_type, task_error>> __v;

  public:
    promise_state() : __complete{false}, __v{std::nullopt} {}

    template <class... Args>
      requires(
        (std::same_as<value_type, void> && sizeof...(Args) == 0) ||
        std::constructible_from<value_type, Args...>
      )
    void emplace(Args &&...args) {
      __v.emplace(std::forward<Args>(args)...);
      __complete.test_and_set(std::memory_order_release);
    }

    void capture_exception() {
      __v.emplace(std::unexpected<task_error>{std::current_exception()});
      __complete.test_and_set(std::memory_order_release);
    }

    bool is_complete(std::memory_order m = std::memory_order_relaxed) const noexcept {
      return __complete.test(m);
    }

    std::expected<value_type, task_error> expected_value() {
      return std::move(__v).value();
    }

    value_type value() {
      if (__v->has_value()) {
        if constexpr (!std::same_as<value_type, void>) {
          return std::move(__v)->value();
        }
        __v.reset();
        return;
      }
      __v->error().rethrow();
    }
  };

  template <class task_type, class value_type> struct basic_promise {
  private:
    static_assert(from_promiseable<task_type, basic_promise>);
    promise_state<value_type> __s;

  public:
    basic_promise() : __s{} {}

    task_type get_return_object() {
      return task_type::from_promise(*this);
    }

    std::suspend_always initial_suspend() const noexcept {
      return std::suspend_always{};
    }
    std::suspend_always final_suspend() const noexcept {
      return std::suspend_always{};
    }

    template <class... Args>
      requires(std::constructible_from<value_type, Args...>)
    void return_value(Args &&...args) {
      __s.emplace(std::forward<Args>(args)...);
    }

    void unhandled_exception() {
      __s.capture_exception();
    }
  };

  template <class task_type> struct basic_promise<task_type, void> {
  private:
    static_assert(from_promiseable<task_type, basic_promise>);
    promise_state<void> __s;

  public:
    basic_promise() : __s{} {}

    task_type get_return_object() {
      return task_type::from_promise(*this);
    }

    std::suspend_always initial_suspend() const noexcept {
      return std::suspend_always{};
    }
    std::suspend_always final_suspend() const noexcept {
      return std::suspend_always{};
    }

    bool is_complete(std::memory_order m = std::memory_order_relaxed) const noexcept {
      return __s.is_complete(m);
    }

    void return_void() {
      __s.emplace();
    }

    void unhandled_exception() {
      __s.capture_exception();
    }

    void value() {
      return __s.value();
    }

    std::expected<void, task_error> expected_value() {
      return __s.expected_value();
    }
  };
}
