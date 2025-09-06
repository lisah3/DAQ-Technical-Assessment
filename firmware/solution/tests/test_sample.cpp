#include <catch2/catch_test_macros.hpp>

// Dummy function to test 
int dummy(int num) {
    return 0;
}

// test dummy function 
TEST_CASE("dummy function returns 0", "[dummy]") {
    REQUIRE(dummy(1) == 0);
    REQUIRE(dummy(2) == 0);
    REQUIRE(dummy(10) == 0);
}
