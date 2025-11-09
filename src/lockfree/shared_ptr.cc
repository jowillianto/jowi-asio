module;
#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>
export module jowi.asio.lockfree:shared_ptr;
import :tagged_ptr;

namespace jowi::asio {
  struct AllocData {
  private:
    void *__data;
    std::atomic<uint64_t> __rcount;
    void (*__deleter)(void *);

    struct Deallocator {
      void operator()(AllocData *ptr) {
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
          std::default_delete<AllocData>{}(ptr);
        }
      }
    };

  public:
    template <class T>
    AllocData(T *data, void (*deleter)(T *)) :
      __data{static_cast<void *>(data)}, __rcount{1},
      __deleter{reinterpret_cast<void (*)(void *)>(deleter)} {}

    template <class T> inline T *get() const noexcept {
      return static_cast<T *>(__data);
    }

    uint64_t ref_count() const noexcept {
      return __rcount.load(std::memory_order_relaxed);
    }

    template <class T>
    static std::unique_ptr<AllocData, Deallocator> allocate(T *ptr, void (*deleter)(T *)) {
      return std::unique_ptr<AllocData, Deallocator>{
        std::make_unique<AllocData>(ptr, deleter).release(), Deallocator{}
      };
    }

    inline static std::unique_ptr<AllocData, Deallocator> copy(
      const std::unique_ptr<AllocData, Deallocator> &ptr
    ) noexcept {
      if (!ptr) return std::unique_ptr<AllocData, Deallocator>{nullptr, Deallocator{}};
      ptr->__rcount.fetch_add(1, std::memory_order_relaxed);
      return std::unique_ptr<AllocData, Deallocator>{ptr.get(), Deallocator{}};
    }

    /*
     * this will take ownership of alloc data without increasing the ref count. to increase the ref
     * count call steal_and_leak.
     */
    inline static std::unique_ptr<AllocData, Deallocator> steal(AllocData *data) noexcept {
      return std::unique_ptr<AllocData, Deallocator>{data, Deallocator{}};
    }

    inline static std::unique_ptr<AllocData, Deallocator> steal_and_leak(AllocData *data) noexcept {
      auto ptr = steal(data);
      auto new_ptr = copy(ptr);
      auto _ = ptr.release();
      return new_ptr;
    }

    inline static void dealloc(AllocData *data) noexcept {
      auto _ = steal(data);
    }

