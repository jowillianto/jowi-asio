module;
#include <atomic>
#include <concepts>
#include <optional>
export module jowi.asio.lockfree:lockfree_queue;
import :shared_ptr;
import :tagged_ptr;

namespace jowi::asio {
  template <class T> struct LockFreeQueueNode {
  private:
    using This = LockFreeQueueNode;
    std::optional<T> __value;

  public:
    std::atomic<SharedPtr<This>> next;
    template <class... Args>
      requires(std::constructible_from<std::optional<T>, Args...>)
    LockFreeQueueNode(Args &&...args) : __value{std::forward<Args>(args)...}, next{nullptr} {}
    T move_value() {
      return std::move(__value).value();
    }
  };
  /*
   * lockfree queue implementation based on the michael-scott queue with ref count memory
   * reclamation.
   */
  export template <class T> struct LockFreeQueue {
  private:
    using NodeType = LockFreeQueueNode<T>;
    NodeType __head;
    std::atomic<SharedPtr<NodeType>> __tail;
    std::atomic<uint64_t> __size;

    std::optional<SharedPtr<NodeType>> __pop() {
      /**
       * simply, we swap head_next with head_next_next through CAS and update __tail if it ever
       * catches up.
       */
      SharedPtr<NodeType> head_next = __head.next.load(memory_order_strict);
      if (!head_next) {
        reset_tail();
        return std::nullopt;
      }
      SharedPtr<NodeType> head_next_next = head_next->next.load(memory_order_strict);
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
    LockFreeQueue() : __head{std::nullopt}, __tail{&__head, [](auto *) {}}, __size{0} {}

    template <class... Args>
      requires(std::constructible_from<T, Args...>)
    void push(Args &&...args) {
      auto new_node = asio::make_shared<NodeType>(std::forward<Args>(args)...);
      /*
       * 1. Find the tail (i.e. node which next node is nullptr)
       * 2. CAS
       * 3. mission Complete, update __tail in the process.
       *
       * NB: __tail is a cache.
       */
      SharedPtr<NodeType> tail = __tail.load(memory_order_strict);
      SharedPtr<NodeType> tail_next{nullptr};

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
      return __pop().transform(&NodeType::move_value);
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

    ~LockFreeQueue() {
      clear();
    }
  };
}
