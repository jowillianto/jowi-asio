module;
#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>
export module jowi.asio.lockfree:shared_ptr;
import :tagged_ptr;

namespace jowi::asio {
  struct alloc_data {
  private:
    void *__data;
    std::atomic<uint64_t> __rcount;
    void (*__deleter)(void *);

    struct __deallocator {
      void operator()(alloc_data *ptr) {
        if (ptr == nullptr) return;
        uint64_t cur_rcount = 1;
        uint64_t des_rcount = 0;
        while (!(ptr->__rcount)
                  .compare_exchange_weak(
                    cur_rcount, des_rcount, std::memory_order_acq_rel, std::memory_order_acquire
                  )) {
          des_rcount = cur_rcount - 1;
        }
        if (cur_rcount == 1 && des_rcount == 0) {
          ptr->__deleter(ptr->__data);
          ptr->__data = nullptr;
          std::default_delete<alloc_data>{}(ptr);
        }
      }
    };

  public:
    template <class T>
    alloc_data(T *data, void (*deleter)(T *)) :
      __data{static_cast<void *>(data)}, __rcount{1},
      __deleter{reinterpret_cast<void (*)(void *)>(deleter)} {}

    template <class T> inline T *get() const noexcept {
      return static_cast<T *>(__data);
    }

    template <class T>
    static std::unique_ptr<alloc_data, __deallocator> allocate(T *ptr, void (*deleter)(T *)) {
      return std::unique_ptr<alloc_data, __deallocator>{
        std::make_unique<alloc_data>(ptr, deleter).release(), __deallocator{}
      };
    }

    inline static std::unique_ptr<alloc_data, __deallocator> copy(
      const std::unique_ptr<alloc_data, __deallocator> &ptr
    ) noexcept {
      if (!ptr) return std::unique_ptr<alloc_data, __deallocator>{nullptr, __deallocator{}};
      auto prev_value = ptr->__rcount.fetch_add(1, std::memory_order_relaxed);
      return std::unique_ptr<alloc_data, __deallocator>{ptr.get(), __deallocator{}};
    }

    /*
     * this will take ownership of alloc data without increasing the ref count. to increase the ref
     * count call steal_and_leak.
     */
    inline static std::unique_ptr<alloc_data, __deallocator> steal(alloc_data *data) noexcept {
      return std::unique_ptr<alloc_data, __deallocator>{data, __deallocator{}};
    }

    inline static std::unique_ptr<alloc_data, __deallocator> steal_and_leak(
      alloc_data *data
    ) noexcept {
      auto ptr = steal(data);
      auto new_ptr = copy(ptr);
      auto _ = ptr.release();
      return new_ptr;
    }

    inline static void dealloc(alloc_data *data) noexcept {
      auto _ = steal(data);
    }

    using managed_type = std::unique_ptr<alloc_data, __deallocator>;
  };

  export template <class T> struct shared_ptr {
  private:
    using alloc_ptr_type = alloc_data::managed_type;
    friend std::atomic<shared_ptr<T>>;
    alloc_ptr_type __ptr;

    // Non public constructor. Never publicly use.
    shared_ptr(alloc_ptr_type ptr) : __ptr{std::move(ptr)} {}

    inline alloc_data *__raw_ptr() const noexcept {
      return __ptr.get();
    }
    inline alloc_data *__release() noexcept {
      return __ptr.release();
    }

  public:
    shared_ptr(T *ptr, void (*deleter)(T *)) {
      if (ptr == nullptr) return;
      __ptr = asio::alloc_data::allocate(ptr, deleter);
    }
    shared_ptr(std::nullptr_t ptr) {
      __ptr = ptr;
    }
    shared_ptr(const shared_ptr &o) {
      __ptr = alloc_data::copy(o.__ptr);
    }
    shared_ptr(shared_ptr &&o) noexcept {
      __ptr = std::move(o.__ptr);
    }
    shared_ptr &operator=(const shared_ptr &o) {
      __ptr = alloc_data::copy(o.__ptr);
      return *this;
    }
    shared_ptr &operator=(shared_ptr &&o) noexcept {
      __ptr = std::move(o.__ptr);
      return *this;
    }

    operator bool() const noexcept {
      return __ptr != nullptr;
    }

    T *get() const noexcept {
      if (__ptr == nullptr) return nullptr;
      return static_cast<T *>(__ptr->get<T>());
    }

    T *operator->() const noexcept {
      return get();
    }
    const T &operator*() const noexcept {
      return *get();
    }
    T &operator*() noexcept {
      return *get();
    }
    void reset() {
      __ptr.reset();
    }
    void release() {
      auto _ = __ptr.release();
    }

    friend bool operator==(const shared_ptr &l, const shared_ptr &r) {
      return l.__ptr == r.__ptr;
    }
  };

