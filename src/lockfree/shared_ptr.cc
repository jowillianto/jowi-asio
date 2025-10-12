module;
#include <atomic>
#include <cassert>
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

    uint64_t ref_count() const noexcept {
      return __rcount.load(std::memory_order_relaxed);
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
      ptr->__rcount.fetch_add(1, std::memory_order_relaxed);
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
    uint64_t ref_count(std::memory_order m = std::memory_order_relaxed) const noexcept {
      return __ptr->ref_count();
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
  mutable std::atomic<asio::uint16_tagged_ptr> __ptr;
  using tagged_ptr = asio::uint16_tagged_ptr;

  /*
   * __update_ref_count
   * @brief for every load performed, a deferred ref will be added to this atomic pointer, this
   * causes the atomic pointer which already owns a ref to own more than one ref. This functions,
   * throws away the deferred ref back into the shared pointer such that the atomic pointer will now
   * only own one ref. This function exits returning the most current pointer when the tag is zero
   * or if the pointer change
   * @param cur_ptr a tagged pointer with nonzero tag.
   * @param m memory order to use.
   */
  inline void __update_ref_count(tagged_ptr cur_ptr, asio::memory_order m) const {
    tagged_ptr desired_ptr = tagged_ptr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() - 1);
    // load or compare_exchange that loads will directly call this function, which guarantees that
    // the pointer is already deferred.
    auto _ = asio::alloc_data::steal_and_leak(cur_ptr.ptr<asio::alloc_data>()).release();
    while (!__ptr.compare_exchange_weak(cur_ptr, desired_ptr, m)) {
      // try again since, no one has done anything to the pointer.
      desired_ptr = tagged_ptr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() - 1);
    }
    // on successful set, leak the pointer. in zero or changed case, the pointer itself is already
    // empty.
  }

public:
  atomic(asio::shared_ptr<T> ptr) noexcept : __ptr{tagged_ptr::from_pair(ptr.__ptr.release(), 0)} {}
  template <class... Args>
    requires(std::constructible_from<asio::shared_ptr<T>, Args...>)
  atomic(Args &&...args) : atomic(asio::shared_ptr<T>{std::forward<Args>(args)...}) {}

  asio::shared_ptr<T> load(asio::memory_order m = asio::memory_order::sequential) const {
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
    // Any call to modify this pointer will perform the work to increase the ref count. Hence,
    // this ref counting guarantees safety by default.
    // 3. The current thread has to try to perform the work of incresing refs before returning.
    // Other thread could have performed this work, however, this thread has to exit with an
    // INCREASED ref.
    // this ensures that on exit, the reference count is guaranteed to have increased.
    __update_ref_count(desired_ptr, m);
    return asio::shared_ptr<T>{asio::alloc_data::steal(desired_ptr.ptr<asio::alloc_data>())};
  }

  asio::shared_ptr<T> exchange(
    asio::shared_ptr<T> desired, asio::memory_order m = asio::memory_order::sequential
  ) {
    /*
      exchange is easy to implement as we do not need to force a ref count increase. Otherwise,
      we need to force a ref count increase. It is a requirement that a thread cannot be loading
      from this pointer when trying to exchange.
    */
    tagged_ptr cur_ptr = tagged_ptr::null();
    tagged_ptr desired_ptr = tagged_ptr::from_pair(desired.__release(), 0);
    while (!__ptr.compare_exchange_weak(cur_ptr, desired_ptr, m)) {
      // We can only swap if all of the deferred ref count is zero.
      cur_ptr = tagged_ptr::from_pair(cur_ptr.raw_ptr(), 0);
    }
    return asio::shared_ptr<T>{asio::alloc_data::steal(cur_ptr.ptr<asio::alloc_data>())};
  }
  inline void store(
    asio::shared_ptr<T> desired, asio::memory_order m = asio::memory_order::sequential
  ) {
    exchange(desired, m);
  }
  bool compare_exchange(
    asio::shared_ptr<T> &e, asio::shared_ptr<T> d, asio::memory_order m = asio::memory_order_seq_cst
  ) {
    /*
     * Expected condition to CAS, we move from no deferred e.__raw_ptr() and d.__raw_ptr()
     */
    tagged_ptr cur_ptr = tagged_ptr::from_pair(e.__raw_ptr(), 0);
    tagged_ptr desired_ptr = tagged_ptr::from_pair(d.__raw_ptr(), 0);
    while (!__ptr.compare_exchange_weak(cur_ptr, desired_ptr, m)) {
      /*
       * On failure, the following two conditions:
       * 1. if cur_ptr != e, load with tag increase regardless.
       * 2. if cur_ptr == e, loop tag until zero. and try again.
       */
      if (cur_ptr.raw_ptr() != e.__raw_ptr()) {
        desired_ptr = tagged_ptr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() + 1);
      } else {
        cur_ptr = tagged_ptr::from_pair(e.__raw_ptr(), 0);
        desired_ptr = tagged_ptr::from_pair(d.__raw_ptr(), 0);
      }
    }
    /*
     * On a loop exit:
     * 1. tag is zero, indicating a successful change of pointer.
     * 2. tag is non zero, indicating a load, deferring the count.
     */
    if (desired_ptr.tag() == 0) {
      asio::alloc_data::dealloc(cur_ptr.ptr<asio::alloc_data>());
      d.__release();
      return true;
    }
    e = asio::shared_ptr<T>{asio::alloc_data::steal(desired_ptr.ptr<asio::alloc_data>())};
    __update_ref_count(desired_ptr, m);
    return false;
  }

  inline bool compare_exchange_weak(
    asio::shared_ptr<T> &e, asio::shared_ptr<T> d, asio::memory_order m = asio::memory_order_seq_cst
  ) {
    return compare_exchange(e, d, m);
  }

  inline bool compare_exchange_strong(
    asio::shared_ptr<T> &e, asio::shared_ptr<T> d, asio::memory_order m = asio::memory_order_seq_cst
  ) {
    return compare_exchange(e, d, m);
  }

  inline ~atomic() {
    /*
     * This forces releasing all the refs.
     */
    store(nullptr, asio::memory_order_strict);
  }
};

template struct std::atomic<asio::shared_ptr<int>>;
