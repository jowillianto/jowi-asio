module;
#include <atomic>
#include <concepts>
#include <tuple>
export module jowi.asio.lockfree:bit_tuple;
import :tagged_ptr;

namespace jowi::asio {
  export template <class... Ts>
  concept BitTupleable = requires() { (sizeof(Ts) + ...) < 8; };

  export template <class... Ts>
    requires(BitTupleable<Ts...>)
  struct BitTuple {
  private:
    alignas(8) std::tuple<Ts...> __packet;

  public:
    constexpr BitTuple(Ts... v) : __packet{std::forward<Ts>(v)...} {}

    constexpr operator uint64_t() const noexcept {
      return to_raw();
    }

    template <uint32_t i> constexpr auto get() const noexcept {
      return std::get<i>(__packet);
    }

    constexpr uint64_t to_raw() const noexcept {
      uint64_t v = 0;
      std::memcpy(&v, &__packet, 8);
      return v;
    }

    /*
     * The user have to guarantee that the bits v is obtained from a valid BitTuple.
     */
    static BitTuple from_bits(uint64_t v) {
      return *reinterpret_cast<BitTuple *>(&v);
    }
  };
}

namespace asio = jowi::asio;

template <class... Ts> struct std::atomic<asio::BitTuple<Ts...>> {
private:
  std::atomic<uint64_t> __a;

public:
  using ValueType = asio::BitTuple<Ts...>;
  using value_type = ValueType;
  atomic(ValueType v) noexcept : __a{static_cast<uint64_t>(v)} {}

  atomic(Ts... vs) noexcept : __a{ValueType{std::forward<Ts>(vs)...}.to_raw()} {}

  void store(ValueType v, asio::MemoryOrder m = asio::memory_order_seq_cst) noexcept {
    __a.store(v, to_store_order(m));
  }

  ValueType exchange(ValueType v, asio::MemoryOrder m = asio::memory_order_seq_cst) noexcept {
    return ValueType::from_bits(__a.exchange(v, m));
  }

  asio::BitTuple<Ts...> load(asio::MemoryOrder m = asio::memory_order_seq_cst) const noexcept {
    return ValueType::from_bits(__a.load(asio::to_load_order(m)));
  }

  bool compare_exchange_weak(
    ValueType &e, ValueType d, asio::MemoryOrder m = asio::memory_order_seq_cst
  ) {
    uint64_t e_raw = e.to_raw();
    uint64_t d_raw = d.to_raw();
    auto [s, f] = to_cas_order(m);
    bool succ = __a.compare_exchange_weak(e_raw, d_raw, s, f);
    if (!succ) {
      e = ValueType::from_bits(e_raw);
    }
    return succ;
  }

  bool compare_exchange_strong(
    ValueType &e, ValueType d, asio::MemoryOrder m = asio::memory_order_seq_cst
  ) {
    uint64_t e_raw = e.to_raw();
    uint64_t d_raw = d.to_raw();
    auto [s, f] = to_cas_order(m);
    bool succ = __a.compare_exchange_strong(e_raw, d_raw, s, f);
    if (!succ) {
      e = ValueType::from_bits(e_raw);
    }
    return succ;
  }
};
