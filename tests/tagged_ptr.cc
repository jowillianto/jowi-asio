#include <jowi/test_lib.hpp>
#include <cstdint>
#include <memory>
import jowi.test_lib;
import jowi.asio.lockfree;

namespace test_lib = jowi::test_lib;
namespace asio = jowi::asio;

JOWI_SETUP(argc, argv) {
  test_lib::get_test_context().set_time_unit(test_lib::test_time_unit::MILLI_SECONDS);
}

JOWI_ADD_TEST(tagged_ptr_basic_roundtrip) {
  int value = 42;
  auto tagged = asio::uint16_tagged_ptr::from_pair(&value, static_cast<uint16_t>(0x1234));
  auto pair = tagged.to_pair<int>();
  test_lib::assert_equal(pair.first, &value);
  test_lib::assert_equal(pair.second, static_cast<uint16_t>(0x1234));

  auto raw_pair = tagged.to_raw_pair();
  test_lib::assert_equal(raw_pair.first, &value);
  test_lib::assert_equal(raw_pair.second, static_cast<uint16_t>(0x1234));

  auto rebuilt = asio::uint16_tagged_ptr::from_pair(raw_pair.first, raw_pair.second);
  auto rebuilt_pair = rebuilt.to_pair<int>();
  test_lib::assert_equal(rebuilt_pair.first, &value);
  test_lib::assert_equal(rebuilt_pair.second, static_cast<uint16_t>(0x1234));

  auto null_tagged = asio::uint16_tagged_ptr::null_tag(&value);
  auto null_pair = null_tagged.to_pair<int>();
  test_lib::assert_equal(null_pair.first, &value);
  test_lib::assert_equal(null_pair.second, static_cast<uint16_t>(0));

  auto null = asio::uint16_tagged_ptr::null();
  test_lib::assert_equal(null.ptr<int>(), nullptr);
  test_lib::assert_equal(null.tag(), static_cast<uint16_t>(0));
}

JOWI_ADD_TEST(tagged_ptr_struct_tag) {
  struct small_tag {
    uint8_t payload;
  };
  using tagged_ptr = asio::tagged_ptr<small_tag>;
  int box = 7;
  auto tagged = tagged_ptr::from_pair(&box, small_tag{12});
  auto pair = tagged.to_pair<int>();
  test_lib::assert_equal(pair.first, &box);
  test_lib::assert_equal(pair.second.payload, static_cast<uint8_t>(12));

  auto rebuilt = tagged_ptr::from_pair(pair.first, pair.second);
  auto rebuilt_pair = rebuilt.to_pair<int>();
  test_lib::assert_equal(rebuilt_pair.first, &box);
  test_lib::assert_equal(rebuilt_pair.second.payload, static_cast<uint8_t>(12));
}

JOWI_ADD_TEST(tagged_ptr_bool_tag) {
  int item = 99;
  auto tagged = asio::bool_tagged_ptr::from_pair(&item, true);
  auto pair = tagged.to_pair<int>();
  test_lib::assert_equal(pair.first, &item);
  test_lib::assert_true(pair.second);

  auto false_tagged = asio::bool_tagged_ptr::from_pair(&item, false);
  test_lib::assert_false(false_tagged.tag());
  test_lib::assert_equal(false_tagged.ptr<int>(), &item);
}

JOWI_ADD_TEST(tagged_ptr_size_contract) {
  test_lib::assert_equal(asio::uint16_tagged_ptr::tag_size(), static_cast<uint8_t>(2));
  test_lib::assert_equal(asio::uint16_tagged_ptr::ptr_size(), static_cast<uint8_t>(6));
  test_lib::assert_equal(asio::uint16_tagged_ptr::tag_bit_size(), static_cast<uint32_t>(16));
  test_lib::assert_equal(asio::uint16_tagged_ptr::ptr_bit_size(), static_cast<uint32_t>(48));
  test_lib::assert_equal(asio::tagged_ptr<bool>::tag_size(), asio::uint16_tagged_ptr::tag_size());
  test_lib::assert_equal(
    asio::tagged_ptr<int16_t>::ptr_size(), asio::uint16_tagged_ptr::ptr_size()
  );
}

JOWI_ADD_TEST(tagged_ptr_heap_store) {
  auto ptr = std::make_unique<int>(10);
  auto tagged = asio::bool_tagged_ptr::from_pair(ptr.get(), true);
  test_lib::assert_equal(tagged.raw_ptr(), ptr.get());
}

// NOTE MIGHT BREAK COMPUTER MEMORY
JOWI_ADD_TEST(tagged_ptr_allocate_a_lot) {
  auto n_count = test_lib::random_integer(100'000, 1'000'000);
  auto ptr = std::make_unique<int[]>(n_count);
  auto tagged = asio::bool_tagged_ptr::from_pair(ptr.get(), true);
  test_lib::assert_equal(tagged.raw_ptr(), ptr.get());
}
