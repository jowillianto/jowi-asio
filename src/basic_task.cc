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
  export template <class T, class V> struct BasicPromise;
  export template <class V> struct BasicTask;

  template <class T> struct TaskAwaitable {
  private:
    static_assert(task<T>);
    T __task;

  public:
    TaskAwaitable(T task) : __task{std::move(task)} {}

    bool await_ready() {
      return __task.is_complete();
    }

    std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> h) {
      std::shared_ptr<EventLoop> loop = EventLoop::require_event_loop();
      loop->push(__task.raw_coro(), false);
      while (!__task.is_complete(std::memory_order_acquire) && loop->run_one()) {
      }
      return h;
    }

    TaskResultType<T> await_resume() noexcept {
      return __task.value();
    }
  };

  template <class V> struct BasicTask {
  public:
    using PromiseType = BasicPromise<BasicTask, V>;
    using promise_type = PromiseType;
    using CoroType = std::coroutine_handle<PromiseType>;

  private:
    std::unique_ptr<void, CoroStateDeleter> __addr;

    inline CoroType __coro() const noexcept {
      return CoroType::from_address(__addr.get());
    }

  public:
    BasicTask(std::coroutine_handle<PromiseType> h) :
      __addr{h.address(), CoroStateDeleter{true}} {}

    void resume() {
      __coro().resume();
    }

    bool is_complete(std::memory_order m = std::memory_order_relaxed) const noexcept {
      return __coro().promise().is_complete(m);
    }

    V value() {
      return __coro().promise().value();
    }
    std::expected<V, TaskError> expected_value() {
      return __coro().promise().expected_value();
    }

    std::coroutine_handle<void> raw_coro() const noexcept {
      return __coro();
    }

    TaskAwaitable<BasicTask> as_awaitable() noexcept {
      return TaskAwaitable{std::move(*this)};
    }

    std::coroutine_handle<void> release() noexcept {
      return std::coroutine_handle<void>::from_address(__addr.release());
    }

    static BasicTask from_promise(PromiseType &p) {
      return BasicTask{CoroType::from_promise(p)};
    }

    static BasicTask from_address(void *addr) {
      return BasicTask{std::coroutine_handle<PromiseType>::from_address(addr)};
    }
  };

  template <class awaitable>
    requires(is_poll_awaitable<awaitable>())
  struct LoopAwaitable {
  private:
    awaitable __a;
    // std::optional<BasicTask<void>> __task;

    BasicTask<void> __rec_wait(std::shared_ptr<EventLoop> loop, std::coroutine_handle<void> h) {
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
    LoopAwaitable(awaitable a) : __a{std::forward<awaitable>(a)} {}

    bool await_ready() {
      return __a.await_ready();
    }

    void await_suspend(std::coroutine_handle<void> h) {
      auto loop = EventLoop::require_event_loop();
      // __task.emplace(__rec_wait(loop, h));
      // loop->push(__task->raw_coro(), false);
      loop->push(__rec_wait(loop, h).release(), true);
    }

    AwaitResultType<awaitable> await_resume() {
      return __a.await_resume();
    }
  };

  template <class T, class V> struct BasicPromise {
  private:
    static_assert(from_promiseable<T, BasicPromise>);
    PromiseState<V> __s;

  public:
    using TaskType = T;
    using ValueType = V;
    using value_type = ValueType;
    BasicPromise() : __s{} {}

    TaskType get_return_object() {
      return TaskType::from_promise(*this);
    }

    std::suspend_always initial_suspend() const noexcept {
      return std::suspend_always{};
    }
    std::suspend_always final_suspend() const noexcept {
      return std::suspend_always{};
    }

    template <class... Args>
      requires(std::constructible_from<ValueType, Args...>)
    void return_value(Args &&...args) {
      __s.emplace(std::forward<Args>(args)...);
    }

    void unhandled_exception() {
      __s.capture_exception();
    }

    ValueType value() {
      return __s.value();
    }

    std::expected<ValueType, TaskError> expected_value() {
      return __s.expected_value();
    }

    template <class awaitable> auto await_transform(awaitable a) const noexcept {
      if constexpr (is_poll_awaitable<awaitable>()) {
        return LoopAwaitable{std::forward<awaitable>(a)};
      } else {
        return a;
      }
    }

    bool is_complete(std::memory_order m = std::memory_order_relaxed) const noexcept {
      return __s.is_complete(m);
    }
  };

  template <class T> struct BasicPromise<T, void> {
  private:
    static_assert(from_promiseable<T, BasicPromise>);
    PromiseState<void> __s;

  public:
    using TaskType = T;
    using ValueType = void;
    using value_type = ValueType;
    BasicPromise() : __s{} {}

    TaskType get_return_object() {
      return TaskType::from_promise(*this);
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

    std::expected<void, TaskError> expected_value() {
      return __s.expected_value();
    }

    template <class awaitable> auto await_transform(awaitable a) const noexcept {
      if constexpr (is_poll_awaitable<awaitable>()) {
        return LoopAwaitable{std::forward<awaitable>(a)};
      } else {
        return a;
      }
    }
  };

  BasicTask<uint64_t> fib(uint64_t beg) {
    if (beg == 0) {
      co_return 1ull;
    } else if (beg == 1) {
      co_return 1ull;
    } else {
      co_return co_await fib(beg - 1).as_awaitable() + co_await fib(beg - 2).as_awaitable();
    }
  }
}
