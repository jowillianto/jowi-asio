module;
#include <atomic>
#include <concepts>
#include <tuple>
export module jowi.asio.lockfree:bit_tuple;
import :tagged_ptr;

namespace jowi::asio {
  export template <class... Ts>
  concept bit_tupleable = requires() { (sizeof(Ts) + ...) < 8; };

  export template <class... Ts>
    requires(bit_tupleable<Ts...>)
  struct bit_tuple {
  private:
    alignas(8) std::tuple<Ts...> __packet;

  public:
    constexpr bit_tuple(Ts... v) : __packet{std::forward<Ts>(v)...} {}

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
     * The user have to guarantee that the bits v is obtained from a valid bit_tuple.
     */
    static bit_tuple from_bits(uint64_t v) {
      return *reinterpret_cast<bit_tuple *>(&v);
    }
  };
}

namespace asio = jowi::asio;

template <class... Ts> struct std::atomic<asio::bit_tuple<Ts...>> {
private:
  std::atomic<uint64_t> __a;

public:
  using value_type = asio::bit_tuple<Ts...>;
  atomic(value_type v) noexcept : __a{static_cast<uint64_t>(v)} {}

  atomic(Ts... vs) noexcept : __a{value_type{std::forward<Ts>(vs)...}.to_raw()} {}

  void store(value_type v, asio::memory_order m = asio::memory_order_seq_cst) noexcept {
    __a.store(v, to_store_order(m));
  }

  value_type exchange(value_type v, asio::memory_order m = asio::memory_order_seq_cst) noexcept {
    return value_type::from_bits(__a.exchange(v, m));
  }

  asio::bit_tuple<Ts...> load(asio::memory_order m = asio::memory_order_seq_cst) const noexcept {
    return value_type::from_bits(__a.load(asio::to_load_order(m)));
  }

  bool compare_exchange_weak(
    value_type &e, value_type d, asio::memory_order m = asio::memory_order_seq_cst
  ) {
    uint64_t e_raw = e.to_raw();
    uint64_t d_raw = d.to_raw();
    auto [s, f] = to_cas_order(m);
    bool succ = __a.compare_exchange_weak(e_raw, d_raw, s, f);
    if (!succ) {
      e = value_type::from_bits(e_raw);
    }
    return succ;
  }

  bool compare_exchange_strong(
    value_type &e, value_type d, asio::memory_order m = asio::memory_order_seq_cst
  ) {
    uint64_t e_raw = e.to_raw();
    uint64_t d_raw = d.to_raw();
    auto [s, f] = to_cas_order(m);
    bool succ = __a.compare_exchange_strong(e_raw, d_raw, s, f);
    if (!succ) {
      e = value_type::from_bits(e_raw);
    }
    return succ;
  }
};
