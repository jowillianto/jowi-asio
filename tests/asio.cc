#include <jowi/test_lib.hpp>
#include <chrono>
#include <coroutine>
#include <functional>
import jowi.test_lib;
import jowi.asio;

namespace test_lib = jowi::test_lib;
namespace asio = jowi::asio;

JOWI_SETUP(argc, argv) {
  test_lib::get_test_context().set_time_unit(test_lib::test_time_unit::MILLI_SECONDS);
}

asio::basic_task<void> random_sleep(uint32_t delay) {
  co_await asio::sleep_for(delay);
}

template <class F, class... Args>
  requires(std::invocable<F, Args...>)
std::pair<std::invoke_result_t<F, Args...>, std::chrono::steady_clock::duration> timed_invoke(
  F &&f, Args &&...args
) {
  auto beg = std::chrono::steady_clock::now();
  auto res = std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
  auto end = std::chrono::steady_clock::now();
  return std::pair{std::move(res), end - beg};
}

JOWI_ADD_TEST(test_async_sleep) {
  auto beg = std::chrono::steady_clock::now();
  asio::gather(random_sleep(500), random_sleep(500));
  auto end = std::chrono::steady_clock::now();
  test_lib::assert_true(
    std::chrono::duration_cast<std::chrono::milliseconds>(end - beg).count() <= 510
  );
}

JOWI_ADD_TEST(test_async_task_await) {
  /* Nested Random Sleep. This should run in parallel with random_sleep */
  auto nested_f = [](uint32_t delay) -> asio::basic_task<void> { co_await random_sleep(delay); };
  auto beg = std::chrono::steady_clock::now();
  asio::gather(nested_f(500), random_sleep(500));
  auto end = std::chrono::steady_clock::now();
  test_lib::assert_true(
    std::chrono::duration_cast<std::chrono::milliseconds>(end - beg).count() <= 510
  );
}