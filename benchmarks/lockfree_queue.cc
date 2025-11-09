#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
import jowi.cli;
import jowi.crogger;
import jowi.asio.lockfree;
import jowi.test_lib;

namespace cli = jowi::cli;
namespace crogger = jowi::crogger;
namespace asio = jowi::asio;
namespace test_lib = jowi::test_lib;

void configure_logger(bool verbose) {
  if (!verbose) {
    crogger::root().set_filter(crogger::SeverityFilter{crogger::FilterOp::gt, 10});
  }
}

bool get_op(std::atomic<uint32_t> &n_op) {
  uint32_t cur_n_op = 1;
  uint32_t desired_n_op = 0;
  while (!n_op.compare_exchange_weak(
    cur_n_op, desired_n_op, std::memory_order_acq_rel, std::memory_order_acquire
  )) {
    if (cur_n_op == 0) {
      return false;
    }
    desired_n_op = cur_n_op - 1;
  }
  return true;
}

void run_lfq(uint32_t t_count, uint32_t n_op) {
  std::atomic_flag beg{false};
  std::atomic<uint32_t> push_count{0};
  std::atomic<uint32_t> pop_count{0};
  std::atomic<uint32_t> op_count{n_op};
  asio::LockFreeQueue<uint32_t> lfq{};
  std::vector<std::thread> ts;
  ts.reserve(t_count);
  auto loop = [&]() mutable {
    while (!beg.test(std::memory_order_acquire)) {
    }
    while (get_op(op_count)) {
      // action to perform
      auto act = test_lib::random_integer(0, 1);
      if (act == 0) {
        lfq.push(test_lib::random_integer(0u, UINT_MAX));
        push_count.fetch_add(1, std::memory_order_relaxed);
      } else {
        if (lfq.pop()) {
          pop_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  };

  crogger::debug(crogger::Msg{"thread_setup_begin"});
  auto start_time = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i != t_count; i += 1) {
    ts.emplace_back(loop);
  }
  auto end_time = std::chrono::steady_clock::now();
  crogger::debug(crogger::Msg{"thread_setup_end"});
  crogger::info(
    crogger::Msg{
      "thread_pool_setup: {:.3f}ms",
      static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count()
      ) /
        1000.
    }
  );

  crogger::debug(crogger::Msg{"thread_run_begin"});
  start_time = std::chrono::steady_clock::now();
  beg.test_and_set(std::memory_order_release);
  for (uint32_t i = 0; i != t_count; i += 1) {
    ts[i].join();
  }
  end_time = std::chrono::steady_clock::now();
  crogger::debug(crogger::Msg{"thread_run_end"});

  crogger::info(
    crogger::Msg{
      "fuzz_time: {:.3f}ms",
      static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count()
      ) /
        1000.
    }
  );
  crogger::info(crogger::Msg{"push: {}", push_count.load(std::memory_order_acquire)});
  crogger::info(crogger::Msg{"pop: {}", pop_count.load(std::memory_order_acquire)});
}

void run_mq(uint32_t t_count, uint32_t n_op) {
  std::atomic_flag beg{false};
  std::atomic<uint32_t> push_count{0};
  std::atomic<uint32_t> pop_count{0};
  std::atomic<uint32_t> op_count{n_op};
  std::queue<uint32_t> q{};
  std::mutex m;
  std::vector<std::thread> ts;
  ts.reserve(t_count);
  auto loop = [&]() mutable {
    while (!beg.test(std::memory_order_acquire)) {
    }
    while (get_op(op_count)) {
      // action to perform
      auto act = test_lib::random_integer(0, 1);
      if (act == 0) {
        std::unique_lock l{m};
        q.push(test_lib::random_integer(0u, UINT_MAX));
        push_count.fetch_add(1, std::memory_order_relaxed);
      } else {
        std::unique_lock l{m};
        if (!q.empty()) {
          q.pop();
          pop_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  };

  crogger::debug(crogger::Msg{"thread_setup_begin"});
  auto start_time = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i != t_count; i += 1) {
    ts.emplace_back(loop);
  }
  auto end_time = std::chrono::steady_clock::now();
  crogger::debug(crogger::Msg{"thread_setup_end"});
  crogger::info(
    crogger::Msg{
      "thread_pool_setup: {:.3f}ms",
      static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count()
      ) /
        1000.
    }
  );

  crogger::debug(crogger::Msg{"thread_run_begin"});
  start_time = std::chrono::steady_clock::now();
  beg.test_and_set(std::memory_order_release);
  for (uint32_t i = 0; i != t_count; i += 1) {
    ts[i].join();
  }
  end_time = std::chrono::steady_clock::now();
  crogger::debug(crogger::Msg{"thread_run_end"});

  crogger::info(
    crogger::Msg{
      "fuzz_time: {:.3f}ms",
      static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count()
      ) /
        1000.
    }
  );
  crogger::info(crogger::Msg{"push: {}", push_count.load(std::memory_order_acquire)});
  crogger::info(crogger::Msg{"pop: {}", pop_count.load(std::memory_order_acquire)});
}

