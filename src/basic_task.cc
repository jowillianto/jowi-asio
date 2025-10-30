module;
#include <chrono>
#include <coroutine>
#include <expected>
#include <memory>
#include <optional>
export module jowi.asio:basic_task;
import :task;
import :event_loop;

namespace jowi::asio {
  export template <class T, class V> struct basic_promise;
  export template <class V> struct basic_task;

  template <class T> struct task_awaitable {
  private:
    static_assert(task<T>);
    T __task;

  public:
    task_awaitable(T task) : __task{std::move(task)} {}

    bool await_ready() {
      return __task.is_complete();
    }

    std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> h) {
      std::shared_ptr<event_loop> loop = event_loop::require_event_loop();
      loop->push(__task.raw_coro(), false);
      while (!__task.is_complete(std::memory_order_acquire) && loop->run_one()) {
      }
      return h;
    }

    task_result_type<T> await_resume() noexcept {
      return __task.value();
    }
  };

  template <class V> struct basic_task {
  public:
    using promise_type = basic_promise<basic_task, V>;
    using coro_type = std::coroutine_handle<promise_type>;

  private:
    std::unique_ptr<void, coro_state_deleter> __addr;

    inline coro_type __coro() const noexcept {
      return coro_type::from_address(__addr.get());
    }

  public:
    basic_task(std::coroutine_handle<promise_type> h) :
      __addr{h.address(), coro_state_deleter{true}} {}

    void resume() {
      __coro().resume();
    }

    bool is_complete(std::memory_order m = std::memory_order_relaxed) const noexcept {
      return __coro().promise().is_complete(m);
    }

    V value() {
      return __coro().promise().value();
    }
    std::expected<V, task_error> expected_value() {
      return __coro().promise().expected_value();
    }

    std::coroutine_handle<void> raw_coro() const noexcept {
      return __coro();
    }

    task_awaitable<basic_task> as_awaitable() noexcept {
      return task_awaitable{std::move(*this)};
    }

    std::coroutine_handle<void> release() noexcept {
      return std::coroutine_handle<void>::from_address(__addr.release());
    }

    static basic_task from_promise(promise_type &p) {
      return basic_task{coro_type::from_promise(p)};
    }

    static basic_task from_address(void *addr) {
      return basic_task{std::coroutine_handle<promise_type>::from_address(addr)};
    }
  };

  template <class awaitable>
    requires(is_poll_awaitable<awaitable>())
  struct loop_awaitable {
  private:
    awaitable __a;
    // std::optional<basic_task<void>> __task;

    basic_task<void> __rec_wait(std::shared_ptr<event_loop> loop, std::coroutine_handle<void> h) {
      auto start = std::chrono::steady_clock::now();
      if (await_ready()) {
        h.resume();
      } else {
        loop->loop_sleep(start);
        loop->push(__rec_wait(loop, h).release(), true);
      }
      co_return;
    }

  public:
    loop_awaitable(awaitable a) : __a{std::forward<awaitable>(a)} {}

    bool await_ready() {
      return __a.await_ready();
    }

    void await_suspend(std::coroutine_handle<void> h) {
      auto loop = event_loop::require_event_loop();
      // __task.emplace(__rec_wait(loop, h));
      // loop->push(__task->raw_coro(), false);
      loop->push(__rec_wait(loop, h).release(), true);
    }

    await_result_type<awaitable> await_resume() {
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

  basic_task<uint64_t> fib(uint64_t beg) {
    if (beg == 0) {
      co_return 1ull;
    } else if (beg == 1) {
      co_return 1ull;
    } else {
      co_return co_await fib(beg - 1).as_awaitable() + co_await fib(beg - 2).as_awaitable();
    }
  }
}
