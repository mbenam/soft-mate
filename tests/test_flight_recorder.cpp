// ===========================================================================
// test_flight_recorder.cpp — the recorder's artifact, proved without a device.
//
// The recorder exists for M8_DRIVER_BUGS.md #34: a fault seen once, never
// reproduced, and therefore only catchable if the evidence is already captured
// when it finally fires. A 20-minute soak on 2026-08-24 ran 432 rounds --
// roughly 6,900 presses -- and did not trip it, which means the one path that
// matters, writing the artifact, has still never run in anger.
//
// That is exactly the "never seen to fire" trap this session keeps finding, so
// it is exercised here instead: fill the ring, dump it, and read the file back.
// A recorder that quietly fails to write is worse than none, because the run
// that finally catches #34 is the one you cannot repeat.
// ===========================================================================

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "m8/FlightRecorder.h"

using namespace m8::dev;

namespace {

std::string slurp(const std::string& p) {
    std::ifstream f(p);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("the flight recorder writes a readable artifact", "[hwdecode][recorder]") {
    FlightRecorder r;
    r.start();
    REQUIRE(r.running());

    r.recordNote("test start");
    r.recordPress(0x41, 15);                    // EDIT|UP, the coarse gesture
    const uint8_t bytes[] = { 0xFD, 0x41, 0x00, 0xC0 };
    r.recordRaw(bytes, sizeof(bytes));
    r.recordRead(170, 8, 3, true);
    r.recordPress(0x21, 15);                    // EDIT|DOWN

    const std::string path = "test_flight_recorder_out.json";
    REQUIRE(r.dump(path, "unit test"));

    const std::string got = slurp(path);
    std::remove(path.c_str());
    REQUIRE_FALSE(got.empty());

    // Each half of the evidence has to survive the round trip. The raw bytes
    // especially: they are what separates "we sent a bad mask" from "the device
    // did something else" from "our decode is wrong", and those three are
    // indistinguishable once decoded (#32).
    CHECK(got.find("\"reason\": \"unit test\"") != std::string::npos);
    CHECK(got.find("\"kind\":\"press\"") != std::string::npos);
    CHECK(got.find("\"mask\":65") != std::string::npos);      // 0x41
    CHECK(got.find("\"mask\":33") != std::string::npos);      // 0x21
    CHECK(got.find("\"kind\":\"read\"") != std::string::npos);
    CHECK(got.find("\"cursor_row\":170") != std::string::npos);
    CHECK(got.find("\"kind\":\"note\"") != std::string::npos);
    CHECK(got.find("test start") != std::string::npos);
    CHECK(got.find("fd410 0c0") == std::string::npos);        // no stray spacing
    CHECK(got.find("fd4100c0") != std::string::npos);         // raw hex, in order
}

TEST_CASE("the ring keeps the most recent events when it wraps",
          "[hwdecode][recorder]") {
    // The seconds before the fault are the ones worth having, so a full ring
    // must drop the OLDEST entries. Getting this backwards would mean a long
    // soak captures its own beginning and throws away the moment it tripped.
    FlightRecorder r;
    r.start();
    for (size_t i = 0; i < FlightRecorder::kCapacity + 50; ++i)
        r.recordPress(static_cast<uint8_t>(i & 0xFF), 1);

    CHECK(r.eventCount() == FlightRecorder::kCapacity);

    const std::string path = "test_flight_recorder_wrap.json";
    REQUIRE(r.dump(path, "wrap"));
    const std::string got = slurp(path);
    std::remove(path.c_str());

    // The first entry written must be event 50, not event 0.
    const size_t firstBrace = got.find("{\"t_ms\":");
    REQUIRE(firstBrace != std::string::npos);
    const std::string firstRec = got.substr(firstBrace, 80);
    INFO("first record: " << firstRec);
    CHECK(firstRec.find("\"mask\":50") != std::string::npos);
}

TEST_CASE("a stopped recorder records nothing", "[hwdecode][recorder]") {
    // It runs inside the driver's read loop. One that kept recording after
    // stop() would change the timing of the very thing it is meant to observe.
    FlightRecorder r;
    r.start();
    r.recordPress(0x41, 15);
    const size_t afterOne = r.eventCount();
    r.stop();
    r.recordPress(0x21, 15);
    r.recordRead(1, 2, 3, true);
    CHECK_FALSE(r.running());
    CHECK(r.eventCount() == afterOne);
}
