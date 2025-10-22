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
    ringbuf_queue<void *, coro_state_deleter> __q;

  public:
    event_loop(uint32_t loop_capacity) : __q{loop_capacity} {}

    void push(std::coroutine_handle<void> coro, bool is_owning) noexcept {
      __q.push([&]() { return coro.address(); }, coro_state_deleter{is_owning});
    }

    bool run_one() noexcept {
      auto task = __q.pop();
    }
  };
}
