#include "common/fixed_string.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

using signlang::common::copy_fixed_string;
using signlang::common::fixed_string_to_string;

TEST_CASE("fixed strings are copied and null terminated") {
  auto destination = std::array<char, 8>{};

  copy_fixed_string("hello", destination);

  CHECK(fixed_string_to_string(destination) == "hello");
  CHECK(destination[5] == '\0');
}

TEST_CASE("fixed strings are truncated to leave room for a terminator") {
  auto destination = std::array<char, 5>{};

  copy_fixed_string(std::string{"long value"}, destination);

  CHECK(fixed_string_to_string(destination) == "long");
  CHECK(destination.back() == '\0');
}

TEST_CASE("null sources and zero-sized destinations are supported") {
  auto destination = std::array<char, 4>{'x', 'x', 'x', 'x'};
  auto empty_destination = std::array<char, 0>{};

  copy_fixed_string(static_cast<const char*>(nullptr), destination);
  copy_fixed_string("ignored", empty_destination);

  CHECK(fixed_string_to_string(destination).empty());
  CHECK(fixed_string_to_string(empty_destination).empty());
}

TEST_CASE("fixed strings without a terminator use the complete array") {
  const auto value = std::array<char, 4>{'t', 'e', 's', 't'};

  CHECK(fixed_string_to_string(value) == "test");
}
