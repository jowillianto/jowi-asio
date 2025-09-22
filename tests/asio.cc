#include <jowi/test_lib.hpp>
#include <chrono>
#include <functional>
import jowi.test_lib;
import jowi.asio;

namespace test_lib = jowi::test_lib;
namespace asio = jowi::asio;

JOWI_SETUP(argc, argv) {
  test_lib::get_test_context().set_time_unit(test_lib::test_time_unit::MILLI_SECONDS);
}

asio::basic_task<int> random_sleep(uint32_t delay) {
  co_await asio::sleep_for(delay);
  co_return 0;
}

template <class F, class... Args>
  requires(std::invocable<F, Args...>)
std::pair<std::invoke_result_t<F, Args...>, std::chrono::system_clock::duration> timed_invoke(
  F &&f, Args &&...args
) {
  auto beg = std::chrono::system_clock::now();
  auto res = std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
  auto end = std::chrono::system_clock::now();
  return std::pair{std::move(res), end - beg};
}

JOWI_ADD_TEST(test_async_fibonacci) {
  auto beg = std::chrono::system_clock::now();
  asio::gather(random_sleep(500), random_sleep(500));
  auto end = std::chrono::system_clock::now();
  test_lib::assert_true(
    500 <= std::chrono::duration_cast<std::chrono::milliseconds>(end - beg).count()
  );
}