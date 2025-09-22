module;
#include <atomic>
#include <concepts>
#include <coroutine>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
export module jowi.asio:task;
import :awaitable;
import :lf_deque;

namespace jowi::asio {
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
  };

  template <class task_type>
  concept task = requires(const std::decay_t<task_type> ct, std::decay_t<task_type> t) {
    promise<typename task_type::promise_type>;
    { ct.done() } -> std::same_as<bool>;
    { t.address() } -> std::same_as<void *>;
    {
      task_type::from_promise(std::declval<typename task_type::promise_type &>())
    } -> std::same_as<task_type>;
    { task_type::from_address(std::declval<void *>()) } -> std::same_as<task_type>;
  };

  template <task task_type>
  using task_result_type = std::invoke_result_t<decltype(&task_type::value), task_type *>;
  template <task task_type>
  using task_expected_type =
    std::invoke_result_t<decltype(&task_type::expected_value), task_type *>;

  using thread_id = decltype(std::this_thread::get_id());

  export struct event_loop {
  private:
    lf_deque<void *> __d;
    static std::unordered_map<thread_id, std::shared_ptr<event_loop>> __s;

  public:
    event_loop() : __d{} {}

    void schedule_task(void *addr, bool eager = false) {
      if (eager) {
        __d.push_back(addr);
      } else {
        __d.push_front(addr);
      }
    }

    uint64_t size() const noexcept {
      return __d.size();
    }

    std::optional<std::coroutine_handle<void>> pop() {
      return __d.pop().transform(std::coroutine_handle<void>::from_address);
    }

    bool run_one() {
      auto task = pop();
      if (task) {
        task->resume();
        return true;
      }
      return false;
    }

    template <class F, class... Args>
      requires(std::is_invocable_r_v<bool, F, Args...>)
    void run_while(F f, Args &...args) {
      auto task = pop();
      while (std::invoke(f, std::forward<Args &>(args)...)) {
        if (task) {
          task->resume();
        }
        task = pop();
      }
    }

    template <class F, class... Args>
      requires(std::is_invocable_r_v<bool, F, Args...>)
    void run_until(F f, Args &...args) {
      auto task = pop();
      while (!std::invoke(f, std::forward<Args &>(args)...)) {
        if (task) {
          task->resume();
        }
        task = pop();
      }
    }

    /*
      Global Functions
    */
    static std::shared_ptr<event_loop> new_event_loop() {
      return std::make_shared<event_loop>();
    }

    static std::optional<std::shared_ptr<event_loop>> get_event_loop() {
      auto it = __s.find(std::this_thread::get_id());
      if (it == __s.end()) {
        return std::nullopt;
      }
      return it->second;
    }

    static std::shared_ptr<event_loop> get_or_create_event_loop() {
      return get_event_loop()
        .or_else([]() {
          auto loop = new_event_loop();
          register_event_loop(loop);
          return std::optional{loop};
        })
        .value();
    }

    /*
      Returns true if the event loop is registered and false if already exists.
      Event loops might need to be registered when running in a multi threaded environment that
      submits tasks to the same task queue. This way, a fair queue can be created for everyone.
    */
    static bool register_event_loop(
      std::shared_ptr<event_loop> loop, thread_id tid = std::this_thread::get_id()
    ) {
      auto it = __s.find(std::this_thread::get_id());
      if (it != __s.end()) {
        return false;
      }
      __s.emplace(tid, loop);
      return true;
    }

    /*
      Returns true if the event loop is removed. False if not found.
    */
    static bool remove_event_loop(thread_id tid = std::this_thread::get_id()) {
      auto it = __s.find(std::this_thread::get_id());
      if (it == __s.end()) {
        return false;
      }
      __s.erase(tid);
      return true;
    }
  };

  std::unordered_map<thread_id, std::shared_ptr<event_loop>> event_loop::__s{};

  export template <class T> struct basic_promise;
  export template <class T> struct basic_task : public std::coroutine_handle<basic_promise<T>> {
    using promise_type = basic_promise<T>;
    using coro_type = std::coroutine_handle<promise_type>;

    basic_task(coro_type coro) : coro_type{std::forward<coro_type>(coro)} {}

    bool is_complete() const {
      return coro_type::promise().is_complete();
    }

    T value() {
      return coro_type::promise().value();
    }
    std::expected<T, std::exception_ptr()> expected_value() {
      return coro_type::promise().expected_value();
    }

    using coro_type::resume;
    using coro_type::address;

    static basic_task from_promise(promise_type &p) {
      return basic_task{coro_type::from_promise(p)};
    }
    static basic_task from_address(void *addr) {
      return basic_task{coro_type::from_address(addr)};
    }
  };

  template <awaitable T> struct loop_awaitable {
  private:
    T __a;

  public:
    loop_awaitable(T a) : __a{std::forward<T>(a)} {}

    bool await_ready() {
      return __a.await_ready();
    }

    std::conditional_t<
      is_poll_awaitable<T>(),
      std::coroutine_handle<void>,
      std::invoke_result_t<decltype(&T::await_suspend), T *, std::coroutine_handle<void>>>
    await_suspend(std::coroutine_handle<void> h) {
      if constexpr (is_poll_awaitable<T>()) {
        event_loop::get_event_loop().value()->run_until(&loop_awaitable::await_ready, *this);
        return h;
      } else {
        return __a.await_suspend(h);
      }
    }

    await_result_type<T> await_resume() {
      return __a.await_resume();
    }
  };

  template <> struct basic_promise<void> {
  private:
    std::atomic_flag __is_complete;
    std::expected<void, std::exception_ptr> __res;

  public:
    using task_type = basic_task<void>;
    using value_type = void;
    basic_promise() : __is_complete(false) {}

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
      return __is_complete.test(m);
    }

    void return_void() {
      __is_complete.test_and_set(std::memory_order_release);
    }

    void unhandled_exception() {
      __res = std::unexpected{std::current_exception()};
      __is_complete.test_and_set(std::memory_order_release);
    }

    void value() {
      if (!__res) {
        std::rethrow_exception(__res.error());
      }
    }

    std::expected<void, std::exception_ptr> expected_value() {
      return __res;
    }

    template <awaitable awaitable_type>
    loop_awaitable<std::decay_t<awaitable_type>> await_transform(awaitable_type v) {
      return loop_awaitable{std::forward<awaitable_type>(v)};
    }
  };

  template <class T>
    requires(!std::same_as<T, void>)
  struct basic_promise<T> {
  private:
    std::atomic_flag __is_complete;
    std::expected<std::optional<T>, std::exception_ptr> __value;

  public:
    using task_type = basic_task<T>;
    using value_type = T;
    basic_promise() : __is_complete(false), __value{std::nullopt} {}

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
      return __is_complete.test(m);
    }

    template <class... Args>
      requires(std::constructible_from<T, Args...>)
    void return_value(Args &&...args) {
      __value->emplace(std::forward<Args>(args)...);
      __is_complete.test_and_set(std::memory_order_release);
    }

    void unhandled_exception() {
      __value = std::unexpected{std::current_exception()};
      __is_complete.test_and_set(std::memory_order_release);
    }

    T value() {
      return std::move(__value)->value();
    }

    std::expected<T, std::exception_ptr> expected_value() {
      return std::move(__value).transform(&std::optional<T>::value);
    }

    template <awaitable awaitable_type>
    loop_awaitable<std::decay_t<awaitable_type>> await_transform(awaitable_type v) {
      return loop_awaitable{std::forward<awaitable_type>(v)};
    }
  };

  /**
    These abstract away the execution loop process.
  */
  export template <task T> task_expected_type<T> run_expected(T t) {
    auto loop = event_loop::get_or_create_event_loop();
    loop->schedule_task(t.address(), false);
    loop->run_until(&T::is_complete, t);
    return t.expected_value();
  }

  export template <task... Ts> std::tuple<task_expected_type<Ts>...> gather_expected(Ts... ts) {
    auto loop = event_loop::get_or_create_event_loop();
    (loop->schedule_task(ts.address(), false), ...);
    loop->run_until([&]() { return (ts.is_complete() && ...); });
    return std::tuple{ts.expected_value()...};
  }

  export template <task T> task_result_type<T> run(T t) {
    auto loop = event_loop::get_or_create_event_loop();
    loop->schedule_task(t.address(), false);
    loop->run_until(&T::is_complete, t);
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
    auto loop = event_loop::get_or_create_event_loop();
    (loop->schedule_task(ts.address(), true), ...);
    loop->run_until([&]() { return (ts.is_complete() && ...); });
    return std::tuple{create_run_result(ts)...};
  }
}