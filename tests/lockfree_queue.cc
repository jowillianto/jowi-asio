#include <jowi/test_lib.hpp>
#include <atomic>
#include <climits>
#include <cstdint>
#include <queue>
#include <thread>
#include <vector>
import jowi.test_lib;
import jowi.asio.lockfree;

namespace test_lib = jowi::test_lib;
namespace asio = jowi::asio;

JOWI_SETUP(argc, argv) {
  test_lib::get_test_context().set_time_unit(test_lib::test_time_unit::MILLI_SECONDS);
}

JOWI_ADD_TEST(test_single_thread_simple) {
  asio::lockfree_queue<uint32_t> lfq{};
  uint32_t num = test_lib::random_integer(0u, UINT_MAX);
  lfq.push(num);
  test_lib::assert_equal(lfq.size(), 1);
  test_lib::assert_equal(lfq.pop().value(), num);
}

JOWI_ADD_TEST(test_single_thread_fuzz) {
  std::queue<uint32_t> q{};
  asio::lockfree_queue<uint32_t> lfq{};
  uint32_t l_count = test_lib::random_integer(10'000u, 99'000u);
  for (uint32_t i = 0; i != l_count; i += 1) {
    uint32_t num = test_lib::random_integer(0u, UINT_MAX);
    q.push(num);
    lfq.push(num);
  }
  for (uint32_t i = 0; i != l_count; i += 1) {
    test_lib::assert_equal(q.front(), lfq.pop().value());
    q.pop();
  }
}

JOWI_ADD_TEST(test_multithread_fuzz) {
  uint32_t l_count = test_lib::random_integer(10'000u, 99'000u);
  uint32_t t_count = test_lib::random_integer(20u, 30u);
  std::atomic_flag beg{false};
  std::atomic<uint32_t> push_count{0};
  std::atomic<uint32_t> pop_count{0};
  asio::lockfree_queue<uint32_t> lfq{};
  std::vector<std::thread> ts;
  ts.reserve(t_count);
  auto loop = [&, l_count]() mutable {
    while (!beg.test(std::memory_order_acquire)) {
    }
    while (l_count--) {
      // action to perform
      auto act = test_lib::random_integer(0, 1);
      if (act == 0) {
        lfq.push(test_lib::random_integer(0u, UINT_MAX));
        push_count.fetch_add(1, std::memory_order_relaxed);
      } else {
        lfq.pop();
      }
    }
  };

  for (uint32_t i = 0; i != t_count; i += 1) {
    ts.emplace_back(loop);
  }

  beg.test_and_set(std::memory_order_release);

  for (uint32_t i = 0; i != t_count; i += 1) {
    ts[i].join();
  }
}
