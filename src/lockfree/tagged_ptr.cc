module;
#include <algorithm>
#include <atomic>
#include <compare>
#include <cstdint>
#include <utility>
export module jowi.asio.lockfree:tagged_ptr;

namespace jowi::asio {
  export enum struct MemoryOrder { relaxed = 0, sequential, strict };
  export MemoryOrder memory_order_relaxed = MemoryOrder::relaxed;
  export MemoryOrder memory_order_seq_cst = MemoryOrder::sequential;
  export MemoryOrder memory_order_strict = MemoryOrder::strict;

  constexpr std::pair<std::memory_order, std::memory_order> to_cas_order(MemoryOrder m) {
    if (m == MemoryOrder::relaxed) {
      return {std::memory_order_relaxed, std::memory_order_relaxed};
    } else if (m == MemoryOrder::sequential) {
      return {std::memory_order_seq_cst, std::memory_order_seq_cst};
    } else {
      return {std::memory_order_acq_rel, std::memory_order_acquire};
    }
  }

  constexpr std::memory_order to_store_order(MemoryOrder m) {
    if (m == MemoryOrder::relaxed) return std::memory_order_relaxed;
    else if (m == MemoryOrder::sequential)
      return std::memory_order_seq_cst;
    else
      return std::memory_order_release;
  }

  constexpr std::memory_order to_load_order(MemoryOrder m) {
    if (m == MemoryOrder::relaxed) return std::memory_order_relaxed;
    else if (m == MemoryOrder::sequential)
      return std::memory_order_seq_cst;
    else
      return std::memory_order_acquire;
  }

  export template <class D, uint8_t ptr_size>
  concept Taggable = requires() {
    ptr_size <= sizeof(void *);
    sizeof(D) <= sizeof(void *) - ptr_size;
  };

  export template <class D, uint8_t psize = 6>
    requires(Taggable<D, psize>)
  struct TaggedPtr {
  private:
    alignas(sizeof(void *)) uint64_t __v;

    explicit TaggedPtr(uint64_t v) : __v{v} {}

    friend struct std::atomic<TaggedPtr<D, psize>>;

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
    inline void *raw_ptr() const noexcept {
      return reinterpret_cast<void *>((__v << tag_bit_size()) >> tag_bit_size());
    }
    template <class T> inline T *ptr() const noexcept {
      return static_cast<T *>(raw_ptr());
    }
    inline uint16_t raw_tag() const noexcept {
      return static_cast<uint16_t>(__v >> ptr_bit_size());
    }
    inline D tag() const noexcept {
      uint16_t raw = raw_tag();
      /**
       * SAFETY: we are reading the bits in uint16_t in terms of D.
       */
      return *static_cast<D *>(static_cast<void *>(&raw));
    }
    inline std::pair<void *, D> to_raw_pair() const noexcept {
      return std::pair{raw_ptr(), tag()};
    }
    template <class T> inline std::pair<T *, D> to_pair() const noexcept {
      return std::pair{ptr<T>(), tag()};
    }

    /*
     * bitwise comparison operator
     */
    constexpr friend bool operator==(const TaggedPtr &l, const TaggedPtr &r) {
      return l.__v == r.__v;
    }
    constexpr friend std::partial_ordering operator<=>(const TaggedPtr &l, const TaggedPtr &r) {
      if (l == r) {
        return std::partial_ordering::equivalent;
      } else {
        return std::partial_ordering::unordered;
      }
    }

    static TaggedPtr from_pair(void *ptr, D tag) {
      // not typo, we want the right shift to be logical
      uint64_t v = (reinterpret_cast<uint64_t>(ptr) << tag_bit_size()) >> tag_bit_size();
      uint64_t long_tag = 0;
      std::memcpy(&long_tag, &tag, std::min(static_cast<uint8_t>(sizeof(tag)), tag_size()));
      long_tag = long_tag << ptr_bit_size();
      return TaggedPtr{v | long_tag};
    }

    inline static TaggedPtr null() {
      return TaggedPtr{0};
    }