  export template <class T, class... Args>
    requires(std::constructible_from<T, Args...>)
  asio::shared_ptr<T> make_shared(Args &&...args) {
    return asio::shared_ptr<T>{new T{std::forward<Args>(args)...}, [](T *ptr) { delete ptr; }};
  }
}

namespace asio = jowi::asio;

template <class T> struct std::atomic<asio::shared_ptr<T>> {
private:
  mutable std::atomic<asio::tagged_ptr<uint16_t>> __ptr;
  using tagged_ptr = asio::tagged_ptr<uint16_t>;

  // This function allows any thread to do the work to increase ref count.
  // This create a
  /*
   * Guarantees that tag will be zero after run. Used in exchange().
   * compare_exchange will provide its own implementation that will check every loop.
   */
  inline tagged_ptr __drop_all(
    tagged_ptr cur_ptr = tagged_ptr::from_pair(nullptr, 1),
    asio::memory_order m = asio::memory_order_seq_cst
  ) const noexcept {
    while (cur_ptr.tag() != 0) {
      cur_ptr = __drop_one(cur_ptr, m);
    }
    return cur_ptr;
  }

  inline tagged_ptr __drop_one(tagged_ptr cur_ptr, asio::memory_order m) const {
    tagged_ptr desired_ptr = tagged_ptr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() - 1);
    auto stolen_ptr = asio::alloc_data::steal_and_leak(cur_ptr.ptr<asio::alloc_data>());
    while (!__ptr.compare_exchange_weak(cur_ptr, desired_ptr, m)) {
      // someone stored a new pointer, we have to relinquish the stolen pointer.
      // someone made the tag zero, we need to return our current copy.
      if (cur_ptr.raw_ptr() != desired_ptr.raw_ptr() || cur_ptr.tag() == 0) {
        stolen_ptr.reset();
        // at this point, it is okay to even return since someone did our job.
        return cur_ptr;
      } else {
        // try again since, no one has done anything to the pointer.
        desired_ptr = tagged_ptr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() - 1);
      }
    }
    // on successful set, leak the pointer. in zero or changed case, the pointer itself is already
    // empty.
    auto _ = stolen_ptr.release();
    return desired_ptr;
  }

