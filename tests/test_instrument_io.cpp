#include <catch2/catch_test_macros.hpp>
#include "io/InstrumentIO.h"
#include <filesystem>

namespace fs = std::filesystem;

TEST_CASE("InstrumentIO save and load MacroSynth .m8i round-trip", "[io]") {
    m8::engine::Instrument orig{};
    orig.type = m8::engine::InstType::INST_MACROSYN;
    m8::engine::setName(orig.name, "LEADSAW");
    auto& ms = orig.macrosyn;
    ms.transp = 1;
    ms.tbl_tic = 0x02;
    ms.shape = 0x19; // FM
    ms.timbre = 0x90;
    ms.color = 0x45;
    ms.degrade = 0x10;
    ms.redux = 0x20;
    ms.amp = 0xC0;
    ms.filter_type = 0x06; // ZDF LP
    ms.cutoff = 0xA0;
    ms.res = 0x60;
    ms.lim = 0x05; // POST:AD
    ms.pan = 0x70;
    ms.dry = 0x80;
    ms.cho = 0x30;
    ms.del = 0x40;
    ms.rev = 0x50;

    // Mod 0 = AHD
    orig.mods[0].type = 0;
    orig.mods[0].dest = 6; // CUTOFF
    orig.mods[0].amt = 0xDF;
    orig.mods[0].p1 = 0x05;
    orig.mods[0].p2 = 0x10;
    orig.mods[0].p3 = 0x40;

    std::string outPath, err;
    fs::path tempDir = fs::temp_directory_path() / "m8_test_inst";
    bool saved = m8::io::saveInstrument(tempDir.generic_string(), orig, std::nullopt, outPath, err);
    REQUIRE(saved);
    REQUIRE(fs::exists(outPath));

    m8::engine::Instrument loaded{};
    std::optional<m8::engine::EqBank> loadedEq;
    bool readOk = m8::io::loadInstrument(outPath, loaded, loadedEq, err);
    INFO(err);
    REQUIRE(readOk);

    REQUIRE(loaded.type == m8::engine::InstType::INST_MACROSYN);
    REQUIRE(std::string(loaded.name).rfind("LEADSAW", 0) == 0);
    REQUIRE(loaded.macrosyn.transp == orig.macrosyn.transp);
    REQUIRE(loaded.macrosyn.tbl_tic == orig.macrosyn.tbl_tic);
    REQUIRE(loaded.macrosyn.shape == orig.macrosyn.shape);
    REQUIRE(loaded.macrosyn.timbre == orig.macrosyn.timbre);
    REQUIRE(loaded.macrosyn.color == orig.macrosyn.color);
    REQUIRE(loaded.macrosyn.degrade == orig.macrosyn.degrade);
    REQUIRE(loaded.macrosyn.redux == orig.macrosyn.redux);
    REQUIRE(loaded.macrosyn.amp == orig.macrosyn.amp);
    REQUIRE(loaded.macrosyn.filter_type == orig.macrosyn.filter_type);
    REQUIRE(loaded.macrosyn.cutoff == orig.macrosyn.cutoff);
    REQUIRE(loaded.macrosyn.res == orig.macrosyn.res);
    REQUIRE(loaded.macrosyn.lim == orig.macrosyn.lim);
    REQUIRE(loaded.macrosyn.pan == orig.macrosyn.pan);
    REQUIRE(loaded.macrosyn.dry == orig.macrosyn.dry);
    REQUIRE(loaded.macrosyn.cho == orig.macrosyn.cho);
    REQUIRE(loaded.macrosyn.del == orig.macrosyn.del);
    REQUIRE(loaded.macrosyn.rev == orig.macrosyn.rev);

    REQUIRE(loaded.mods[0].type == orig.mods[0].type);
    REQUIRE(loaded.mods[0].dest == orig.mods[0].dest);
    REQUIRE(loaded.mods[0].amt == orig.mods[0].amt);
    REQUIRE(loaded.mods[0].p1 == orig.mods[0].p1);
    REQUIRE(loaded.mods[0].p2 == orig.mods[0].p2);
    REQUIRE(loaded.mods[0].p3 == orig.mods[0].p3);

    std::error_code ec;
    fs::remove_all(tempDir, ec);
}

TEST_CASE("InstrumentIO load example FMDUBSTABEQ_4_1.m8i", "[io]") {
    std::string examplePath = "third_party/m8-files-cxx/examples/instruments/FMDUBSTABEQ_4_1.m8i";
    if (fs::exists(examplePath)) {
        m8::engine::Instrument loaded{};
        std::optional<m8::engine::EqBank> loadedEq;
        std::string err;
        bool ok = m8::io::loadInstrument(examplePath, loaded, loadedEq, err);
        REQUIRE(ok);
        REQUIRE(loaded.type == m8::engine::InstType::INST_FMSYNTH);
        REQUIRE(loadedEq.has_value());
    }
}
