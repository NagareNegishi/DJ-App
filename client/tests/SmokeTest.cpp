// Proves the dj-app-tests target compiles, links Catch2, and is run by CTest.
#include <catch2/catch_test_macros.hpp>

TEST_CASE("smoke: test harness runs", "[smoke]")
{
    REQUIRE(1 + 1 == 2);
}
