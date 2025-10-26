module;
#include <concepts>
#include <coroutine>
#include <exception>
#include <expected>
export module jowi.asio:task;
import :awaitable;

namespace jowi::asio {
  export struct task_error : public std::exception {
  private:
    std::exception_ptr __e;

  public:
    task_error(std::exception_ptr e) : __e{std::move(e)} {}

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
    } -> std::same_as<std::expected<typename promise_type::value_type, task_error>>;
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
    } -> std::same_as<std::expected<typename task_type::promise_type::value_type, task_error>>;
    {
      task_type::from_promise(std::declval<typename task_type::promise_type &>())
    } -> std::same_as<task_type>;
  };

  export template <task task_type>
  using task_result_type = std::invoke_result_t<decltype(&task_type::value), task_type *>;

  export template <task task_type>
  using task_expected_type =
    std::invoke_result_t<decltype(&task_type::expected_value), task_type *>;
}
