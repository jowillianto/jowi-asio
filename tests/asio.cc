#include <jowi/test_lib.hpp>
#include <chrono>
#include <coroutine>
#include <functional>
#include <semaphore>
import jowi.test_lib;
import jowi.asio;

namespace test_lib = jowi::test_lib;
namespace asio = jowi::asio;

JOWI_SETUP(argc, argv) {
  test_lib::get_test_context().set_time_unit(test_lib::test_time_unit::MILLI_SECONDS);
}

asio::unique_task<uint64_t> random_sleep(uint32_t delay) {
  co_await asio::sleep_for(delay / 2);
  co_await asio::sleep_for(delay / 2);
  co_return 0ULL;
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
  auto res = asio::parallel_expected(random_sleep(100), random_sleep(100));
  auto end = std::chrono::steady_clock::now();
  test_lib::assert_equal(test_lib::assert_expected_value(std::move(std::get<0>(res))), 0ULL);
  test_lib::assert_equal(test_lib::assert_expected_value(std::move(std::get<1>(res))), 0ULL);
  test_lib::assert_true(
    std::chrono::duration_cast<std::chrono::milliseconds>(end - beg).count() >= 100
  );
  test_lib::assert_true(
    std::chrono::duration_cast<std::chrono::milliseconds>(end - beg).count() < 200
  );
}

JOWI_ADD_TEST(test_async_task_await) {
  /* Nested Random Sleep. This should run in parallel with random_sleep */
  auto nested_f = [](uint32_t delay) -> asio::unique_task<uint64_t> {
    co_return co_await random_sleep(delay);
  };
  auto beg = std::chrono::steady_clock::now();
  auto res = asio::parallel_expected(nested_f(100), random_sleep(100));
  auto end = std::chrono::steady_clock::now();
  test_lib::assert_equal(test_lib::assert_expected_value(std::move(std::get<0>(res))), 0ULL);
  test_lib::assert_equal(test_lib::assert_expected_value(std::move(std::get<1>(res))), 0ULL);
  test_lib::assert_true(
    std::chrono::duration_cast<std::chrono::milliseconds>(end - beg).count() >= 100
  );
  test_lib::assert_true(
    std::chrono::duration_cast<std::chrono::milliseconds>(end - beg).count() < 200
  );
}

JOWI_ADD_TEST(test_await_waiting_task) {
  std::binary_semaphore s{1};
  auto task1 = [&]() -> asio::unique_task<uint32_t> {
    co_await asio::sleep_for(std::chrono::milliseconds{100});
    s.release();
    co_return 0ULL;
  };
  auto task2 = [&]() -> asio::unique_task<uint32_t> {
    co_await asio::asema_acquire{s};
    co_await asio::asema_acquire{s};
    co_return 0ULL;
  };

  auto res = asio::parallel_expected(task1(), task2());
  test_lib::assert_equal(test_lib::assert_expected_value(std::move(std::get<0>(res))), 0ULL);
  test_lib::assert_equal(test_lib::assert_expected_value(std::move(std::get<1>(res))), 0ULL);
}
