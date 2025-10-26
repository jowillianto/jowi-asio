module;
#include <memory>
#include <optional>
#include <thread>
#include <vector>
export module jowi.asio:executor;
import jowi.asio.lockfree;
import :event_loop;

namespace jowi::asio {
  export struct pool_worker {
  private:
    std::thread __t;
    /*
     * tuple def:
     * - if worker should start run (start sig)
     * - if worker should stop run (stop sig)
     */
    using state_type = bit_tuple<bool, bool>;
    std::atomic<state_type> __state;

    void __finalise_state() {
      __state.store({true, true}, asio::memory_order_strict);
    }

  public:
    pool_worker(std::shared_ptr<event_loop> loop) :
      __state{false, true}, __t{pool_worker::thread_work, std::reference_wrapper(__state)} {
      static_cast<void>(event_loop::register_event_loop(loop, __t.get_id()));
    }

    void start() noexcept {
      state_type e{false, false};
      state_type d{true, false};
      while (__state.compare_exchange_weak(e, d, asio::memory_order_strict)) {
        d = state_type{true, e.get<1>()};
      }
    }

    void stop() noexcept {
      state_type e{true, false};
      state_type d{true, true};
      while (__state.compare_exchange_weak(e, d, asio::memory_order_strict)) {
        d = state_type{e.get<0>(), true};
      }
    }
    ~pool_worker() {
      if (__t.joinable()) {
        // this ensure that the event loop for running will never be executed
        __finalise_state();
        __t.join();
      }
    }

    static void thread_work(std::atomic<bit_tuple<bool, bool>> &s) {
      state_type state = s.load(asio::memory_order_strict);
      /*
       * This protects against two cases:
       * - thread has been signaled to start
       * - thread signaled to stop directly.
       */
      while (!state.get<0>() && state.get<1>()) {
        state = s.load(asio::memory_order_strict);
      }
      /*
       * we assume that event loop must have been created after start.
       */
      auto loop = event_loop::require_event_loop();
      while (state.get<1>()) {
        loop->run_one();
        state = s.load(asio::memory_order_strict);
      }
    }
  };

  export struct pool_executor {
  private:
    std::vector<std::unique_ptr<pool_worker>> __workers;

  public:
    pool_executor(
      uint64_t max_size,
      uint32_t loop_size = 4096,
      std::optional<std::shared_ptr<event_loop>> opt_loop = std::nullopt
    ) {
      auto loop = opt_loop.or_else(
                            []() { return event_loop::get_event_loop(); }
      ).or_else([&]() {
         return std::optional{std::make_shared<event_loop>(loop_size)};
       }).value();
      __workers.reserve(max_size);
      for (uint64_t i = 0; i != max_size; i += 1) {
        __workers.emplace_back(std::make_unique<pool_worker>(loop));
      }
      for (auto &worker : __workers) {
        worker->start();
      }
    }
  };

  /*
   * Execution
   */
  export template <class... tasks> auto gather_expected(tasks... ts) {
    auto l = event_loop::get_event_loop()
               .or_else([]() {
                 auto l = std::make_shared<event_loop>();
                 static_cast<void>(event_loop::register_event_loop(l));
                 return std::optional{l};
               })
               .value();
    (l->push(ts.raw_coro(), false), ...);
    l->run_forever();
    return std::tuple{ts.expected_value()...};
  }
}
