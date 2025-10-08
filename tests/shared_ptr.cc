#include <jowi/test_lib.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>
#include <vector>
import jowi.test_lib;
import jowi.asio.lockfree;

namespace test_lib = jowi::test_lib;
namespace asio = jowi::asio;

JOWI_SETUP(argc, argv) {
  test_lib::get_test_context().set_time_unit(test_lib::test_time_unit::MILLI_SECONDS);
}

void increment_uint(uint32_t *d) {
  *d += 1;
}

JOWI_ADD_TEST(test_shared_ptr_drop) {
  uint32_t drop_count = 0;
  auto ptr = asio::shared_ptr<uint32_t>{&drop_count, increment_uint};
  ptr.reset();
  test_lib::assert_equal(drop_count, 1);
}

JOWI_ADD_TEST(test_shared_ptr_boolean) {
  auto ptr = asio::shared_ptr<uint32_t>{nullptr};
  test_lib::assert_false(ptr);
  uint32_t drop_count = 0;
  ptr = asio::shared_ptr<uint32_t>{&drop_count, increment_uint};
  test_lib::assert_true(ptr);
}

JOWI_ADD_TEST(test_shared_ptr_move) {
  uint32_t drop_count = 0;
  auto ptr = asio::shared_ptr<uint32_t>{&drop_count, increment_uint};
  auto ptr2 = std::move(ptr);
  test_lib::assert_false(ptr);
}

JOWI_ADD_TEST(test_shared_ptr_living_copy_no_drop) {
  uint32_t drop_count = 0;
  auto ptr = asio::shared_ptr<uint32_t>{&drop_count, increment_uint};
  auto ptr2 = ptr;
  ptr.reset();
  test_lib::assert_equal(drop_count, 0);
}

JOWI_ADD_TEST(test_shared_ptr_thread_drop) {
  uint32_t drop_count = 0;
  auto ptr = asio::shared_ptr<uint32_t>{&drop_count, increment_uint};
  auto fut = std::async(std::launch::deferred, [](decltype(ptr) ptr) { ptr.reset(); }, ptr);
  ptr.reset();
  fut.get();
  test_lib::assert_equal(drop_count, 1);
}

JOWI_ADD_TEST(test_shared_ptr_drop_fuzz) {
  uint32_t drop_count = 0;
  auto ptr = asio::shared_ptr<uint32_t>{&drop_count, increment_uint};
  auto start = std::chrono::steady_clock::now();
  auto run_after_delay = [](decltype(start) s, decltype(ptr) ptr) {
    std::this_thread::sleep_until(s + std::chrono::milliseconds{test_lib::random_integer(0, 250)});
  };
  auto tcount = test_lib::random_integer(10, 50);
  std::vector<std::thread> ts;
  ts.reserve(tcount);
  for (uint32_t i = 0; i != tcount; i += 1) {
    ts.emplace_back(std::thread{run_after_delay, start, ptr});
  }
  // release current copy.
  ptr.reset();
  for (auto &t : ts) {
    t.join();
  }
  test_lib::assert_equal(drop_count, 1);
}

JOWI_ADD_TEST(test_atomic_shared_ptr_unithread) {
  uint32_t drop_count[2] = {0, 0};
  auto ptr = asio::shared_ptr<uint32_t>{&drop_count[0], increment_uint};
  std::atomic a_ptr{ptr};
  auto ptr2 = asio::shared_ptr<uint32_t>{&drop_count[1], increment_uint};
  std::atomic a_ptr2{ptr2};
  // store 2 -> 1. We will now drop one fully.
  a_ptr.store(ptr2);
  test_lib::assert_equal(a_ptr.load(), ptr2);
  // drop one fully.
  ptr.reset();
  test_lib::assert_equal(drop_count[0], 1);
  // now we drop everything fully.
  a_ptr.store(nullptr);
  a_ptr2.store(nullptr);
  ptr2.reset();
  test_lib::assert_equal(drop_count[1], 1);
}

JOWI_ADD_TEST(test_atomic_shared_ptr_hold_copy) {
  uint32_t drop_count = 0;
  auto ptr = asio::shared_ptr<uint32_t>{&drop_count, increment_uint};
  std::atomic a_ptr{ptr};
  ptr.reset();
  test_lib::assert_equal(drop_count, 0);
  // now drop atomic
  a_ptr.store(nullptr);
  test_lib::assert_equal(drop_count, 1);
}