public:
  atomic(asio::shared_ptr<T> ptr) noexcept : __ptr{tagged_ptr::from_pair(ptr.__ptr.release(), 0)} {}
  template <class... Args>
    requires(std::constructible_from<asio::shared_ptr<T>, Args...>)
  atomic(Args &&...args) : atomic(asio::shared_ptr<T>{std::forward<Args>(args)...}) {}

  asio::shared_ptr<T> load(asio::memory_order m = asio::memory_order::sequential) const noexcept {
    /*
      we need to force a ref count increase over here. i.e. load, increase ref count and make these
      two operation atomic. We can do this by increasing ref count and decreasing the ref count. We
      call this the deferred ref count. storing or changing can only be done after all deferred
      count have been resolved.
    */
    // 1. Let defer ref count first.
    tagged_ptr cur_ptr = tagged_ptr::null();
    tagged_ptr desired_ptr = tagged_ptr::from_pair(nullptr, 1);
    while (!__ptr.compare_exchange_weak(cur_ptr, desired_ptr, m)) {
      desired_ptr = tagged_ptr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() + 1);
    }
    // 2. Now we have a safety measure. Now Increase ref count on the loaded pointer.
    // Any call to modify this pointer will perform the work to increase the ref count.
    // ah damn. we still need to perform the ritual of getting the ref to zero. This can be
    // scheduled out and other threads might have performed this first.
    __drop_one(desired_ptr, m);
    return asio::shared_ptr<T>{
      std::move(asio::alloc_data::steal(desired_ptr.ptr<asio::alloc_data>()))
    };
  }

  asio::shared_ptr<T> exchange(
    asio::shared_ptr<T> desired, asio::memory_order m = asio::memory_order::sequential
  ) noexcept {
    /*
      exchange is easy to implement as we do not need to force a ref count increase. Otherwise,
      we need to force a ref count increase. It is a requirement that a thread cannot be loading
      from this pointer when trying to exchange.
    */
    tagged_ptr cur_ptr = tagged_ptr::null();
    tagged_ptr desired_ptr = tagged_ptr::from_pair(desired.__release(), 0);
    while (!__ptr.compare_exchange_weak(cur_ptr, desired_ptr, m)) {
      // steal work
      cur_ptr = __drop_all(cur_ptr, m);
      // no steal
      // cur_ptr = tagged_ptr::from_pair(cur_ptr.raw_ptr(), 0);
    }
    return asio::shared_ptr<T>{asio::alloc_data::steal(cur_ptr.ptr<asio::alloc_data>())};
  }
  inline void store(
    asio::shared_ptr<T> desired, asio::memory_order m = asio::memory_order::sequential
  ) noexcept {
    exchange(desired, m);
  }
  bool compare_exchange(
    asio::shared_ptr<T> &e, asio::shared_ptr<T> d, asio::memory_order m = asio::memory_order_seq_cst
  ) noexcept {
    // Otherwise, we will perform the deed.
    tagged_ptr cur_ptr = tagged_ptr::from_pair(e.__raw_ptr(), 0);
    tagged_ptr desired_ptr = tagged_ptr::from_pair(d.__raw_ptr(), 0);
    while (!__ptr.compare_exchange_weak(cur_ptr, desired_ptr, m)) {
      /*
       * Work stealing.
       */
      // Load pointer from memory instead.
      while (cur_ptr.tag() != 0 && cur_ptr.raw_ptr() == e.__raw_ptr()) {
        cur_ptr = __drop_one(cur_ptr, m);
      }
      if (cur_ptr.raw_ptr() == e.__raw_ptr() && cur_ptr.tag() == 0) {
        cur_ptr = tagged_ptr::from_pair(e.__raw_ptr(), 0);
        desired_ptr = tagged_ptr::from_pair(d.__raw_ptr(), 0);
      } else {
        desired_ptr = tagged_ptr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() + 1);
      }
      // no steal
      // auto ptr = cur_ptr.ptr<asio::alloc_data>();
      // if (ptr == e.__raw_ptr()) {
      //   cur_ptr = tagged_ptr::from_pair(e.__raw_ptr(), 0);
      //   desired_ptr = tagged_ptr::from_pair(d.__raw_ptr(), 0);
      // } else {
      //   desired_ptr = tagged_ptr::from_pair(ptr, cur_ptr.tag() + 1);
      // }
    }
    // On loop exit, two conditions could happen:
    // 1. a load (a load)
    // 2. a cas (if desired_ptr == d.__ptr, it is a cas)
    // CAS check can be done with the tag, a nonzero tag should indicate that it is only a ref count
    // increase.
    if (desired_ptr.tag() == 0) {
      // since it is a CAS, ignore e and proceed with leaking d.
      // and freeing the current pointer.
      asio::alloc_data::dealloc(cur_ptr.ptr<asio::alloc_data>());
      d.__release();
      return true;
    }
    // load case. Read from desired_ptr and preform operations.
    e = asio::shared_ptr<T>{asio::alloc_data::steal(desired_ptr.ptr<asio::alloc_data>())};
    __drop_one(desired_ptr, m);
    return false;
  }

  inline bool compare_exchange_weak(
    asio::shared_ptr<T> &e, asio::shared_ptr<T> d, asio::memory_order m = asio::memory_order_seq_cst
  ) noexcept {
    return compare_exchange(e, d, m);
  }

  inline bool compare_exchange_strong(
    asio::shared_ptr<T> &e, asio::shared_ptr<T> d, asio::memory_order m = asio::memory_order_seq_cst
  ) noexcept {
    return compare_exchange(e, d, m);
  }

  inline ~atomic() {
    // deallocate current
    exchange(nullptr, asio::memory_order_relaxed);
  }
};

template struct std::atomic<asio::shared_ptr<int>>;
