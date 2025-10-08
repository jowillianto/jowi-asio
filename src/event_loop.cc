module;
#include <atomic>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>
export module jowi.asio:event_loop;
import jowi.asio.lockfree;
import :awaitable;
import :task;

namespace jowi::asio {
  // template <class T> using shared_ptr = std::shared_ptr<T>;
  template <class T> using atomic_shared_ptr = std::atomic<shared_ptr<T>>;

  /*
    unique_ptr and atomic_shared_ptr here is nullable (don't forget)
  */
  template <class T> struct lf_queue_node {
    std::optional<T> v;
    atomic_shared_ptr<lf_queue_node<T>> next;

    lf_queue_node(std::optional<T> v, asio::shared_ptr<lf_queue_node<T>> n) :
      v{std::move(v)}, next{std::move(n)} {}

    template <class... Args>
      requires(std::constructible_from<T, Args...>)
    lf_queue_node(Args &&...args) : v{std::forward<Args>(args)...}, next{nullptr} {}

    bool has_value() const noexcept {
      return static_cast<bool>(v);
    }

    static lf_queue_node dummy() {
      return lf_queue_node{std::nullopt, nullptr};
    }
  };

  template <class T> struct lf_queue {
  public:
    using value_type = lf_queue_node<T>;

  private:
    lf_queue_node<T> __head;
    atomic_shared_ptr<lf_queue_node<T>> __tail;
    std::atomic<size_t> __size;

    /*
      Insert in front into the queue in michael scott style.
    */
    void __push_back(shared_ptr<lf_queue_node<T>> node) noexcept {
      /*
        Over here, we are protecting the invariant that the tail is the node which nextnode
        is a nullptr. Hence, at all times, we are going to find such a node.
        __tail serves as a starting point and as a cache.
      */
      auto tail = __tail.load(memory_order_strict);
      auto tail_next = shared_ptr<lf_queue_node<T>>{nullptr};
      // Check if the loaded tail is a real tail.
      while (!tail->next.compare_exchange_weak(tail_next, node, memory_order_strict)) {
        // This means, the next node is not null, we need to move the tail node.
        tail = std::move(tail_next);
      }
      // These need not be strongly ordered because who cares.
      __tail.store(node, memory_order_relaxed);
      __size.fetch_add(1, std::memory_order_relaxed);
    }

    std::optional<shared_ptr<value_type>> __pop_front() noexcept {
      /*
        we are performing a swap of head.next to head.next.next
      */
      auto head_next = __head.next.load(memory_order_strict);
      if (!head_next) return std::nullopt;
      auto head_next_next = __head.next.load(memory_order_strict);
      while (!__head.next.compare_exchange_weak(head_next, head_next_next, memory_order_strict)) {
        if (!head_next) {
          return std::nullopt;
        }
      }
      if (!head_next_next) {
        __tail.store(__get_resetted_tail(), memory_order_strict);
      }
      // No more contention, detached.
      head_next->next.store(nullptr, memory_order_relaxed);
      __size.fetch_sub(1, std::memory_order_relaxed);
      return head_next;
    }

    shared_ptr<lf_queue_node<T>> __get_resetted_tail() {
      return shared_ptr<lf_queue_node<T>>{&__head, [](lf_queue_node<T> *) {}};
    }

  public:
    lf_queue() : __head{lf_queue_node<T>::dummy()}, __tail{__get_resetted_tail()} {}

    template <class... Args>
      requires(std::constructible_from<T, Args...>)
    void emplace_back(Args &&...args) {
      __push_back(asio::make_shared<value_type>(std::forward<Args>(args)...));
    }

    std::optional<T> pop_front() noexcept {
      return __pop_front().and_then([](shared_ptr<value_type> ptr) { return ptr->v; });
    }
  };

  template <class T, class D = std::default_delete<T>>
    requires(std::constructible_from<D>)
  struct lf_ringbuf_node {
    std::atomic<T *> value;

    lf_ringbuf_node() : value{nullptr} {}

    /* Gets ownership of the node pointer. */
    std::optional<std::unique_ptr<T, D>> load() noexcept {
      T *cur_value = value.exchange(nullptr, std::memory_order_relaxed);
      if (cur_value == nullptr) {
        return std::nullopt;
      }
      return std::unique_ptr<T, D>{cur_value, D{}};
    }

    /* Force giving ownership into the node, if successful, returns the value. */
    std::optional<std::unique_ptr<T, D>> store(std::unique_ptr<T, D> v) noexcept {
      T *cur_value = value.exchange(v.get(), std::memory_order_relaxed);
      if (cur_value == nullptr) {
        return std::nullopt;
      }
      v.release();
      return std::unique_ptr<T, D>{cur_value, D{}};
    }

    /*
      Try to give ownership into the node. If the node contains value, return the ownership back
      to the caller.
    */
    std::optional<std::unique_ptr<T, D>> strong_try_store(std::unique_ptr<T> v) noexcept {
      T *ptr = nullptr;
      bool res = value.compare_exchange_strong(
        ptr, v.get(), std::memory_order_acq_rel, std::memory_order_acquire
      );
      if (res) {
        v.release();
        return std::nullopt;
      } else {
        return v;
      }
    }
    /*
      Try to give ownership into the node. If the node contains value, return the ownership back
      to the caller.
    */
    std::optional<std::unique_ptr<T, D>> weak_try_store(std::unique_ptr<T> v) noexcept {
      T *ptr = nullptr;
      bool res = value.compare_exchange_weak(
        ptr, v.get(), std::memory_order_acq_rel, std::memory_order_acquire
      );
      if (res) {
        v.release();
        return std::nullopt;
      } else {
        return v;
      }
    }

