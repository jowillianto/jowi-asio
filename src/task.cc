module;
#include <atomic>
#include <concepts>
#include <coroutine>
#include <exception>
#include <expected>
#include <optional>
export module jowi.asio:task;
import :awaitable;

namespace jowi::asio {
  /*
   * TaskError
   * wraps an exception pointer such that it is rethrowable and follows the convention specified by
   * the C++ standard library.
   */
  export struct TaskError : public std::exception {
  private:
    std::exception_ptr __e;

  public:
    TaskError(std::exception_ptr e) : __e{std::move(e)} {}

    const char *what() const noexcept {
      try {
        rethrow();
      } catch (const std::exception &e) {
        return e.what();
      }
      /*
       * This will never be executed.
       */
      return nullptr;
    }

    void rethrow() const {
      std::rethrow_exception(__e);
    }
  };

  /*
   * PromiseState.
   * A state machine that holds the current state of a promise.
   */
  template <class value_type> struct PromiseState {
  private:
    std::atomic_flag __complete;
    std::optional<std::expected<value_type, TaskError>> __v;

  public:
    PromiseState() : __complete{false}, __v{std::nullopt} {}

    template <class... Args>
      requires(
        (std::same_as<value_type, void> && sizeof...(Args) == 0) ||
        std::constructible_from<value_type, Args...>
      )
    void emplace(Args &&...args) {
      if constexpr (std::same_as<void, value_type>) {
        __v.emplace(std::expected<void, TaskError>{});
      } else {
        __v.emplace(value_type{std::forward<Args>(args)...});
      }
      __complete.test_and_set(std::memory_order_release);
    }

    void capture_exception() {
      __v.emplace(std::unexpected<TaskError>{std::current_exception()});
      __complete.test_and_set(std::memory_order_release);
    }

    bool is_complete(std::memory_order m = std::memory_order_relaxed) const noexcept {
      return __complete.test(m);
    }

    std::expected<value_type, TaskError> expected_value() {
      return std::move(__v).value();
    }

    value_type value() {
      if (__v->has_value()) {
        if constexpr (!std::same_as<value_type, void>) {
          return std::move(__v)->value();
        } else {
          __v.reset();
          return;
        }
      }
      __v->error().rethrow();
      throw;
    }
  };

  /*
    probably useless definition
  */
  template <class promise_type>
  concept promise = requires(const promise_type cp, promise_type p) {
    { std::declval<typename promise_type::value_type>() }; // promise return value type.
    { std::declval<typename promise_type::task_type>() };
    { p.get_return_object() } -> std::same_as<typename promise_type::task_type>;
    { p.initial_suspend() } -> awaitable;
    { p.final_suspend() } -> awaitable;
    { p.unhandled_exception() } -> std::same_as<void>;
    { p.value() } -> std::same_as<typename promise_type::value_type>;
    {
      p.expected_value()
    } -> std::same_as<std::expected<typename promise_type::value_type, TaskError>>;
  };

  export template <class task_type, class promise_type>
  concept from_promiseable = requires(promise_type &p) {
    { task_type::from_promise(p) } -> std::same_as<task_type>;
  };

  export template <class task_type>
  concept task = requires(const task_type ct, task_type t) {
    promise<typename task_type::promise_type>;
    { ct.is_complete() } -> std::same_as<bool>;
    { t.value() } -> std::same_as<typename task_type::promise_type::value_type>;
    { ct.raw_coro() } -> std::same_as<std::coroutine_handle<void>>;
    {
      t.expected_value()
    } -> std::same_as<std::expected<typename task_type::promise_type::value_type, TaskError>>;
    {
      task_type::from_promise(std::declval<typename task_type::promise_type &>())
    } -> std::same_as<task_type>;
    { task_type::from_address(std::declval<void *>()) } -> std::same_as<task_type>;
  };

  export template <task task_type>
  using TaskResultType = std::invoke_result_t<decltype(&task_type::value), task_type *>;

  export template <task task_type>
  using TaskExpectedType =
    std::invoke_result_t<decltype(&task_type::expected_value), task_type *>;
}
