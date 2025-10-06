module;
#include <atomic>
#include <concepts>
#include <coroutine>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <thread>
export module jowi.asio:basic_task;
import :event_loop;
import :awaitable;
import :task;

namespace jowi::asio {

  template <class promise_type> struct coroutine_handle_destroyer {
    void operator()(void *addr) {
      if (addr != nullptr) {
        // std::coroutine_handle<promise_type>::from_address(addr).destroy();
      }
    }
  };

  export template <class T, class return_type> struct value_promise;
  export template <class T> struct void_promise;
  export template <class T> struct unique_task;
  export template <class T> struct shared_task;

  template <class F, class... Args>
    requires(std::is_invocable_r_v<bool, F, Args...> && std::copyable<F>)
  unique_task<void> resume_when_done(
    std::shared_ptr<event_loop> l, std::coroutine_handle<void> h, F f, Args &...args
  );

  template <class T> struct unique_task {
    using promise_type = std::conditional_t<
      std::same_as<T, void>,
      void_promise<unique_task<T>>,
      value_promise<unique_task<T>, T>>;
    using handle_type = std::coroutine_handle<promise_type>;

  private:
    std::unique_ptr<void, coroutine_handle_destroyer<promise_type>> __h;
    unique_task(handle_type h) : __h{h.address()} {}

  public:
    handle_type handle() noexcept {
      return handle_type::from_address(__h.get());
    }
    const handle_type handle() const noexcept {
      return handle_type::from_address(__h.get());
    }
    bool is_complete() const noexcept {
      return handle().promise().is_complete();
    }
    void release() noexcept {
      __h.release();
    }

    T value() {
      if constexpr (std::same_as<T, void>) {
        handle().promise().value();
      } else {
        return handle().promise().value();
      }
    }

    std::expected<T, task_error> expected_value() {
      return handle().promise().expected_value();
    }

    static unique_task from_promise(promise_type &p) {
      return handle_type::from_promise(p);
    }

    void resume() {
      handle().resume();
    }

    /*
      awaitable
    */
    bool await_ready() {
      return is_complete();
    }

    std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> h) {
      auto l = event_loop::require_event_loop();
      return resume_when_done(l, h, &unique_task::is_complete, *this).handle();
    }