    using managed_type = std::unique_ptr<AllocData, Deallocator>;
  };

  export template <class T> struct SharedPtr {
  private:
    using alloc_ptr_type = AllocData::managed_type;
    friend std::atomic<SharedPtr<T>>;
    alloc_ptr_type __ptr;

    // Non public constructor. Never publicly use.
    SharedPtr(alloc_ptr_type ptr) : __ptr{std::move(ptr)} {}

    inline AllocData *__raw_ptr() const noexcept {
      return __ptr.get();
    }
    inline AllocData *__release() noexcept {
      return __ptr.release();
    }

  public:
    SharedPtr(T *ptr, void (*deleter)(T *)) {
      if (ptr == nullptr) return;
      __ptr = asio::AllocData::allocate(ptr, deleter);
    }
    SharedPtr(std::nullptr_t ptr) {
      __ptr = ptr;
    }
    SharedPtr(const SharedPtr &o) {
      __ptr = AllocData::copy(o.__ptr);
    }
    SharedPtr(SharedPtr &&o) noexcept {
      __ptr = std::move(o.__ptr);
    }
    SharedPtr &operator=(const SharedPtr &o) {
      __ptr = AllocData::copy(o.__ptr);
      return *this;
    }
    SharedPtr &operator=(SharedPtr &&o) noexcept {
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
    const auto &operator*() const noexcept
      requires(!std::same_as<T, void>)
    {
      return *get();
    }
    auto &operator*() noexcept
      requires(!std::same_as<T, void>)
    {
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

    friend bool operator==(const SharedPtr &l, const SharedPtr &r) {
      return l.__ptr == r.__ptr;
    }
  };

  export template <class T, class... Args>
    requires(std::constructible_from<T, Args...>)
  asio::SharedPtr<T> make_shared(Args &&...args) {
    return asio::SharedPtr<T>{new T{std::forward<Args>(args)...}, [](T *ptr) { delete ptr; }};
  }
}

namespace asio = jowi::asio;

template <class T> struct std::atomic<asio::SharedPtr<T>> {
private:
  mutable std::atomic<asio::Uint16TaggedPtr> __ptr;
  using TaggedPtr = asio::Uint16TaggedPtr;

public:
  atomic(asio::SharedPtr<T> ptr) noexcept : __ptr{TaggedPtr::from_pair(ptr.__ptr.release(), 0)} {}
  template <class... Args>
    requires(std::constructible_from<asio::SharedPtr<T>, Args...>)
  atomic(Args &&...args) : atomic(asio::SharedPtr<T>{std::forward<Args>(args)...}) {}

  asio::SharedPtr<T> load(asio::MemoryOrder m = asio::MemoryOrder::sequential) const {
    /*
      we need to force a ref count increase over here. i.e. load, increase ref count and make these
      two operation atomic. We can do this by increasing ref count and decreasing the ref count. We
      call this the deferred ref count. storing or changing can only be done after all deferred
      count have been resolved.
    */
    // 1. Let defer ref count first.
    TaggedPtr cur_ptr = TaggedPtr::null();
    TaggedPtr desired_ptr = TaggedPtr::from_pair(nullptr, 1);
    while (!__ptr.compare_exchange_weak(cur_ptr, desired_ptr, m)) {
      desired_ptr = TaggedPtr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() + 1);
    }
    // 2. Now we have a safety measure. Now Increase ref count on the loaded pointer.
    // Any call to modify this pointer will perform the work to increase the ref count. Hence,
    // this ref counting guarantees safety by default.
    // 3. The current thread has to try to perform the work of incresing refs before returning.
    // Other thread could have performed this work, however, this thread has to exit with an
    // INCREASED ref.
    // this ensures that on exit, the reference count is guaranteed to have increased.
    // __update_ref_count(desired_ptr, m);
    cur_ptr = desired_ptr;
    desired_ptr = TaggedPtr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() - 1);
    auto _ = asio::AllocData::steal_and_leak(cur_ptr.ptr<asio::AllocData>()).release();
    while (!__ptr.compare_exchange_weak(cur_ptr, desired_ptr, m)) {
      // try again since, no one has done anything to the pointer.
      desired_ptr = TaggedPtr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() - 1);
    }
    return asio::SharedPtr<T>{asio::AllocData::steal(desired_ptr.ptr<asio::AllocData>())};
  }

  asio::SharedPtr<T> exchange(
    asio::SharedPtr<T> desired, asio::MemoryOrder m = asio::MemoryOrder::sequential
  ) {
    /*
      exchange is easy to implement as we do not need to force a ref count increase. Otherwise,
      we need to force a ref count increase. It is a requirement that a thread cannot be loading
      from this pointer when trying to exchange.
    */
    TaggedPtr cur_ptr = TaggedPtr::null();
    TaggedPtr desired_ptr = TaggedPtr::from_pair(desired.__release(), 0);
    while (!__ptr.compare_exchange_weak(cur_ptr, desired_ptr, m)) {
      // We can only swap if all of the deferred ref count is zero.
      cur_ptr = TaggedPtr::from_pair(cur_ptr.raw_ptr(), 0);
    }
    return asio::SharedPtr<T>{asio::AllocData::steal(cur_ptr.ptr<asio::AllocData>())};
  }
  inline void store(
    asio::SharedPtr<T> desired, asio::MemoryOrder m = asio::MemoryOrder::sequential
  ) {
    exchange(desired, m);
  }
  bool compare_exchange(
    asio::SharedPtr<T> &e, asio::SharedPtr<T> d, asio::MemoryOrder m = asio::memory_order_seq_cst
  ) {
    /*
     * Expected condition to CAS, we move from no deferred e.__raw_ptr() and d.__raw_ptr()
     */
    TaggedPtr cur_ptr = TaggedPtr::from_pair(e.__raw_ptr(), 0);
    TaggedPtr desired_ptr = TaggedPtr::from_pair(d.__raw_ptr(), 0);
    while (!__ptr.compare_exchange_weak(cur_ptr, desired_ptr, m)) {
      /*
       * On failure, the following two conditions:
       * 1. if cur_ptr != e, load with tag increase regardless.
       * 2. if cur_ptr == e, loop tag until zero. and try again.
       */
      if (cur_ptr.raw_ptr() != e.__raw_ptr()) {
        desired_ptr = TaggedPtr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() + 1);
      } else {
        cur_ptr = TaggedPtr::from_pair(e.__raw_ptr(), 0);
        desired_ptr = TaggedPtr::from_pair(d.__raw_ptr(), 0);
      }
    }
    /*
     * On a loop exit:
     * 1. tag is zero, indicating a successful change of pointer.
     * 2. tag is non zero, indicating a load, deferring the count.
     */
    if (desired_ptr.tag() == 0) {
      asio::AllocData::dealloc(cur_ptr.ptr<asio::AllocData>());
      d.__release();
      return true;
    }
    e = asio::SharedPtr<T>{asio::AllocData::steal(desired_ptr.ptr<asio::AllocData>())};
    cur_ptr = desired_ptr;
    desired_ptr = TaggedPtr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() - 1);
    auto _ = asio::AllocData::steal_and_leak(cur_ptr.ptr<asio::AllocData>()).release();
    while (!__ptr.compare_exchange_weak(cur_ptr, desired_ptr, m)) {
      // try again since, no one has done anything to the pointer.
      desired_ptr = TaggedPtr::from_pair(cur_ptr.raw_ptr(), cur_ptr.tag() - 1);
    }
    return false;
  }

  inline bool compare_exchange_weak(
    asio::SharedPtr<T> &e, asio::SharedPtr<T> d, asio::MemoryOrder m = asio::memory_order_seq_cst
  ) {
    return compare_exchange(e, d, m);
  }

  inline bool compare_exchange_strong(
    asio::SharedPtr<T> &e, asio::SharedPtr<T> d, asio::MemoryOrder m = asio::memory_order_seq_cst
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

template struct std::atomic<asio::SharedPtr<int>>;
