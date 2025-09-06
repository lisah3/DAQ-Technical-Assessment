#include <catch2/catch_test_macros.hpp>
#include "can_utils.hpp"
#include <algorithm>
#include <string>
#include <vector>

#ifndef DBC_DIR
#error "DBC_DIR not defined — set it in CMake to point at firmware/dbc-files"
#endif


TEST_CASE("parse_frame parses and validates CAN log lines", "[parse_frame]") {
    const std::string ts    = "1730892639.316946";
    const std::string iface = "can0";

    auto make_line = [&](std::string id_hex, std::string payload_hex) {
        return "(" + ts + ") " + iface + " " + id_hex + "#" + payload_hex;
    };

    SECTION("parses 4-byte payload") {
        auto f = parse_frame(make_line("1AB", "0A0B0C0D"));
        REQUIRE(f.has_value());
        CHECK(f->ts == 1730892639.316946);
        CHECK(f->iface == "can0");
        CHECK(f->id == 0x1AB);
        REQUIRE(f->data.size() == 4);
        CHECK(f->data[0] == 0x0A);
        CHECK(f->data[1] == 0x0B);
        CHECK(f->data[2] == 0x0C);
        CHECK(f->data[3] == 0x0D);
    }

    SECTION("parses 8-byte payload") {
        auto f = parse_frame(make_line("1AB", "deAdBEEF00112233"));
        REQUIRE(f.has_value());
        CHECK(f->ts == 1730892639.316946);
        CHECK(f->iface == "can0");
        CHECK(f->id == 0x1AB);
        REQUIRE(f->data.size() == 8);
        CHECK(f->data[0] == 0xDE);
        CHECK(f->data[1] == 0xAD);
        CHECK(f->data[2] == 0xBE);
        CHECK(f->data[3] == 0xEF);
        CHECK(f->data[4] == 0x00);
        CHECK(f->data[5] == 0x11);
        CHECK(f->data[6] == 0x22);
        CHECK(f->data[7] == 0x33);
    }

    SECTION("allows zero-length payload") {
        auto f = parse_frame("(" + ts + ") can1 123#");
        REQUIRE(f.has_value());
        CHECK(f->iface == "can1");
        CHECK(f->id == 0x123);
        CHECK(f->data.empty());
    }

    SECTION("rejects odd-length payload") {
        auto f = parse_frame("(" + ts + ") can0 12F#ABC");
        CHECK_FALSE(f.has_value());
    }
}


TEST_CASE("decodes a range of signals", "[decode_signals]") {
    // 1) Load networks from repo DBCs
    std::vector<std::string> dbc_paths = {
        std::string(DBC_DIR) + "/ControlBus.dbc",
        std::string(DBC_DIR) + "/SensorBus.dbc",
        std::string(DBC_DIR) + "/TractiveBus.dbc"
    };
    std::vector<std::string> ifaces;
    auto nets = load_networks(dbc_paths, ifaces);
    REQUIRE(nets.size() == dbc_paths.size());
    REQUIRE(ifaces.size() == dbc_paths.size());

    // 2) Build iface->(id->message) maps
    auto maps = build_bus_maps(nets, ifaces);

    SECTION("first line in dump.log") {
        // 3) Parse the sample line
        const std::string line = "(1730892639.316946) can1 709#FF7F0080A3BC";
        auto maybe = parse_frame(line);
        REQUIRE(maybe.has_value());
        const CanFrame& frame = *maybe;

        // 4) Lookup message by iface+id
        auto busIt = maps.find(frame.iface);
        REQUIRE(busIt != maps.end());
        const dbcppp::IMessage* msg = busIt->second.find(frame.id);
        REQUIRE(msg != nullptr);

        // 5) Decode and find the specific signal
        auto decoded = decode_signals(msg, frame);
        auto it = std::find_if(decoded.begin(), decoded.end(),
                            [](const DecodedSignal& s){ return s.name == "CoolantPressureFanOUT"; });
        REQUIRE(it != decoded.end());

        // 6) Assert expected value
        CHECK(it->value == -1724.5);
    }

}
