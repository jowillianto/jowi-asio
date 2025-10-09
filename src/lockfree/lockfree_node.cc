module;
#include <atomic>
#include <concepts>
#include <optional>
export module jowi.asio.lockfree:lockfree_node;
import :shared_ptr;
import :tagged_ptr;

namespace jowi::asio {
  template <class T> struct lockfree_node {
  private:
    using This = lockfree_node;
    std::optional<T> __value;

  public:
    std::atomic<shared_ptr<This>> next;
    template <class... Args>
      requires(std::constructible_from<std::optional<T>, Args...>)
    lockfree_node(shared_ptr<This> next = nullptr, Args &&...args) :
      __value(std::forward<Args>(args)...), next(std::move(next)) {}
    T move_value() {
      return std::move(__value).value();
    }
  };

  export template <class T> struct lockfree_queue {
  private:
    using node_type = lockfree_node<T>;
    node_type __head;
    std::atomic<shared_ptr<node_type>> __tail;
    std::atomic<uint64_t> __size;

    std::optional<shared_ptr<node_type>> __pop() {
      /**
       * simply, we swap head_next with head_next_next through CAS and update __tail if it ever
       * catches up.
       */
      shared_ptr<node_type> head_next = __head.next.load(memory_order_strict);
      if (!head_next) {
        reset_tail();
        return std::nullopt;
      }
      shared_ptr<node_type> head_next_next = head_next->next.load(memory_order_strict);
      while (!__head.next.compare_exchange_weak(head_next, head_next_next, memory_order_strict)) {
        if (!head_next) {
          reset_tail();
          return std::nullopt;
        }
        head_next_next = head_next->next.load(memory_order_strict);
      }
      // head_next is now a stupid node with nothing in it.
      __size.fetch_sub(1, std::memory_order_relaxed);
      head_next->next.store(nullptr, memory_order_relaxed);
      return head_next;
    }

  public:
    lockfree_queue() : __head{nullptr, std::nullopt}, __tail{&__head, [](auto *) {}}, __size{0} {}

    template <class... Args>
      requires(std::constructible_from<T, Args...>)
    void push(Args &&...args) {
      auto new_node = make_shared<node_type>(nullptr, std::forward<Args>(args)...);
      /*
       * 1. Find the tail (i.e. node which next node is nullptr)
       * 2. CAS
       * 3. mission Complete, update __tail in the process.
       *
       * NB: __tail is a cache.
       */
      shared_ptr<node_type> tail = __tail.load(memory_order_strict);
      shared_ptr<node_type> tail_next{nullptr};

      // Find tail, i.e. we can only replace if tail_next is a nullptr.
      while (!(tail->next).compare_exchange_weak(tail_next, new_node, memory_order_strict)) {
        tail = std::move(tail_next); // this will set tail_next to nullptr
      }
      // update tail (could be outdated by the time we update tho hohoho)
      __tail.store(new_node, memory_order_relaxed);
      __size.fetch_add(1, std::memory_order_relaxed);
    }

    inline void reset_tail() {
      __tail.store({&__head, [](auto *) {}}, memory_order_strict);
    }

    std::optional<T> pop() {
      return __pop().transform(&node_type::move_value);
    }

    void clear() {
      auto node = __pop();
      while (node) {
        node = __pop();
      }
    }

    uint64_t size(std::memory_order m = std::memory_order_seq_cst) const noexcept {
      return __size.load(m);
    }

    ~lockfree_queue() {
      clear();
    }
  };

  export template <class T> struct lockfree_stack {
  private:
    using node_type = lockfree_node<T>;
    node_type __head;
    std::atomic<uint64_t> __size;

  public:
    lockfree_stack() : __head{}, __size{0} {}

    uint64_t size(std::memory_order m = std::memory_order_seq_cst) const noexcept {
      return __size.load(m);
    }
  };
}
