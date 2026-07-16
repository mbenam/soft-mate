#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string>

// Regression test for the GROOVE screen crash (HexU8 null-pointer UB).
// GrooveScreen.cpp originally called HexU8(grooveData.steps[y], 0) which
// passed integer 0 to const std::string&, constructing from null pointer.
// This crashes when steps[y] == 0xFF (the return-emptyStr branch in HexU8).
// Fixed by changing 0 to "0". This test navigates to GROOVE via --script
// to confirm the crash no longer occurs.

TEST_CASE("Groove screen does not crash", "[ui]") {
    std::string cmd =
        "build\\Release\\m8_clone.exe"
        " --script tests\\ui\\groovetest.m8script"
        " --headless"
        " --out-dir test_out_ui";

    int rc = std::system(cmd.c_str());
    REQUIRE(rc == 0);
}
