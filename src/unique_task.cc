module;
#include <atomic>
#include <coroutine>
#include <memory>
export module jowi.asio:unique_task;
import :task;
import :basic_promise;

namespace jowi::asio {

  struct coro_state_deleter {
    void operator()(void *addr) {
      if (addr != nullptr) {
        std::coroutine_handle<void>::from_address(addr).destroy();
      }
    }
  };

  template <class value_type> struct unique_task {
  public:
    using promise_type = basic_promise<unique_task, value_type>;
    using coro_type = std::coroutine_handle<promise_type>;

  private:
    std::unique_ptr<void, coro_state_deleter> __addr;

    inline coro_type __coro() const noexcept {
      return coro_type::from_address(__addr.get());
    }

  public:
    unique_task(std::coroutine_handle<promise_type> h) :
      __addr{h.address(), coro_state_deleter{}} {}

    void resume() {
      __coro().resume();
    }

    bool is_complete(std::memory_order m = std::memory_order_relaxed) {
      return __coro().promise().is_complete();
    }

    auto value() {
      return __coro().promise().value();
    }
    auto expected_value() {
      return __coro().promise().expected_value();
    }

    static unique_task from_promise(promise_type &p) {
      return unique_task{coro_type::from_promise(p)};
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

    bool is_complete(std::memory_order m = std::memory_order_relaxed) {
      return __coro().promise().is_complete();
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

    static shared_task from_promise(promise_type &p) {
      return shared_task{coro_type::from_promise(p)};
    }
  };
}
