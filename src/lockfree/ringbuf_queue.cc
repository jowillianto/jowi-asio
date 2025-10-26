module;
#include <atomic>
#include <memory>
#include <optional>
#include <vector>
export module jowi.asio.lockfree:ringbuf_queue;
import :shared_ptr;
import :tagged_ptr;
import :bit_tuple;

namespace jowi::asio {
  template <class T, std::invocable<T *> D, uint8_t ptr_size = 6>
    requires(taggable<D, ptr_size>)
  struct ringbuf_queue_node {
  private:
    std::atomic<tagged_ptr<D, ptr_size>> __ptr;

    using tagged_ptr = tagged_ptr<D, ptr_size>;

  public:
    constexpr ringbuf_queue_node() noexcept : __ptr{tagged_ptr::null()} {}
    constexpr ringbuf_queue_node(ringbuf_queue_node &&o) noexcept :
      __ptr{o.__ptr.exchange(tagged_ptr::null(), asio::memory_order_relaxed)} {}

    ringbuf_queue_node &operator=(ringbuf_queue_node &&o) noexcept {
      auto ptr = o.__ptr.exchange(tagged_ptr::null(), asio::memory_order_relaxed);
      __ptr.store(ptr, asio::memory_order_relaxed);
      return *this;
    }

    constexpr std::optional<std::unique_ptr<T, D>> load(
      asio::memory_order m = asio::memory_order_seq_cst
    ) noexcept {
      auto ptr = __ptr.exchange(tagged_ptr::null(), m);
      if (ptr == tagged_ptr::null()) {
        return std::nullopt;
      }
      return std::unique_ptr<T, D>{ptr.template ptr<T>(), ptr.tag()};
    }

    constexpr std::optional<std::unique_ptr<T, D>> store(
      std::unique_ptr<T, D> ptr, asio::memory_order m = asio::memory_order_seq_cst
    ) noexcept {
      auto prev_ptr = __ptr.exchange(tagged_ptr::from_pair(ptr.release(), ptr.get_deleter()), m);
      if (prev_ptr == tagged_ptr::null()) {
        return std::nullopt;
      }
      return std::unique_ptr<T, D>{prev_ptr.template ptr<T>(), prev_ptr.tag()};
    }
    constexpr std::optional<std::unique_ptr<T, D>> try_store(
      std::unique_ptr<T, D> ptr, asio::memory_order m = asio::memory_order_seq_cst
    ) noexcept {
      auto e_ptr = tagged_ptr::null();
      auto d_ptr = tagged_ptr::from_pair(ptr.get(), ptr.get_deleter());
      bool success = __ptr.compare_exchange_strong(e_ptr, d_ptr, m);
      if (success) {
        ptr.release();
        return std::nullopt;
      }
      return ptr;
    }

    constexpr void blocking_store(
      std::unique_ptr<T, D> ptr, asio::memory_order m = asio::memory_order_seq_cst
    ) noexcept {
      auto e_ptr = tagged_ptr::null();
      auto d_ptr = tagged_ptr::from_pair(ptr.release(), ptr.get_deleter());
      while (!__ptr.compare_exchange_weak(e_ptr, d_ptr, m)) {
        e_ptr = tagged_ptr::null();
      }
    }

    ~ringbuf_queue_node() {
      auto ptr = load(asio::memory_order_relaxed);
    }
  };

  template struct ringbuf_queue_node<uint32_t, std::default_delete<uint32_t>, 6>;

  export template <class T, std::invocable<T *> D = std::default_delete<T>>
    requires(std::constructible_from<D>)
  struct ringbuf_queue {
  private:
    std::vector<ringbuf_queue_node<T, D>> __nodes;
    using pointer_type = bit_tuple<uint32_t, uint32_t>;
    std::atomic<pointer_type> __counter;

  public:
    ringbuf_queue(uint32_t max_size) : __nodes{}, __counter{pointer_type{0, 0}} {
      __nodes.reserve(max_size);
      for (uint64_t i = 0; i != max_size; i += 1) {
        __nodes.emplace_back();
      }
    }

    uint32_t capacity() const noexcept {
      return __nodes.size();
    }

    void push(std::unique_ptr<T, D> ptr) {
      pointer_type e_point{0, 0};
      pointer_type d_point{1, 0};
      /*
       * push() means incrementing the push pointer by one such that it will never overlap with the
       * pop pointer. pop() can move the pop pointer to overlap with the push pointer but the push
       * pointer should never move the push pointer to overlap with the pop pointer, the operation
       * will block otherwise.
       */
      while (!__counter.compare_exchange_weak(e_point, d_point, asio::memory_order_strict)) {
        uint32_t d_push_point = (e_point.get<0>() + 1) % capacity();
        if (d_push_point == e_point.get<1>()) {
          // no advance
          e_point = pointer_type{0, 0};
          d_point = pointer_type{1, 0};
        } else {
          d_point = pointer_type{d_push_point, e_point.get<1>()};
        }
      }
      /*
       * Now get the pointer we have protected. blocking_store here should ideally not block.
       */
      __nodes[e_point.get<0>()].blocking_store(std::move(ptr), asio::memory_order_strict);
    }

    template <class... Args>
      requires(std::constructible_from<T, Args...> && std::same_as<D, std::default_delete<T>>)
    void push(Args &&...args) {
      push(std::make_unique<T>(std::forward<Args>(args)...));
    }

    std::optional<std::unique_ptr<T, D>> try_push(std::unique_ptr<T, D> ptr) {
      pointer_type e_point{0, 0};
      pointer_type d_point{1, 0};
      /*
       * Quit loop when there is no space.
       */
      while (!__counter.compare_exchange_weak(e_point, d_point, asio::memory_order_strict)) {
        uint32_t d_push_point = (e_point.get<0>() + 1) % capacity();
        if (d_push_point == e_point.get<1>()) {
          // no advance
          return std::move(ptr);
        } else {
          d_point = pointer_type{d_push_point, e_point.get<1>()};
        }
      }
      __nodes[e_point.get<0>()].blocking_store(std::move(ptr), asio::memory_order_strict);
      return std::nullopt;
    }

    template <class... Args>
      requires(std::constructible_from<T, Args...> && std::same_as<D, std::default_delete<T>>)
    std::optional<std::unique_ptr<T, D>> try_push(Args &&...args) {
      return try_push(std::make_unique<T>(std::forward<Args>(args)...));
    }

    std::optional<std::unique_ptr<T, D>> pop() {
      pointer_type e_point{1, 0};
      pointer_type d_point{1, 1};
      while (!__counter.compare_exchange_weak(e_point, d_point, asio::memory_order_strict)) {
        uint32_t d_pop_point = (e_point.get<1>() + 1) % capacity();
        // no advance. cannot pop if is equal
        if (e_point.get<0>() == e_point.get<1>()) {
          return std::nullopt;
        }
        d_point = pointer_type{e_point.get<0>(), d_pop_point};
      }
      return __nodes[e_point.get<1>()].load(asio::memory_order_strict);
    }
  };
}
