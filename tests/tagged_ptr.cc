#include <jowi/test_lib.hpp>
#include <cstdint>
#include <memory>
import jowi.test_lib;
import jowi.asio.lockfree;

namespace test_lib = jowi::test_lib;
namespace asio = jowi::asio;

JOWI_SETUP(argc, argv) {
  test_lib::get_test_context().set_time_unit(test_lib::TestTimeUnit::MILLI_SECONDS);
}

JOWI_ADD_TEST(TaggedPtr_basic_roundtrip) {
  int value = 42;
  auto tagged = asio::Uint16TaggedPtr::from_pair(&value, static_cast<uint16_t>(0x1234));
  auto pair = tagged.to_pair<int>();
  test_lib::assert_equal(pair.first, &value);
  test_lib::assert_equal(pair.second, static_cast<uint16_t>(0x1234));

  auto raw_pair = tagged.to_raw_pair();
  test_lib::assert_equal(raw_pair.first, &value);
  test_lib::assert_equal(raw_pair.second, static_cast<uint16_t>(0x1234));

  auto rebuilt = asio::Uint16TaggedPtr::from_pair(raw_pair.first, raw_pair.second);
  auto rebuilt_pair = rebuilt.to_pair<int>();
  test_lib::assert_equal(rebuilt_pair.first, &value);
  test_lib::assert_equal(rebuilt_pair.second, static_cast<uint16_t>(0x1234));

  auto null_tagged = asio::Uint16TaggedPtr::null_tag(&value);
  auto null_pair = null_tagged.to_pair<int>();
  test_lib::assert_equal(null_pair.first, &value);
  test_lib::assert_equal(null_pair.second, static_cast<uint16_t>(0));

  auto null = asio::Uint16TaggedPtr::null();
  test_lib::assert_equal(null.ptr<int>(), nullptr);
  test_lib::assert_equal(null.tag(), static_cast<uint16_t>(0));
}

JOWI_ADD_TEST(TaggedPtr_struct_tag) {
  struct small_tag {
    uint8_t payload;
  };
  using TaggedPtr = asio::TaggedPtr<small_tag>;
  int box = 7;
  auto tagged = TaggedPtr::from_pair(&box, small_tag{12});
  auto pair = tagged.to_pair<int>();
  test_lib::assert_equal(pair.first, &box);
  test_lib::assert_equal(pair.second.payload, static_cast<uint8_t>(12));

  auto rebuilt = TaggedPtr::from_pair(pair.first, pair.second);
  auto rebuilt_pair = rebuilt.to_pair<int>();
  test_lib::assert_equal(rebuilt_pair.first, &box);
  test_lib::assert_equal(rebuilt_pair.second.payload, static_cast<uint8_t>(12));
}

JOWI_ADD_TEST(TaggedPtr_bool_tag) {
  int item = 99;
  auto tagged = asio::BoolTaggedPtr::from_pair(&item, true);
  auto pair = tagged.to_pair<int>();
  test_lib::assert_equal(pair.first, &item);
  test_lib::assert_true(pair.second);

  auto false_tagged = asio::BoolTaggedPtr::from_pair(&item, false);
  test_lib::assert_false(false_tagged.tag());
  test_lib::assert_equal(false_tagged.ptr<int>(), &item);
}

JOWI_ADD_TEST(TaggedPtr_size_contract) {
  test_lib::assert_equal(asio::Uint16TaggedPtr::tag_size(), static_cast<uint8_t>(2));
  test_lib::assert_equal(asio::Uint16TaggedPtr::ptr_size(), static_cast<uint8_t>(6));
  test_lib::assert_equal(asio::Uint16TaggedPtr::tag_bit_size(), static_cast<uint32_t>(16));
  test_lib::assert_equal(asio::Uint16TaggedPtr::ptr_bit_size(), static_cast<uint32_t>(48));
  test_lib::assert_equal(asio::TaggedPtr<bool>::tag_size(), asio::Uint16TaggedPtr::tag_size());
  test_lib::assert_equal(
    asio::TaggedPtr<int16_t>::ptr_size(), asio::Uint16TaggedPtr::ptr_size()
  );
}

JOWI_ADD_TEST(TaggedPtr_heap_store) {
  auto ptr = std::make_unique<int>(10);
  auto tagged = asio::BoolTaggedPtr::from_pair(ptr.get(), true);
  test_lib::assert_equal(tagged.raw_ptr(), ptr.get());
}

// NOTE MIGHT BREAK COMPUTER MEMORY
JOWI_ADD_TEST(TaggedPtr_allocate_a_lot) {
  auto n_count = test_lib::random_integer(100'000, 1'000'000);
  auto ptr = std::make_unique<int[]>(n_count);
  auto tagged = asio::BoolTaggedPtr::from_pair(ptr.get(), true);
  test_lib::assert_equal(tagged.raw_ptr(), ptr.get());
}
