module;
#include <concepts>
#include <coroutine>
#include <exception>
#include <expected>
export module jowi.asio:task;
import :awaitable;

namespace jowi::asio {
  export struct task_error {
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

      // CODE THAT WILL NEVER BE EXECUTED.
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
  concept promise = requires(const std::decay_t<promise_type> cp, std::decay_t<promise_type> p) {
    { std::declval<typename promise_type::value_type>() };
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

  template <class task_type>
  concept task = requires(const std::decay_t<task_type> ct, std::decay_t<task_type> t) {
    promise<typename task_type::promise_type>;
    { ct.is_complete() } -> std::same_as<bool>;
    { t.value() } -> std::same_as<typename task_type::promise_type::value_type>;
    {
      t.expected_value()
    } -> std::same_as<std::expected<typename task_type::promise_type::value_type, task_error>>;
    {
      task_type::from_promise(std::declval<typename task_type::promise_type &>())
    } -> std::same_as<task_type>;
  };

  template <task task_type>
  using task_result_type = std::invoke_result_t<decltype(&task_type::value), task_type *>;
  template <task task_type>
  using task_expected_type =
    std::invoke_result_t<decltype(&task_type::expected_value), task_type *>;
}