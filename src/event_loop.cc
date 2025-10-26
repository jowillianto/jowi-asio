module;
#include <coroutine>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
export module jowi.asio:event_loop;
import jowi.asio.lockfree;
import :awaitable;
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

    static std::unordered_map<thread_id, std::shared_ptr<event_loop>> __local_loop;

  public:
    event_loop(uint32_t loop_capacity = 4096) : __q{loop_capacity} {}

    void push(task auto t) {
      std::coroutine_handle<void> coro = t.raw_coro();
      __q.push(std::unique_ptr<void, coro_state_deleter>{coro.address(), coro_state_deleter{true}});
    }
    void push(task auto &t) {  
      std::coroutine_handle<void> coro = t.raw_coro();
      __q.push(
        std::unique_ptr<void, coro_state_deleter>{coro.address(), coro_state_deleter{false}}
      );
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
      std::coroutine_handle<void>::from_address(state->get()).resume();
      return true;
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
  };

  std::unordered_map<thread_id, std::shared_ptr<event_loop>> event_loop::__local_loop{};
}
