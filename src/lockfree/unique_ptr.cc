module;
#include <concepts>
#include <optional>
export module jowi.asio.lockfree:unique_ptr;
import :tagged_ptr;

namespace jowi::asio {

  export template <class T, std::invocable<T> deleter_type>
    requires(taggable<deleter_type, sizeof(T)>)
  struct raii_wrapper {};
}
