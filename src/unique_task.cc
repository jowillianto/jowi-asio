module;
#include <atomic>
#include <coroutine>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
export module jowi.asio:unique_task;
import :task;
import :event_loop;

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
        } else {
          __v.reset();
          return;
        }
      }
      __v->error().rethrow();
      throw;
    }
  };

  export template <class value_type> struct unique_task;
  export template <class value_type> struct shared_task;
  template <class T, class V> struct basic_promise;

  template <class value_type> struct unique_task {
  public:
    using promise_type = basic_promise<unique_task, value_type>;
    using coro_type = std::coroutine_handle<promise_type>;

  private:
    std::unique_ptr<void, coro_state_deleter> __addr;

    inline coro_type __coro() const noexcept {
      return coro_type::from_address(__addr.get());
    }

    unique_task<void> __schedule(std::shared_ptr<event_loop> loop, std::coroutine_handle<void> h) {
      if (is_complete(std::memory_order_acquire)) {
        h.resume();
      } else {
        loop->push(__schedule(loop, h).release(), true);
      }
      co_return;
    }

  public:
    unique_task(std::coroutine_handle<promise_type> h) :
      __addr{h.address(), coro_state_deleter{true}} {}

    void resume() {
      __coro().resume();
    }

    bool is_complete(std::memory_order m = std::memory_order_relaxed) const noexcept {
      return __coro().promise().is_complete(m);
    }

    auto value() {
      return __coro().promise().value();
    }
    auto expected_value() {
      return __coro().promise().expected_value();
    }

    std::coroutine_handle<void> raw_coro() const noexcept {
      return __coro();
    }

    static unique_task from_promise(promise_type &p) {
      return unique_task{coro_type::from_promise(p)};
    }

    bool await_ready() {
      return is_complete(std::memory_order_acquire);
    }

    auto await_suspend(std::coroutine_handle<void> h) {
      // resume();
      // return h;
      std::shared_ptr<event_loop> loop = event_loop::require_event_loop();
      // loop->push(__schedule(loop));
      // loop->push(__schedule(loop, h).release(), true);
      // // loop->push(h, false);
      // return raw_coro();
      loop->push(raw_coro(), false);
      while (!is_complete(std::memory_order_acquire) && loop->run_one()) {
      }
      return h;
    }
    std::coroutine_handle<void> release() && {
      return std::coroutine_handle<void>::from_address(__addr.release());
    }

    value_type await_resume() {
      return value();
    }
  };

  template <class value_type> struct shared_task {
  public:
    using promise_type = basic_promise<shared_task, value_type>;
    using coro_type = std::coroutine_handle<promise_type>;

  private:
    std::shared_ptr<void> __addr;

    inline coro_type __coro() const noexcept {
      return coro_type::from_address(__addr.get());
    }

  public:
    shared_task(std::coroutine_handle<promise_type> h) :
      __addr{h.address(), coro_state_deleter{}} {}

    void resume() {
      __coro().resume();
    }

    std::coroutine_handle<void> raw_coro() const noexcept {
      return __coro();
    }

    bool is_complete(std::memory_order m = std::memory_order_relaxed) const noexcept {
      return __coro().promise().is_complete(m);
    }

    std::shared_ptr<void> address() {
      return __addr;
    }

    auto value() {
      return __coro().promise().value();
    }
    auto expected_value() {
      return __coro().promise().expected_value();
    }

    static constexpr auto is_defer_awaitable = true;
    bool await_ready() const {
      return is_complete(std::memory_order_acquire);
    }

    std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> h) const {
      return h;
    }

    value_type await_resume() {
      return value();
    }

    static shared_task from_promise(promise_type &p) {
      return shared_task{coro_type::from_promise(p)};
    }
  };

  template <class awaitable>
    requires(is_poll_awaitable<awaitable>())
  struct loop_awaitable {
  private:
    awaitable __a;

    unique_task<void> __rec_wait(std::shared_ptr<event_loop> l, std::coroutine_handle<void> h) {
      if (await_ready()) {
        h.resume();
      } else {
        l->push(__rec_wait(l, h).release(), true);
      }
      co_return;
    }

  public:
    loop_awaitable(awaitable a) : __a{std::forward<awaitable>(__a)} {}

    bool await_ready() {
      return __a.await_ready();
    }

    void await_suspend(std::coroutine_handle<void> h) {
      auto loop = event_loop::require_event_loop();
      loop->push(__rec_wait(loop, h).release(), true);
    }

    auto await_resume() {
      return __a.await_resume();
    }
  };

  template <class T, class V> struct basic_promise {
  private:
    static_assert(from_promiseable<T, basic_promise>);
    promise_state<V> __s;

  public:
    using task_type = T;
    using value_type = V;
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

    value_type value() {
      return __s.value();
    }

    std::expected<value_type, task_error> expected_value() {
      return __s.expected_value();
    }

    template <class awaitable> auto await_transform(awaitable a) const noexcept {
      if constexpr (is_poll_awaitable<awaitable>()) {
        return loop_awaitable{std::forward<awaitable>(a)};
      } else {
        return a;
      }
    }

    bool is_complete(std::memory_order m = std::memory_order_relaxed) const noexcept {
      return __s.is_complete(m);
    }
  };

  template <class T> struct basic_promise<T, void> {
  private:
    static_assert(from_promiseable<T, basic_promise>);
    promise_state<void> __s;

  public:
    using task_type = T;
    using value_type = void;
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

    template <class awaitable> auto await_transform(awaitable a) const noexcept {
      if constexpr (is_poll_awaitable<awaitable>()) {
        return loop_awaitable{std::forward<awaitable>(a)};
      } else {
        return a;
      }
    }
  };
}
