#include <jowi/test_lib.hpp>
#include <future>
#include <cstdint>
#include <thread>
#include <chrono>
import jowi.test_lib;
import jowi.asio.lockfree;

namespace test_lib = jowi::test_lib;
namespace asio = jowi::asio;

JOWI_SETUP(argc, argv) {
  test_lib::get_test_context().set_time_unit(test_lib::test_time_unit::MILLI_SECONDS);
}

void increment_uint(uint32_t* d) {
    *d += 1;
}

JOWI_ADD_TEST(test_drop) {
  uint32_t drop_count = 0;
  auto ptr = asio::shared_ptr<uint32_t>{&drop_count,increment_uint};
  ptr.reset();
  test_lib::assert_equal(drop_count, 1);
}

JOWI_ADD_TEST(test_living_copy_do_not_drop) {
    uint32_t drop_count = 0;
    auto ptr = asio::shared_ptr<uint32_t>{&drop_count,increment_uint};
    auto ptr2 = ptr;
    ptr.reset();
    test_lib::assert_equal(drop_count, 0);
}

JOWI_ADD_TEST(test_daemon_drop) {
    uint32_t drop_count = 0;
    auto ptr = asio::shared_ptr<uint32_t>{&drop_count,increment_uint};
    auto fut = std::async(
        std::launch::deferred,
        [](decltype(ptr) ptr){
            ptr.reset();
        },
        ptr
    );
    ptr.reset();
    fut.get();
    test_lib::assert_equal(drop_count, 1);
}

JOWI_ADD_TEST(test_fuzz) {
    uint32_t drop_count = 0;
    auto ptr = asio::shared_ptr<uint32_t>{&drop_count, increment_uint};
    auto start = std::chrono::steady_clock::now();
    auto run_after_delay = [](decltype(start) s, decltype(ptr) ptr){
        std::this_thread::sleep_until(s + std::chrono::milliseconds{test_lib::random_integer(0, 250)});
    };
    auto tcount = test_lib::random_integer(10, 50);
    std::vector<std::thread> ts;
    ts.reserve(tcount);
    for (uint32_t i = 0; i != tcount; i += 1) {
        ts.emplace_back(std::thread{
            run_after_delay,
            start,
            ptr
        });
    }
    // release current copy.
    ptr.reset();
    for (auto& t: ts) {
        t.join();
    }
    test_lib::assert_equal(drop_count, 1);
}
