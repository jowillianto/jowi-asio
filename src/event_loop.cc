module;
#include <chrono>
#include <coroutine>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
export module jowi.asio:event_loop;
import jowi.asio.lockfree;
import :awaitable;
import :awaitables;
import :task;

namespace jowi::asio {
  using thread_id = decltype(std::this_thread::get_id());

  struct coro_state_deleter {
    bool is_owning;

    void operator()(void *state) {
      if (!is_owning || state == nullptr) return;
      std::coroutine_handle<void>::from_address(state).destroy();
    }
  };

  struct event_loop {
  private:
    ringbuf_queue<void, coro_state_deleter> __q;
    std::chrono::nanoseconds __time_slice;

    static std::unordered_map<thread_id, std::shared_ptr<event_loop>> __local_loop;

    struct async_push {
    private:
      std::reference_wrapper<event_loop> __l;
      std::optional<std::unique_ptr<void, coro_state_deleter>> __ptr;

    public:
      async_push(event_loop &l, std::unique_ptr<void, coro_state_deleter> ptr) :
        __l{l}, __ptr{std::move(ptr)} {}

      static constexpr bool is_defer_awaitable = true;
      bool await_ready() {
        __ptr = __l.get().__q.try_push(std::move(__ptr).value());
        return !__ptr.has_value();
      }

      /*
       * blocking push.
       */
      std::coroutine_handle<void> await_suspend(std::coroutine_handle<void> h) {
        __l.get().__q.push(std::move(__ptr).value());
        return h;
      }

      void await_resume() {}
    };

  public:
    event_loop(
      uint32_t loop_capacity = 4096,
      std::chrono::nanoseconds loop_time_slice = std::chrono::nanoseconds{100}
    ) : __q{loop_capacity}, __time_slice{loop_time_slice} {}

    void push(std::coroutine_handle<void> h, bool is_owning = true) {
      __q.push(
        std::unique_ptr<void, coro_state_deleter>{h.address(), coro_state_deleter{is_owning}}
      );
    }

    async_push apush(std::coroutine_handle<void> h, bool is_owning = true) {
      return async_push{
        *this, std::unique_ptr<void, coro_state_deleter>{h.address(), coro_state_deleter{is_owning}}
      };
    }

    void run_until_complete(task auto t) {
      push(t);
      bool should_run = true;
      while (should_run && !t.is_complete()) {
        should_run = run_one();
      }
    }

    void run_forever() {
      bool should_run = true;
      while (should_run) {
        should_run = run_one();
      }
    }

    /*
     * runs a task and returns wether a task is run.
     */
    bool run_one() {
      auto state = __q.pop();
      if (!state) {
        return false;
      }
      auto coro = std::coroutine_handle<void>::from_address(state->get());
      coro.resume();
      return true;
    }

    /*
     * sleep for time slice period of time
     */
    void loop_sleep(
      std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now()
    ) const noexcept {
      std::this_thread::sleep_until(start_time + __time_slice);
    }
    sleep_awaitable aloop_sleep(
      std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now()
    ) const noexcept {
      return sleep_until(start_time + __time_slice);
    }

    /*
     * Singleton Functions
     */
    [[nodiscard("register success status")]] static bool register_event_loop(
      std::shared_ptr<event_loop> l, thread_id id = std::this_thread::get_id()
    ) {
      auto it = __local_loop.find(id);
      if (it == __local_loop.end()) {
        __local_loop.emplace(id, l);
        return true;
      }
      return false;
    }

    [[nodiscard("remove success status")]] static bool remove_event_loop(
      thread_id id = std::this_thread::get_id()
    ) {
      auto it = __local_loop.find(id);
      if (it == __local_loop.end()) {
        return false;
      }
      __local_loop.erase(it);
      return true;
    }

    static std::optional<std::shared_ptr<event_loop>> get_event_loop(
      thread_id id = std::this_thread::get_id()
    ) {
      auto it = __local_loop.find(id);
      if (it == __local_loop.end()) {
        return std::nullopt;
      }
      return it->second;
    }
    static std::shared_ptr<event_loop> require_event_loop(
      thread_id id = std::this_thread::get_id()
    ) {
      return get_event_loop(id).value();
    }
  };

  std::unordered_map<thread_id, std::shared_ptr<event_loop>> event_loop::__local_loop{};
}
