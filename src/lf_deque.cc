module;
#include <atomic>
#include <concepts>
#include <cstddef>
#include <memory>
#include <optional>
export module jowi.asio:lf_deque;
import :shared_ptr;

namespace jowi::asio {

  template <class T> struct lf_node {
    std::optional<T> value;
    std::atomic<std::shared_ptr<lf_node>> next_node;

    lf_node() : value{std::nullopt}, next_node{nullptr} {}
    template <class... Args>
      requires(std::constructible_from<T, Args...>)
    lf_node(Args &&...args) : value{std::forward<Args>(args)...}, next_node{nullptr} {}
  };

  export template <class T> struct lf_deque {
  private:
    std::shared_ptr<lf_node<T>> __head;
    std::atomic<std::shared_ptr<lf_node<T>>> __tail;
    std::atomic<size_t> __size;

    /*
      Inserts into
    */
    void __push_back(std::shared_ptr<lf_node<T>> new_node) noexcept {
      /*
        In order to push back, we first need to find a node which is a tail.
        A tail is defined as a node which next_node is a nullptr
      */
      auto tail = __tail.load(std::memory_order_acquire);
      std::shared_ptr<lf_node<T>> tail_next{nullptr};
      // we expect that tail_next_node contains nullptr and if such, it should be set to
      // new_node
      while (!tail->next_node.compare_exchange_weak(
        tail_next, new_node, std::memory_order_acq_rel, std::memory_order_acquire
      )) {
        // Otherwise, we advance the tail.
        tail = std::move(tail_next);
      }
      // No need to sync between threads, this is protected by the invariant above.
      __tail.store(new_node, std::memory_order_relaxed);
      __size.fetch_add(1, std::memory_order_relaxed);
    }

    void __push_front(std::shared_ptr<lf_node<T>> new_node) noexcept {
      /*
        Inserts to the start of the node. The following have to be done:
        - load head_next
        - set head_next to the current node next.
        - cas the current head_next to the current node.
        This way, all connected new heads will be a valid head.
      */
      auto head_next_node = __head->next_node.load(std::memory_order_acquire);
      new_node->next_node.store(head_next_node, std::memory_order_relaxed);
      while (!__head->next_node.compare_exchange_weak(
        head_next_node, new_node, std::memory_order_acq_rel, std::memory_order_acquire
      )) {
        new_node->next_node.store(head_next_node, std::memory_order_relaxed);
      }
      __size.fetch_add(1, std::memory_order_relaxed);
    }

    std::optional<std::shared_ptr<lf_node<T>>> __pop_front() noexcept {
      /*
        Head is always a dummy. Such that the true head is the node after the head. To perform an
        atomic pop, we need to perform the following:
        - read the head_next
        - read head_next_next
        - swap head_next -> head_next_next.
        Now that head is detached:
        - set head_next -> next to nullptr.
        We will freely set the tail over here, since the tail itself will protect its own ivnariant.
      */
      auto to_pop = __head->next_node.load(std::memory_order_acquire);
      if (!to_pop) {
        return std::nullopt;
      }
      auto to_pop_next = to_pop->next_node.load(std::memory_order_acquire);
      while (!__head->next_node.compare_exchange_weak(
        to_pop, to_pop_next, std::memory_order_acq_rel, std::memory_order_acquire
      )) {
        if (!to_pop) {
          return std::nullopt;
        }
        to_pop_next = to_pop->next_node.load(std::memory_order_acquire);
      }
      if (!to_pop_next) {
        __tail.store(__head, std::memory_order_release);
      }
      __size.fetch_sub(1, std::memory_order_relaxed);
      return to_pop;
    }

  public:
    lf_deque() noexcept : __head{std::make_shared<lf_node<T>>()}, __tail{__head} {}

    template <class... Args>
      requires(std::constructible_from<T, Args...>)
    void push_back(Args &&...args) {
      __push_back(std::make_shared<lf_node<T>>(std::forward<Args>(args)...));
    }

    template <class... Args>
      requires(std::constructible_from<T, Args...>)
    void push_front(Args &&...args) {
      __push_front(std::make_shared<lf_node<T>>(std::forward<Args>(args)...));
    }

    std::optional<T> pop() noexcept {
      return __pop_front().and_then([](std::shared_ptr<lf_node<T>> ptr) { return ptr->value; });
    }

    size_t size() const noexcept {
      return __size.load(std::memory_order_relaxed);
    }
  };
}