module;
#include <algorithm>
#include <atomic>
#include <compare>
#include <cstdint>
#include <utility>
export module jowi.asio.lockfree:tagged_ptr;

namespace jowi::asio {
  export enum struct memory_order { relaxed = 0, sequential, strict };
  export memory_order memory_order_relaxed = memory_order::relaxed;
  export memory_order memory_order_seq_cst = memory_order::sequential;
  export memory_order memory_order_strict = memory_order::strict;

  constexpr std::pair<std::memory_order, std::memory_order> to_cas_order(memory_order m) {
    if (m == memory_order::relaxed) {
      return {std::memory_order_relaxed, std::memory_order_relaxed};
    } else if (m == memory_order::sequential) {
      return {std::memory_order_seq_cst, std::memory_order_seq_cst};
    } else {
      return {std::memory_order_acq_rel, std::memory_order_acquire};
    }
  }

  constexpr std::memory_order to_store_order(memory_order m) {
    if (m == memory_order::relaxed) return std::memory_order_relaxed;
    else if (m == memory_order::sequential)
      return std::memory_order_seq_cst;
    else
      return std::memory_order_release;
  }

  constexpr std::memory_order to_load_order(memory_order m) {
    if (m == memory_order::relaxed) return std::memory_order_relaxed;
    else if (m == memory_order::sequential)
      return std::memory_order_seq_cst;
    else
      return std::memory_order_acquire;
  }

  export template <class D, uint8_t psize = 6>
    requires(sizeof(D) <= (sizeof(void *) - psize) && psize <= sizeof(void *))
  struct tagged_ptr {
  private:
    alignas(sizeof(void *)) uint64_t __v;

    explicit tagged_ptr(uint64_t v) : __v{v} {}

    friend struct std::atomic<tagged_ptr<D, psize>>;

  public:
    /*
      returns the size of the tag in bits.
    */
    static consteval uint8_t tag_size() {
      return 8 - psize;
    }
    static consteval uint8_t ptr_size() {
      return psize;
    }
    static consteval uint32_t tag_bit_size() {
      return tag_size() * 8;
    }
    static consteval uint32_t ptr_bit_size() {
      return ptr_size() * 8;
    }
    void *raw_ptr() const noexcept {
      return reinterpret_cast<void *>(
        static_cast<int64_t>(__v << tag_bit_size()) >> tag_bit_size()
      );
    }
    template <class T> T *ptr() const noexcept {
      return static_cast<T *>(raw_ptr());
    }
    uint16_t raw_tag() const noexcept {
      return static_cast<uint16_t>(__v >> ptr_bit_size());
    }
    D tag() const noexcept {
      return static_cast<D>(raw_tag());
    }
    std::pair<void *, D> to_raw_pair() const noexcept {
      return std::pair{raw_ptr(), tag()};
    }
    template <class T> std::pair<T *, D> to_pair() const noexcept {
      return std::pair{ptr<T>(), tag()};
    }

    /*
     * bitwise comparison operator
     */
    constexpr friend bool operator==(const tagged_ptr &l, const tagged_ptr &r) {
      return l == r;
    }
    constexpr friend std::partial_ordering operator<=>(const tagged_ptr &l, const tagged_ptr &r) {
      if (l == r) {
        return std::partial_ordering::equivalent;
      } else {
        return std::partial_ordering::unordered;
      }
    }

    static tagged_ptr from_pair(void *ptr, D tag) {
      // not typo, we want the right shift to be logical
      uint64_t v = (reinterpret_cast<uint64_t>(ptr) << tag_bit_size()) >> tag_bit_size();
      uint64_t long_tag = 0;
      std::memcpy(&long_tag, &tag, std::min(static_cast<uint8_t>(sizeof(tag)), tag_size()));
      long_tag = long_tag << ptr_bit_size();
      return tagged_ptr{v | long_tag};
    }

    static tagged_ptr null() {
      return tagged_ptr{0};
    }