    T await_resume() {
      return value();
    }
  };

  export template <class T> struct shared_task {

    using promise_type = std::conditional_t<
      std::same_as<T, void>,
      void_promise<unique_task<T>>,
      value_promise<unique_task<T>, T>>;
    using handle_type = std::coroutine_handle<promise_type>;

  private:
    std::shared_ptr<void> __h;
    shared_task(handle_type h) : __h{h.address(), coroutine_handle_destroyer<promise_type>{}} {}

  public:
    shared_task(unique_task<T> t) : shared_task{t.handle()} {}
    shared_task &operator=(unique_task<T> t) {
      __h = std::shared_ptr<void>{t.handle().address(), coroutine_handle_destroyer<promise_type>{}};
      return *this;
    }

    handle_type handle() noexcept {
      return handle_type::from_address(__h.get());
    }
    const handle_type handle() const noexcept {
      return handle_type::from_address(__h.get());
    }
    bool is_complete() {
      return handle().promise().is_complete();
    }
    void release() noexcept {
      __h.reset();
    }

    T value() {
      if constexpr (std::same_as<T, void>) {
        handle().promise().value();
      } else {
        return handle().promise().value();
      }
    }

    std::expected<T, task_error> expected_value() {
      return handle().promise().expected_value();
    }

    bool is_complete() const noexcept {
      return handle().promise().is_complete();
    }

    void resume() const noexcept {
      return handle().resume();
    }

    static shared_task from_promise(promise_type &p) {
      return handle_type::from_promise(p);
    }

    /*
      awaitable
    */
    bool await_ready() {
      return is_complete();
    }

    void await_suspend(std::coroutine_handle<void> h) {
      event_loop::require_event_loop()->push_back(h);
    }

    T await_resume() {
      return value();
    }
  };

  template <class F, class... Args>
    requires(std::is_invocable_r_v<bool, F, Args...> && std::copyable<F>)
  unique_task<void> resume_when_done(
    std::shared_ptr<event_loop> l, std::coroutine_handle<void> h, F f, Args &...args
  ) {
    if (std::invoke(f, args...)) {
      h.resume();
    } else {
      l->push_back(resume_when_done(l, h, f, args...).handle());
    }
    co_return;
  }

  template <poll_awaitable T>
    requires(is_poll_awaitable<T>())
  struct loop_awaitable {
  private:
    T __a;

  public:
    loop_awaitable(T a) : __a{std::forward<T>(a)} {}

    bool await_ready() {
      return __a.await_ready();
    }

    std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> h) {
      auto loop = event_loop::require_event_loop();
      return resume_when_done(loop, h, &loop_awaitable::await_ready, *this).handle();
    }

    await_result_type<T> await_resume() {
      return __a.await_resume();
    }
  };
  export template <class T, class return_type> struct value_promise {
  private:
    std::atomic_flag __f;
    std::optional<std::expected<return_type, task_error>> __v;

  public:
    using value_type = return_type;
    using task_type = T;
    task_type get_return_object() {
      return T::from_promise(*this);
    }

    std::suspend_always initial_suspend() const noexcept {
      return std::suspend_always{};
    }

    std::suspend_always final_suspend() const noexcept {
      return std::suspend_always{};
    }

    template <class... Args>
      requires(std::constructible_from<return_type, Args...>)
    void return_value(Args &&...args) {
      __v.emplace(std::forward<Args>(args)...);
      __f.test_and_set(std::memory_order_acq_rel);
    }

    void unhandled_exception() {
      __v.emplace(std::unexpected{task_error{std::current_exception()}});
      __f.test_and_set(std::memory_order_acq_rel);
    }

    return_type value() {
      if (__v->has_value()) {
        return std::move(__v)->value();
      } else {
        std::move(__v)->error().rethrow();
        throw; // unreachable code.
      }
    }

    std::expected<return_type, task_error> expected_value() {
      return std::move(__v).value();
    }
    bool is_complete() const noexcept {
      return __f.test(std::memory_order_acquire);
    }

    auto await_transform(awaitable auto a) {
      if constexpr (is_poll_awaitable<decltype(a)>()) {
        return loop_awaitable{std::move(a)};
      } else {
        return a;
      }
    }
  };

  export template <class T> struct void_promise {
  private:
    std::atomic_flag __f;
    std::optional<std::expected<void, task_error>> __v;

  public:
    using task_type = T;
    using value_type = void;
    T get_return_object() {
      return T::from_promise(*this);
    }

    std::suspend_always initial_suspend() const noexcept {
      return std::suspend_always{};
    }

    std::suspend_always final_suspend() const noexcept {
      return std::suspend_always{};
    }

    void return_void() {
      __f.test_and_set(std::memory_order_acq_rel);
    }

    void unhandled_exception() {
      __v.emplace(std::unexpected{task_error{std::current_exception()}});
      __f.test_and_set(std::memory_order_acq_rel);
    }

    void value() {
      if (!__v->has_value()) {
        return std::move(__v)->error().rethrow();
      }
    }
    std::expected<void, task_error> expected_value() {
      return std::move(__v).value();
    }
    bool is_complete() const noexcept {
      return __f.test(std::memory_order_acquire);
    }
    auto await_transform(awaitable auto a) {
      if constexpr (is_poll_awaitable<decltype(a)>()) {
        return loop_awaitable{std::move(a)};
      } else {
        return a;
      }
    }
  };

  /**
    These abstract away the execution loop process.
  */
  export template <task T> task_expected_type<T> run_expected(T t) {
    std::shared_ptr<event_loop> loop =
      event_loop::get_or_create_event_loop(std::this_thread::get_id());
    loop->push_back(t.handle());
    auto task = loop->pop_front();
    // loop->run_until(&T::done, t);
    loop->run();
    event_loop::unregister_event_loop();
    return t.expected_value();
  }

  export template <task... Ts> std::tuple<task_expected_type<Ts>...> gather_expected(Ts... ts) {
    auto loop = event_loop::get_or_create_event_loop(std::this_thread::get_id());
    (loop->push_back(ts.handle()), ...);
    // loop->run_until([&]() { return (ts.done() && ...); });
    loop->run();
    event_loop::unregister_event_loop();
    return std::tuple{ts.expected_value()...};
  }

  export template <task T> task_result_type<T> run(T t) {
    auto loop = event_loop::get_or_create_event_loop(std::this_thread::get_id());
    loop->push_back(t);
    // loop->run_until(&T::done, t);
    loop->run();
    event_loop::unregister_event_loop();
    return t.value();
  }

  struct void_result {};

  template <task task_type> auto create_run_result(task_type &t) {
    if constexpr (std::same_as<task_result_type<task_type>, void>) {
      return void_result{};
    } else {
      return t.value();
    }
  }

  export template <task... Ts>
  std::tuple<std::invoke_result_t<decltype(create_run_result<Ts>), Ts &>...> gather(Ts... ts) {
    auto loop = event_loop::get_or_create_event_loop(std::this_thread::get_id());
    (loop->push_back(ts), ...);
    // loop->run_until([&]() { return (ts.done() && ...); });
    loop->run();
    event_loop::unregister_event_loop();
    return std::tuple{create_run_result(ts)...};
  }
}