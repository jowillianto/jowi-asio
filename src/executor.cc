module;
#include <memory>
#include <optional>
#include <thread>
#include <vector>
export module jowi.asio:executor;
import jowi.asio.lockfree;
import :event_loop;

namespace jowi::asio {
  export struct PoolWorker {
  private:
    std::thread __t;
    /*
     * tuple def:
     * - if worker should start run (start sig)
     * - if worker should stop run (stop sig)
     */
    using StateType = BitTuple<bool, bool>;
    std::atomic<StateType> __state;

    void __finalise_state() {
      __state.store({true, true}, asio::memory_order_strict);
    }

  public:
    PoolWorker(std::shared_ptr<EventLoop> loop) :
      __state{false, true}, __t{PoolWorker::thread_work, std::reference_wrapper(__state)} {
      static_cast<void>(EventLoop::register_event_loop(loop, __t.get_id()));
    }

    void start() noexcept {
      StateType e{false, false};
      StateType d{true, false};
      while (__state.compare_exchange_weak(e, d, asio::memory_order_strict)) {
        d = StateType{true, e.get<1>()};
      }
    }

    void stop() noexcept {
      StateType e{true, false};
      StateType d{true, true};
      while (__state.compare_exchange_weak(e, d, asio::memory_order_strict)) {
        d = StateType{e.get<0>(), true};
      }
    }

    ~PoolWorker() {
      if (__t.joinable()) {
        // this ensure that the event loop for running will never be executed
        __finalise_state();
        __t.join();
      }
    }

    static void thread_work(std::atomic<BitTuple<bool, bool>> &s) {
      StateType state = s.load(asio::memory_order_strict);
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
      auto loop = EventLoop::require_event_loop();
      while (state.get<1>()) {
        loop->run_one();
        state = s.load(asio::memory_order_strict);
      }
    }
  };

  export struct PoolExecutor {
  private:
    std::vector<std::unique_ptr<PoolWorker>> __workers;

  public:
    PoolExecutor(
      uint64_t max_size,
      uint32_t loop_size = 4096,
      std::optional<std::shared_ptr<EventLoop>> opt_loop = std::nullopt
    ) {
      auto loop = opt_loop.or_else(
                            []() { return EventLoop::get_event_loop(); }
      ).or_else([&]() {
         return std::optional{std::make_shared<EventLoop>(loop_size)};
       }).value();
      __workers.reserve(max_size);
      for (uint64_t i = 0; i != max_size; i += 1) {
        __workers.emplace_back(std::make_unique<PoolWorker>(loop));
      }
      for (auto &worker : __workers) {
        worker->start();
      }
    }

    ~PoolExecutor() {
      for (auto &worker : __workers) {
        worker->stop();
      }
    }
  };

  /*
   * Execution
   */
  export template <class... tasks> auto parallel_expected(tasks... ts) {
    auto l = EventLoop::get_event_loop()
               .or_else([]() {
                 auto l = std::make_shared<EventLoop>();
                 static_cast<void>(EventLoop::register_event_loop(l));
                 return std::optional{l};
               })
               .value();
    (l->push(ts.raw_coro(), false), ...);
    l->run_forever();
    return std::tuple{ts.expected_value()...};
  }

  struct EmptyResult {
    template <class task> static auto get_result(task &t) {
      if constexpr (std::same_as<TaskResultType<task>, void>) {
        t.value();
        return EmptyResult{};
      } else {
        return t.value();
      }
    }
  };

  export template <class... tasks> auto parallel(tasks... ts) {
    auto l = EventLoop::get_event_loop()
               .or_else([]() {
                 auto l = std::make_shared<EventLoop>();
                 static_cast<void>(EventLoop::register_event_loop(l));
                 return std::optional{l};
               })
               .value();
    (l->push(ts.raw_coro(), false), ...);
    l->run_forever();
    return std::tuple{EmptyResult::get_result(ts)...};
  }
}