    static tagged_ptr null_tag(void *ptr) {
      return tagged_ptr{(reinterpret_cast<uint64_t>(ptr) << tag_bit_size()) >> tag_bit_size()};
    }
  };

  /* specialisation */
  template struct tagged_ptr<uint16_t>;
  template struct tagged_ptr<int16_t>;
  template struct tagged_ptr<bool>;

  export using uint16_tagged_ptr = tagged_ptr<uint16_t>;
  export using int16_tagged_ptr = tagged_ptr<int16_t>;
  export using bool_tagged_ptr = tagged_ptr<bool>;
}

namespace asio = jowi::asio;

template <class D, uint8_t psize>
  requires(sizeof(D) <= (8 - psize))
struct std::atomic<asio::tagged_ptr<D, psize>> {
private:
  std::atomic<uint64_t> __v;

  using tagged_ptr = asio::tagged_ptr<D, psize>;

public:
  atomic(asio::tagged_ptr<D, psize> v) : __v{v.__v} {}

  asio::tagged_ptr<D, psize> load(
    asio::memory_order m = asio::memory_order_seq_cst
  ) const noexcept {
    return tagged_ptr{__v.load(asio::to_load_order(m))};
  }
  void store(
    asio::tagged_ptr<D, psize> d, asio::memory_order m = asio::memory_order_seq_cst
  ) noexcept {
    __v.store(d.__v, asio::to_store_order(m));
  }

  asio::tagged_ptr<D, psize> exchange(
    asio::tagged_ptr<D, psize> d, asio::memory_order m = asio::memory_order_seq_cst
  ) noexcept {
    return tagged_ptr{__v.exchange(d.__v, asio::to_cas_order(m).first)};
  }

  bool compare_exchange_weak(
    asio::tagged_ptr<D, psize> &e,
    asio::tagged_ptr<D, psize> d,
    asio::memory_order m = asio::memory_order_seq_cst
  ) noexcept {
    auto [s, f] = asio::to_cas_order(m);
    return __v.compare_exchange_weak(e.__v, d.__v, s, f);
  }

  bool compare_exchange_strong(
    asio::tagged_ptr<D, psize> &e,
    asio::tagged_ptr<D, psize> d,
    asio::memory_order m = asio::memory_order_seq_cst
  ) noexcept {
    auto [s, f] = asio::to_cas_order(m);
    return __v.compare_exchange_strong(e.__v, d.__v, s, f);
  }

  void store_tag(D tag, asio::memory_order m = asio::memory_order::sequential) noexcept {
    exchange_tag(tag, m);
  }

  void store_ptr(void *ptr, asio::memory_order m = asio::memory_order::sequential) noexcept {
    exchange_ptr(ptr, m);
  }

  tagged_ptr exchange_tag(D tag, asio::memory_order m = asio::memory_order::sequential) noexcept {
    auto cur_ptr = tagged_ptr::null();
    auto target_ptr = tagged_ptr::null_tag(nullptr);
    while (!compare_exchange_weak(cur_ptr, target_ptr, m)) {
      target_ptr = tagged_ptr::from_pair(cur_ptr.to_raw_pair().first, tag);
    }
    return cur_ptr;
  }

  tagged_ptr exchange_ptr(
    void *ptr, asio::memory_order m = asio::memory_order::sequential
  ) noexcept {
    auto cur_ptr = tagged_ptr::null();
    auto target_ptr = tagged_ptr::from_pair(ptr, static_cast<D>(0));
    while (!compare_exchange_weak(cur_ptr, target_ptr, m)) {
      target_ptr = tagged_ptr::from_pair(ptr, cur_ptr.to_raw_pair().second);
    }
    return cur_ptr;
  }
};

/*
  Instantiate several atomics
*/
template struct std::atomic<asio::int16_tagged_ptr>;
template struct std::atomic<asio::uint16_tagged_ptr>;
template struct std::atomic<asio::bool_tagged_ptr>;
