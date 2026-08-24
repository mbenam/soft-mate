// ===========================================================================
// m8_editwatch — watch the cursor while an edit walk runs, to settle #34.
//
// Why this exists
// ---------------
// M8_DRIVER_BUGS.md #34: `set AMP FF` left AMP at FD and moved LIM from 04 to
// 08, and the measurement taken around it had to be discarded. The suspected
// mechanism -- a dropped EDIT modifier turning a coarse step (`value_inc16` =
// EDIT|UP = 0x41) into a bare arrow, i.e. a cursor move -- is inferred, not
// measured. The entry is explicit that a guard aimed at the wrong mechanism is
// worse than none, and that the first step is to reproduce it deliberately.
//
// Reproducing it was blocked by the read model, not by luck. `editValue`
// re-reads through the settle-gated path, which returns only after the repaint
// finishes -- by which time a cursor that slipped has already slipped, the
// walk has continued on the new field, and all the evidence left is the
// endpoint. LiveReader samples without waiting for quiet, so the walk can be
// watched press by press and the exact iteration identified.
//
// This does not call editValue. It replays the gesture editValue would send,
// one press at a time, and reports where the cursor was after each -- so the
// mechanism is observed rather than inferred. It samples between presses too,
// at --sample-ms, because a slip that is corrected before the next press would
// otherwise look like nothing happened.
//
//   m8_editwatch --port COM3 --field AMP --neighbour LIM --steps 16 \
//                --gesture coarse-up --allow-mutation
//
// MUTATES THE DEVICE. It edits the named field, then walks the value back and
// verifies. If the restore does not verify it says so -- reload the project
// from the card, which is lossless as long as nobody saved.
//
// Exit 0 = the walk completed with the cursor never leaving the field.
// Exit 1 = the cursor moved, or the neighbour changed. #34 reproduced; the
//          JSON carries the iteration and the gesture that did it.
// Exit 2 = setup failed (port, screen, field, gestures not pinned).
// ===========================================================================

#include "m8/FieldGuard.h"
#include "m8/FlightRecorder.h"
#include "m8/Gestures.h"
#include "m8/LiveReader.h"
#include "m8/M8Device.h"
#include "m8/Primitives.h"
#include "m8/ScreenModel.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace m8::dev;
using clk = std::chrono::steady_clock;

namespace {

struct Slip {
    int iteration = 0;      // which press it followed (0 = before any press)
    int atMs = 0;
    int cursorRowY = -1;    // where the cursor was found
    int expectedRowY = -1;
};

const char* gestureName(uint8_t m) {
    switch (m) {
        case 0x05: return "value_inc (EDIT|RIGHT)";
        case 0x81: return "value_dec (EDIT|LEFT)";
        case 0x41: return "value_inc16 (EDIT|UP)";
        case 0x21: return "value_dec16 (EDIT|DOWN)";
        default:   return "custom";
    }
}

} // namespace