    inline static TaggedPtr null_tag(void *ptr) {
      return TaggedPtr{(reinterpret_cast<uint64_t>(ptr) << tag_bit_size()) >> tag_bit_size()};
    }
  };

  /* specialisation */
  template struct TaggedPtr<uint16_t>;
  template struct TaggedPtr<int16_t>;
  template struct TaggedPtr<bool>;

  export using Uint16TaggedPtr = TaggedPtr<uint16_t>;
  export using Int16TaggedPtr = TaggedPtr<int16_t>;
  export using BoolTaggedPtr = TaggedPtr<bool>;
}

namespace asio = jowi::asio;

template <class D, uint8_t psize>
  requires(asio::Taggable<D, psize>)
struct std::atomic<asio::TaggedPtr<D, psize>> {
private:
  std::atomic<uint64_t> __v;

  using TaggedPtr = asio::TaggedPtr<D, psize>;

public:
  atomic(asio::TaggedPtr<D, psize> v) : __v{v.__v} {}

  asio::TaggedPtr<D, psize> load(
    asio::MemoryOrder m = asio::memory_order_seq_cst
  ) const noexcept {
    return TaggedPtr{__v.load(asio::to_load_order(m))};
  }
  void store(
    asio::TaggedPtr<D, psize> d, asio::MemoryOrder m = asio::memory_order_seq_cst
  ) noexcept {
    __v.store(d.__v, asio::to_store_order(m));
  }

  asio::TaggedPtr<D, psize> exchange(
    asio::TaggedPtr<D, psize> d, asio::MemoryOrder m = asio::memory_order_seq_cst
  ) noexcept {
    return TaggedPtr{__v.exchange(d.__v, asio::to_cas_order(m).first)};
  }

  bool compare_exchange_weak(
    asio::TaggedPtr<D, psize> &e,
    asio::TaggedPtr<D, psize> d,
    asio::MemoryOrder m = asio::memory_order_seq_cst
  ) noexcept {
    auto [s, f] = asio::to_cas_order(m);
    return __v.compare_exchange_weak(e.__v, d.__v, s, f);
  }

  bool compare_exchange_strong(
    asio::TaggedPtr<D, psize> &e,
    asio::TaggedPtr<D, psize> d,
    asio::MemoryOrder m = asio::memory_order_seq_cst
  ) noexcept {
    auto [s, f] = asio::to_cas_order(m);
    return __v.compare_exchange_strong(e.__v, d.__v, s, f);
  }

  void store_tag(D tag, asio::MemoryOrder m = asio::MemoryOrder::sequential) noexcept {
    exchange_tag(tag, m);
  }

  void store_ptr(void *ptr, asio::MemoryOrder m = asio::MemoryOrder::sequential) noexcept {
    exchange_ptr(ptr, m);
  }

  TaggedPtr exchange_tag(D tag, asio::MemoryOrder m = asio::MemoryOrder::sequential) noexcept {
    auto cur_ptr = TaggedPtr::null();
    auto target_ptr = TaggedPtr::null_tag(nullptr);
    while (!compare_exchange_weak(cur_ptr, target_ptr, m)) {
      target_ptr = TaggedPtr::from_pair(cur_ptr.to_raw_pair().first, tag);
    }
    return cur_ptr;
  }

  TaggedPtr exchange_ptr(
    void *ptr, asio::MemoryOrder m = asio::MemoryOrder::sequential
  ) noexcept {
    auto cur_ptr = TaggedPtr::null();
    auto target_ptr = TaggedPtr::from_pair(ptr, static_cast<D>(0));
    while (!compare_exchange_weak(cur_ptr, target_ptr, m)) {
      target_ptr = TaggedPtr::from_pair(ptr, cur_ptr.to_raw_pair().second);
    }
    return cur_ptr;
  }
};

/*
  Instantiate several atomics
*/
template struct std::atomic<asio::Int16TaggedPtr>;
template struct std::atomic<asio::Uint16TaggedPtr>;
template struct std::atomic<asio::BoolTaggedPtr>;