    ~lf_ringbuf_node() {
      load();
    }
  };
  template <class T, class D = std::default_delete<T>> struct lf_ringbuf_queue {
  private:
    std::vector<lf_ringbuf_node<T, D>> __v;
    std::atomic<uint64_t> __beg;
    std::atomic<uint64_t> __end;

  public:
    lf_ringbuf_queue(size_t max_size = 2048) : __v{max_size}, __beg{0}, __end{0} {}

    /*
      Creates a new node and inserts it into the queue, this call will block if the queue is full.
      Given a big enough queue however, this call will never block.
    */
    void emplace_back(std::unique_ptr<T, D> v) noexcept {
      /* Obtain a node index to poll on. */
      uint64_t beg = 0;
      while (!__beg.compare_exchange_weak(
        beg, (beg + 1) % size(), std::memory_order_acq_rel, std::memory_order_acquire
      )) {
      }
      /*
        poll insert. That's why ensure that the other end does not block.
      */
      std::optional<std::unique_ptr<T, D>> to_insert{std::move(v)};
      while (to_insert) {
        to_insert = __v[beg].weak_try_store(std::move(to_insert).value());
      }
    }

    /*
      Try to get a node from the queue, this call will never block.
    */
    std::optional<std::unique_ptr<T, D>> pop_front() {
      uint64_t end = 0;
      while (!__end.compare_exchange_weak(
        end, (end + 1) % size(), std::memory_order_acq_rel, std::memory_order_acquire
      )) {
      }
      return __v[end].load();
    }

    /*
      Get the size of the ring buffer
    */
    uint64_t size() const noexcept {
      return __v.size();
    }
  };
  template struct lf_ringbuf_queue<int>;

  using thread_id = decltype(std::this_thread::get_id());

  template <class T>
  concept event_container = requires(T cont, std::unique_ptr<std::coroutine_handle<void>> n) {
    { cont.emplace_back(n) };
    {
      cont.pop_front()
    } -> std::same_as<std::optional<std::unique_ptr<std::coroutine_handle<void>>>>;
  };

  struct event_loop {
  private:
    static std::unordered_map<thread_id, std::shared_ptr<event_loop>> __store;
    using container_type = lf_queue<std::coroutine_handle<void>>;
    container_type __tasks;

  public:
    template <class... Args>
      requires(std::constructible_from<container_type, Args...>)
    event_loop(Args &&...args) : __tasks{std::forward<Args>(args)...} {}

    void push_back(std::coroutine_handle<void> h) {
      __tasks.emplace_back(h);
    }

    std::optional<std::coroutine_handle<void>> pop_front() {
      return __tasks.pop_front();
    }

    /*
      These are commonly used helper functions.
    */
    template <class F, class... Args>
      requires(std::is_invocable_r_v<bool, F, Args...>)
    void run_until(F f, Args &...args) {
      auto task = pop_front();
      while (task && !std::invoke(f, args...)) {
        task->resume();
        // if (!task->done()) {
        //   push_back(std::move(task).value());
        // }
        task = pop_front();
      }
    }

    void run() {
      std::optional<std::coroutine_handle<void>> task = pop_front();
      while (task) {
        task->resume();
        // if (!task->done()) {
        //   push_back(std::move(task).value());
        // }
        task = pop_front();
      }
    }
    /*
      These are globals
    */
    static std::optional<std::shared_ptr<event_loop>> get_event_loop(
      thread_id id = std::this_thread::get_id()
    ) {
      auto it = __store.find(id);
      if (it == __store.end()) {
        return std::nullopt;
      }
      return it->second;
    }

    template <class... Args>
      requires(std::constructible_from<event_loop, Args...>)
    static std::shared_ptr<event_loop> get_or_create_event_loop(thread_id id, Args &&...args) {
      return get_event_loop(id)
        .or_else([&]() {
          auto loop = std::make_shared<event_loop>(std::forward<Args>(args)...);
          event_loop::register_event_loop(loop);
          return std::optional{loop};
        })
        .value();
    }

    static std::shared_ptr<event_loop> require_event_loop(
      thread_id id = std::this_thread::get_id()
    ) {
      return get_event_loop(id).value();
    }

    static bool register_event_loop(
      std::shared_ptr<event_loop> loop, thread_id id = std::this_thread::get_id()
    ) {
      auto cur_loop = get_event_loop(id);
      if (cur_loop) {
        return false;
      }
      __store.emplace(id, loop);
      return true;
    }
    static bool unregister_event_loop(thread_id id = std::this_thread::get_id()) {
      auto it = __store.find(id);
      if (it == __store.end()) {
        return false;
      }
      __store.erase(it);
      return true;
    }
  };

  std::unordered_map<thread_id, std::shared_ptr<event_loop>> event_loop::__store{};

  // template <class T> struct event;

  // template <> struct event<void> {
  //   virtual void await_ready() = 0;
  //   virtual bool is_blocking() = 0;
  //   virtual void await_suspend() = 0;
  // };

  // template <class T> struct event<T> {};

  // struct lf_queue {};

  // struct event_loop {};
}