void run_rbf(uint32_t t_count, uint32_t n_op, uint32_t buf_size) {
  std::atomic_flag beg{false};
  std::atomic<uint32_t> push_count{0};
  std::atomic<uint32_t> pop_count{0};
  std::atomic<uint32_t> op_count{n_op};
  asio::RingbufQueue<uint32_t> lfq{buf_size};
  std::vector<std::thread> ts;
  ts.reserve(t_count);
  auto loop = [&]() mutable {
    while (!beg.test(std::memory_order_acquire)) {
    }
    while (get_op(op_count)) {
      // action to perform
      auto act = test_lib::random_integer(0, 1);
      if (act == 0) {
        lfq.push(test_lib::random_integer(0u, UINT_MAX));
        push_count.fetch_add(1, std::memory_order_relaxed);
      } else {
        if (lfq.pop()) {
          pop_count.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
  };

  crogger::debug(crogger::Msg{"thread_setup_begin"});
  auto start_time = std::chrono::steady_clock::now();
  for (uint32_t i = 0; i != t_count; i += 1) {
    ts.emplace_back(loop);
  }
  auto end_time = std::chrono::steady_clock::now();
  crogger::debug(crogger::Msg{"thread_setup_end"});
  crogger::info(
    crogger::Msg{
      "thread_pool_setup: {:.3f}ms",
      static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count()
      ) /
        1000.
    }
  );

  crogger::debug(crogger::Msg{"thread_run_begin"});
  start_time = std::chrono::steady_clock::now();
  beg.test_and_set(std::memory_order_release);
  for (uint32_t i = 0; i != t_count; i += 1) {
    ts[i].join();
  }
  end_time = std::chrono::steady_clock::now();
  crogger::debug(crogger::Msg{"thread_run_end"});

  crogger::info(
    crogger::Msg{
      "fuzz_time: {:.3f}ms",
      static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count()
      ) /
        1000.
    }
  );
  crogger::info(crogger::Msg{"push: {}", push_count.load(std::memory_order_acquire)});
  crogger::info(crogger::Msg{"pop: {}", pop_count.load(std::memory_order_acquire)});
}

int main(int argc, const char **argv) {
  auto app = cli::App{
    cli::AppIdentity{
      .name{"Lockfree Queue Benchmark"},
      .description{"A benchmark for the lockfree queue included in jowi::asio"},
      .author{"Jonathan Willianto"},
      .license{"MIT License (For Now)"},
      .version{0, 0, 0},
    },
    argc,
    argv
  };

  app.add_argument("--tcount")
    .help("The amount of threads to run the benchmark on. Default: 20")
    .require_value()
    .optional();
  app.add_argument("--n_op")
    .help("Amount of loops in each thread. Default: 1,000,000")
    .require_value()
    .optional();
  app.add_argument("--buf_size")
    .help("The buffer size for the ringbuf queue. Default: 4096")
    .require_value()
    .optional();
  app.add_argument("--verbose").help("Print ALL DEBUG MESSAGES").as_flag();
  app.parse_args();

  auto t_count =
    app.expect(app.args().first_of("--tcount").transform(&cli::parse_arg<uint32_t>).value_or(20u));
  auto n_op = app.expect(
    app.args().first_of("--n_op").transform(cli::parse_arg<uint32_t>).value_or(1'000'000u)
  );
  auto buf_size = app.expect(
    app.args().first_of("--buf_size").transform(cli::parse_arg<uint32_t>).value_or(4096)
  );
  auto verbose = app.args().contains("--verbose");
  configure_logger(verbose);

  crogger::info(crogger::Msg{"thread_count: {}", t_count});
  crogger::info(crogger::Msg{"n_op: {}", n_op});
  crogger::info(crogger::Msg{"buf_size: {}", buf_size});

  crogger::info(crogger::Msg{"Ring Buffer Implementation: "});
  run_rbf(t_count, n_op, buf_size);
  crogger::info(crogger::Msg{"Lock Free Implementation: "});
  run_lfq(t_count, n_op);
  crogger::info(crogger::Msg{"Mutex Implementation: "});
  run_mq(t_count, n_op);
}