JOWI_ADD_TEST(test_shared_ptr_compare_exchange) {
  auto drop_count = std::pair{0u, 0u};
  auto ptr = std::pair{
    asio::shared_ptr{&drop_count.first, increment_uint},
    asio::shared_ptr{&drop_count.second, increment_uint}
  };
  std::pair<std::atomic<decltype(ptr.first)>, std::atomic<decltype(ptr.second)>> a_ptr{
    ptr.first, ptr.second
  };

  asio::shared_ptr<uint32_t> dummy{nullptr};
  // dummy is null
  test_lib::assert_false(a_ptr.first.compare_exchange_weak(dummy, nullptr));
  // dummy contains ptr.first
  test_lib::assert_equal(dummy, ptr.first);
  // dummy is ptr.first
  test_lib::assert_false(a_ptr.second.compare_exchange_weak(dummy, nullptr));
  // dummy contains ptr.second
  test_lib::assert_equal(dummy, ptr.second);
  // this should succeed, content of dummy is undisturbed.
  test_lib::assert_true(a_ptr.second.compare_exchange_weak(dummy, nullptr));
  // dummy contains ptr.second
  test_lib::assert_equal(dummy, ptr.second);
  // check that now second atomic pointer contains ptr.second.
  test_lib::assert_equal(a_ptr.second.load(), asio::shared_ptr<uint32_t>{nullptr});
  // we drop ptr.second and now drop_count.second should be one.
  ptr.second.reset();
  // oh damn dummy is ptr.2, how silly, let's reset it.
  dummy.reset();
  // now check
  test_lib::assert_equal(drop_count.second, 1);
  // now we do the same for the first one
  test_lib::assert_false(a_ptr.first.compare_exchange_weak(dummy, nullptr));
  // now contains ptr.first
  test_lib::assert_equal(dummy, ptr.first);
  // now we store the dummy should succeed and dummy should be untouched.
  test_lib::assert_true(a_ptr.first.compare_exchange_weak(dummy, nullptr));
  // check dummy
  test_lib::assert_equal(dummy, ptr.first);
  // drop dummy and ptr.first
  dummy.reset();
  ptr.first.reset();
  // check
  test_lib::assert_equal(drop_count.first, 1);
}

JOWI_ADD_TEST(test_shared_ptr_fuzz) {
  // thread count
  auto t_count = test_lib::random_integer(20u, 50u);
  // variety count
  auto v_count = test_lib::random_integer(100u, 200u);
  // loop count
  auto l_count = test_lib::random_integer(1000u, 2000u);
  std::atomic_flag beg{false};
  std::vector<std::thread> ts;
  std::vector<uint32_t> is_dropped(v_count, 0u);
  std::vector<std::unique_ptr<std::atomic<asio::shared_ptr<uint32_t>>>> aptr;
  aptr.reserve(v_count);
  ts.reserve(t_count);
  for (uint32_t i = 0; i != v_count; i += 1) {
    is_dropped.emplace_back(0u);
    aptr.emplace_back(
      std::make_unique<std::atomic<asio::shared_ptr<uint32_t>>>(&is_dropped[i], increment_uint)
    );
  }
  auto loop = [&, l_count]() mutable {
    while (!beg.test(std::memory_order_acquire)) {
    }
    while (l_count--) {
      // pick target to replace.
      auto s_id = test_lib::random_integer(0u, v_count - 1);
      auto t_id = test_lib::random_integer(0u, v_count - 1);
      while (s_id == t_id) {
        t_id = test_lib::random_integer(0u, v_count - 1);
      }
      // load from atomic variable.
      auto loaded_ptr = aptr[s_id]->load();
      aptr[t_id]->store(loaded_ptr);
    }
  };
  for (uint32_t i = 0; i != t_count; i += 1) {
    ts.emplace_back(loop);
  }
  beg.test_and_set(std::memory_order_release);
  for (uint32_t i = 0; i != t_count; i += 1) {
    ts[i].join();
  }
  aptr.clear();
  for (uint32_t i = 0; i != v_count; i += 1) {
    test_lib::assert_equal(is_dropped[i], 1);
  }
}
