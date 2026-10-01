// Catch2 supplies main() via Catch2WithMain.
// This file exists so the test target builds before the first TEST_CASE lands.
// Do not add test cases here; one file per unit under test (test_crc.cpp, test_protocol.cpp, ...).
#include <catch2/catch_test_macros.hpp>
