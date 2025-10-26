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
  asio::ringbuf_queue<uint32_t> rbq{2048};
  uint32_t num = test_lib::random_integer(0u, UINT_MAX);
  rbq.push(num);
  test_lib::assert_equal(*rbq.pop().value(), num);
}

JOWI_ADD_TEST(test_no_push_full) {
  asio::ringbuf_queue<uint32_t> rbq{1};
  uint32_t num = test_lib::random_integer(0u, UINT_MAX);
  rbq.push(num);
  test_lib::assert_true(rbq.try_push(10).has_value());
  test_lib::assert_equal(*rbq.pop().value(), num);
}

JOWI_ADD_TEST(test_no_pop_empty) {
  asio::ringbuf_queue<uint32_t> rbq{1};
  test_lib::assert_false(rbq.pop().has_value());
}

JOWI_ADD_TEST(test_no_pop_empty_after_push) {
  asio::ringbuf_queue<uint32_t> rbq{1};
  rbq.push(10);
  test_lib::assert_true(rbq.pop().has_value());
  test_lib::assert_false(rbq.pop().has_value());
}

JOWI_ADD_TEST(test_single_thread_fuzz) {
  std::queue<uint32_t> q{};
  asio::ringbuf_queue<uint32_t> rbq{100'000u};
  uint32_t l_count = test_lib::random_integer(10'000u, 99'000u);
  for (uint32_t i = 0; i != l_count; i += 1) {
    uint32_t num = test_lib::random_integer(0u, UINT_MAX);
    q.push(num);
    rbq.push(num);
  }
  for (uint32_t i = 0; i != l_count; i += 1) {
    test_lib::assert_equal(q.front(), *rbq.pop().value());
    q.pop();
  }
}

JOWI_ADD_TEST(test_multithread_fuzz) {
  uint32_t l_count = test_lib::random_integer(10'000u, 99'000u);
  uint32_t t_count = test_lib::random_integer(20u, 30u);
  std::atomic_flag beg{false};
  std::atomic<uint32_t> push_count{0};
  std::atomic<uint32_t> pop_count{0};
  asio::ringbuf_queue<uint32_t> rbq{50'000u};
  std::vector<std::thread> ts;
  ts.reserve(t_count);
  auto loop = [&, l_count]() mutable {
    while (!beg.test(std::memory_order_acquire)) {
    }
    while (l_count--) {
      // action to perform
      auto act = test_lib::random_integer(0, 1);
      if (act == 0) {
        rbq.push(test_lib::random_integer(0u, UINT_MAX));
        push_count.fetch_add(1, std::memory_order_relaxed);
      } else {
        rbq.pop();
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