int main(int argc, char** argv) {
    std::string port = "COM3", field = "AMP", neighbour = "LIM", gesture = "coarse-up";
    int steps = 16, holdMs = 40, sampleMs = 5, gapMs = 60;
    bool allowMutation = false;
    int soakMinutes = 0;
    bool drive = false;
    std::string targetLo = "00", targetHi = "FF";

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if      (!std::strcmp(a, "--port")      && i + 1 < argc) port = argv[++i];
        else if (!std::strcmp(a, "--field")     && i + 1 < argc) field = argv[++i];
        else if (!std::strcmp(a, "--neighbour") && i + 1 < argc) neighbour = argv[++i];
        else if (!std::strcmp(a, "--gesture")   && i + 1 < argc) gesture = argv[++i];
        else if (!std::strcmp(a, "--steps")     && i + 1 < argc) steps = std::atoi(argv[++i]);
        else if (!std::strcmp(a, "--hold-ms")   && i + 1 < argc) holdMs = std::atoi(argv[++i]);
        else if (!std::strcmp(a, "--sample-ms") && i + 1 < argc) sampleMs = std::atoi(argv[++i]);
        else if (!std::strcmp(a, "--gap-ms")    && i + 1 < argc) gapMs = std::atoi(argv[++i]);
        else if (!std::strcmp(a, "--allow-mutation")) allowMutation = true;
        else if (!std::strcmp(a, "--soak") && i + 1 < argc) soakMinutes = std::atoi(argv[++i]);
        else if (!std::strcmp(a, "--drive")) drive = true;
        else if (!std::strcmp(a, "--target-lo") && i + 1 < argc) targetLo = argv[++i];
        else if (!std::strcmp(a, "--target-hi") && i + 1 < argc) targetHi = argv[++i];
        else if (!std::strcmp(a, "--help")) {
            std::printf("usage: m8_editwatch --port COM3 --field AMP --neighbour LIM\n"
                        "                    [--gesture coarse-up|coarse-down|fine-up|fine-down]\n"
                        "                    [--steps 16] [--hold-ms 40] [--sample-ms 5]\n"
                        "                    [--gap-ms 60] --allow-mutation\n");
            return 0;
        } else { std::fprintf(stderr, "unknown argument: %s\n", a); return 2; }
    }

    if (!allowMutation) {
        std::fprintf(stderr, "m8_editwatch edits a field; pass --allow-mutation\n");
        return 2;
    }

    GestureTable& g = getGestures();
    if (!g.loadFromFile("hw_buttons.json") || !g.isReady()) {
        std::fprintf(stderr, "gestures not pinned (hw_buttons.json); run from the repo root\n");
        return 2;
    }

    uint8_t mask = 0, inverse = 0;   // swapped each soak round
    if      (gesture == "coarse-up")   { mask = g.valueInc16; inverse = g.valueDec16; }
    else if (gesture == "coarse-down") { mask = g.valueDec16; inverse = g.valueInc16; }
    else if (gesture == "fine-up")     { mask = g.valueInc;   inverse = g.valueDec;   }
    else if (gesture == "fine-down")   { mask = g.valueDec;   inverse = g.valueInc;   }
    else { std::fprintf(stderr, "unknown --gesture %s\n", gesture.c_str()); return 2; }

    M8Device dev;
    if (!dev.open(port.c_str())) {
        std::fprintf(stderr, "could not open %s\n", port.c_str());
        return 2;
    }

    // ---- setup, through the settle-gated path (nothing is moving yet) ----
    auto r = gotoScreen(dev, Screen::INSTRUMENT);
    if (!r.ok) { std::fprintf(stderr, "goto INSTRUMENT failed\n"); dev.close(); return 2; }
    r = moveCursorTo(dev, field);
    if (!r.ok) { std::fprintf(stderr, "cursor to %s failed\n", field.c_str()); dev.close(); return 2; }

    dev.readSettled(0, 250, 2000);
    const int  homeRowY   = dev.grid().cursorRowY();
    const std::string fieldBase = canonRow(rowTextFor(dev.grid(), field, Screen::INSTRUMENT));
    const std::string neighBase = canonRow(rowTextFor(dev.grid(), neighbour, Screen::INSTRUMENT));
    if (homeRowY < 0 || fieldBase.empty()) {
        std::fprintf(stderr, "could not establish a baseline for %s\n", field.c_str());
        dev.close();
        return 2;
    }
    std::printf("baseline: cursor row y=%d  %s=[%s]  %s=[%s]\n",
                homeRowY, field.c_str(), fieldBase.c_str(),
                neighbour.c_str(), neighBase.c_str());
    std::printf("gesture : 0x%02X  %s   x%d steps, hold %d ms, gap %d ms\n",
                mask, gestureName(mask), steps, holdMs, gapMs);

    // ---- drive mode: soak the REAL editValue ------------------------------
    //
    // Everything measured so far replayed the GESTURE editValue sends. About
    // 7,200 coarse presses across an 8-40 ms hold range and a 12-60 ms gap
    // range produced zero cursor slips, which rules the gesture itself out and
    // leaves the parts only editValue does:
    //
    //   - a readSettled(120, 200, 1200) between every press
    //   - the fine-step phase as it closes on the target
    //   - the direction flips its convergence check produces
    //
    // #34's original event was `set AMP FF` -- a real editValue call in the
    // middle of a measurement sweep. This drives exactly that, alternating
    // between two targets, with the guard armed and the recorder running.
    // editValue dumps editvalue_drift.json itself the moment the cursor leaves
    // the field; this additionally watches the NEIGHBOUR, which is what #34
    // actually damaged.
    if (drive) {
        getFlightRecorder().start();
        getFlightRecorder().recordNote("editvalue soak start");
        const auto until = clk::now() + std::chrono::minutes(soakMinutes > 0 ? soakMinutes : 1);
        int calls = 0, failures = 0;
        std::string lastError;

        while (clk::now() < until) {
            const std::string want = (calls % 2 == 0) ? targetHi : targetLo;
            auto res = editValue(dev, field, want, holdMs);
            ++calls;
            if (!res.ok) {
                ++failures;
                lastError = res.error;
                std::fprintf(stderr, "\n! editValue failed on call %d: %s\n", calls,
                             res.error.c_str());
                break;
            }
            dev.readSettled(0, 200, 1200);
            const std::string nb = canonRow(rowTextFor(dev.grid(), neighbour, Screen::INSTRUMENT));
            if (!nb.empty() && !neighBase.empty() && nb != neighBase) {
                getFlightRecorder().recordNote("neighbour changed");
                getFlightRecorder().dump("editvalue_soak_drift.json",
                    "neighbour changed during an editValue soak (#34)");
                std::fprintf(stderr,
                    "\n! CAUGHT IT: %s moved from [%s] to [%s] after %d calls\n"
                    "!   flight recorder -> editvalue_soak_drift.json\n\n",
                    neighbour.c_str(), neighBase.c_str(), nb.c_str(), calls);
                ++failures;
                break;
            }
            if (calls % 10 == 0)
                std::fprintf(stderr, "  %d editValue calls, %s still [%s]\n",
                             calls, neighbour.c_str(), nb.c_str());
        }

        getFlightRecorder().stop();
        std::printf("{\n  \"mode\": \"drive\",\n  \"field\": \"%s\",\n"
                    "  \"neighbour\": \"%s\",\n  \"calls\": %d,\n  \"failures\": %d,\n"
                    "  \"reproduced_34\": %s\n}\n",
                    field.c_str(), neighbour.c_str(), calls, failures,
                    failures ? "true" : "false");
        if (!lastError.empty())
            std::fprintf(stderr, "last error: %s\n", lastError.c_str());
        dev.close();
        return failures ? 1 : 0;
    }

    // ---- the walk, watched -----------------------------------------------
    LiveReader live(dev);
    if (!live.start()) { std::fprintf(stderr, "LiveReader refused\n"); dev.close(); return 2; }

    std::vector<Slip> slips;
    std::string neighSaw;
    bool neighbourMoved = false;
    int  pressesDone = 0;
    const auto t0 = clk::now();

    // Soak: repeat the walk up and back down until the clock runs out.
    //
    // #34 was seen once in a long session and has not been reproduced in 288
    // deliberate presses, so the only realistic way to catch it is volume --
    // and the flight recorder means the evidence is already captured when it
    // finally fires, rather than needing someone watching at the moment.
    // --soak 0 (the default) runs a single walk, as before.
    getFlightRecorder().start();
    getFlightRecorder().recordNote("editwatch start");
    const auto soakUntil = clk::now() + std::chrono::minutes(soakMinutes);
    int soakRounds = 0;

    do {
    if (soakMinutes > 0) {
        ++soakRounds;
        // Alternate direction each round so the value walks up, then back down,
        // and the field stays inside its range instead of pinning at FF.
        if (soakRounds > 1) std::swap(mask, inverse);
        std::fprintf(stderr, "soak round %d (%d min budget)\n", soakRounds, soakMinutes);
    }

    for (int i = 1; i <= steps; ++i) {
        dev.press(mask, holdMs);
        ++pressesDone;

        // Sample densely across the gap. A slip that is corrected before the
        // next press still happened, and sampling only once per press would
        // miss it -- which is one way this bug stayed invisible.
        const auto until = clk::now() + std::chrono::milliseconds(gapMs);
        while (clk::now() < until) {
            const LiveSnapshot s = live.snapshot();
            const int y = s.grid.cursorRowY();
            if (y >= 0 && y != homeRowY) {
                Slip sl;
                sl.iteration = i;
                sl.atMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                              clk::now() - t0).count());
                sl.cursorRowY = y;
                sl.expectedRowY = homeRowY;
                slips.push_back(sl);
            }
            const std::string n = canonRow(rowTextFor(s.grid, neighbour, Screen::INSTRUMENT));
            if (!n.empty() && !neighBase.empty() && n != neighBase && !neighbourMoved) {
                neighbourMoved = true;
                neighSaw = n;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(sampleMs));
        }
    }

    // Stop the soak the moment anything moves: the recorder holds the seconds
    // before it, and continuing would overwrite them.
    if (!slips.empty() || neighbourMoved) {
        getFlightRecorder().recordNote("slip or neighbour drift detected");
        getFlightRecorder().dump("editwatch_drift.json",
                                 "cursor slipped during an edit walk (M8_DRIVER_BUGS.md #34)");
        std::fprintf(stderr, "\n! caught it -- flight recorder written to editwatch_drift.json\n\n");
        break;
    }
    } while (soakMinutes > 0 && clk::now() < soakUntil);

    getFlightRecorder().stop();
    live.stop();

    dev.readSettled(0, 250, 2000);
    const std::string fieldAfter = canonRow(rowTextFor(dev.grid(), field, Screen::INSTRUMENT));
    const std::string neighAfter = canonRow(rowTextFor(dev.grid(), neighbour, Screen::INSTRUMENT));
    const int rowAfter = dev.grid().cursorRowY();

    // ---- put it back ------------------------------------------------------
    std::printf("restoring: %d x 0x%02X\n", pressesDone, inverse);
    for (int i = 0; i < pressesDone; ++i) {
        dev.press(inverse, holdMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(gapMs));
    }
    dev.readSettled(0, 250, 2000);
    const std::string fieldRestored = canonRow(rowTextFor(dev.grid(), field, Screen::INSTRUMENT));
    const std::string neighRestored = canonRow(rowTextFor(dev.grid(), neighbour, Screen::INSTRUMENT));
    const bool restored = (fieldRestored == fieldBase) && (neighRestored == neighBase);

    const bool reproduced = !slips.empty() || neighbourMoved;

    std::printf("{\n");
    std::printf("  \"field\": \"%s\", \"neighbour\": \"%s\",\n", field.c_str(), neighbour.c_str());
    std::printf("  \"gesture\": \"%s\", \"mask\": \"0x%02X\", \"steps\": %d,\n",
                gesture.c_str(), mask, steps);
    std::printf("  \"hold_ms\": %d, \"gap_ms\": %d, \"sample_ms\": %d,\n", holdMs, gapMs, sampleMs);
    std::printf("  \"cursor_home_row_y\": %d, \"cursor_row_y_after\": %d,\n", homeRowY, rowAfter);
    std::printf("  \"field_before\": \"%s\", \"field_after\": \"%s\",\n",
                fieldBase.c_str(), fieldAfter.c_str());
    std::printf("  \"neighbour_before\": \"%s\", \"neighbour_after\": \"%s\",\n",
                neighBase.c_str(), neighAfter.c_str());
    std::printf("  \"cursor_slips\": %zu,\n", slips.size());
    std::printf("  \"slips\": [\n");
    for (size_t i = 0; i < slips.size() && i < 20; ++i)
        std::printf("    {\"iteration\":%d,\"t_ms\":%d,\"cursor_row_y\":%d,\"expected_row_y\":%d}%s\n",
                    slips[i].iteration, slips[i].atMs, slips[i].cursorRowY, slips[i].expectedRowY,
                    (i + 1 < slips.size() && i + 1 < 20) ? "," : "");
    std::printf("  ],\n");
    std::printf("  \"neighbour_moved\": %s,\n", neighbourMoved ? "true" : "false");
    if (neighbourMoved)
        std::printf("  \"neighbour_saw\": \"%s\",\n", neighSaw.c_str());
    std::printf("  \"restored\": %s,\n", restored ? "true" : "false");
    std::printf("  \"reproduced_34\": %s\n", reproduced ? "true" : "false");
    std::printf("}\n");

    if (!restored)
        std::fprintf(stderr,
            "\n! %s did not return to its starting value. Nothing was saved to the\n"
            "!   card, so reloading the project restores it losslessly.\n\n", field.c_str());

    dev.close();
    return reproduced ? 1 : 0;
}
