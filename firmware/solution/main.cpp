#include "can_utils.hpp"
#include <iostream>

int main() {
    // open files
    std::ifstream in;
    std::ofstream out;
    try {
        in  = open_input_file("dump.log");
        out = open_output_file("output.txt");
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    // load dbc networks
    std::vector<std::string> dbc_paths = {
    "dbc-files/ControlBus.dbc",
    "dbc-files/SensorBus.dbc",
    "dbc-files/TractiveBus.dbc"
    };
    std::vector<std::string> net_ifaces;
    auto nets = load_networks(dbc_paths, net_ifaces);

    // build one ID->message map per interface
    auto busMaps = build_bus_maps(nets, net_ifaces);

    // Read the log line-by-line, parse fields, convert payload hex->bytes, then decode
    std::string line;

    while (std::getline(in, line)) {
        auto maybeFrame = parse_frame(line);
        if (!maybeFrame) continue;
        const CanFrame& frame = *maybeFrame; 

        auto busIt = busMaps.find(frame.iface);
        if (busIt != busMaps.end()) {
            const dbcppp::IMessage* msg = busIt->second.find(frame.id);
            if (msg) {
                auto decoded = decode_signals(msg, frame);
                for (const auto& sig : decoded) {
                    out << format_decoded_line(frame.ts, sig.name, sig.value) << "\n";
                }
            }
        }
    }

    out.close();
    return 0;
}