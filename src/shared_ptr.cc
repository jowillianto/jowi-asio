module;
#include <atomic>
#include <memory>
#include <utility>
export module jowi.asio:shared_ptr;

namespace jowi::asio {}

template <class T> struct std::atomic<std::shared_ptr<T>> {
private:
  std::shared_ptr<T> __ptr;

public:
  atomic(std::shared_ptr<T> ptr) : __ptr{std::move(ptr)} {}

  std::shared_ptr<T> load(std::memory_order m) noexcept {
    return std::atomic_load_explicit(&__ptr, m);
  }

  void store(std::shared_ptr<T> desired, std::memory_order m) noexcept {
    std::atomic_store_explicit(&__ptr, std::move(desired), m);
  }

  std::shared_ptr<T> exchange(std::shared_ptr<T> desired, std::memory_order m) noexcept {
    return std::atomic_exchange_explicit(&__ptr, std::move(desired), m);
  }

  bool compare_exchange_weak(
    std::shared_ptr<T> &cur, std::shared_ptr<T> desired, std::memory_order s, std::memory_order r
  ) {
    return std::atomic_compare_exchange_weak_explicit(&__ptr, &cur, std::move(desired), s, r);
  }

  bool compare_exchange_strong(
    std::shared_ptr<T> &cur, std::shared_ptr<T> desired, std::memory_order s, std::memory_order r
  ) {
    return std::atomic_compare_exchange_strong_explicit(&__ptr, &cur, std::move(desired), s, r);
  }
};