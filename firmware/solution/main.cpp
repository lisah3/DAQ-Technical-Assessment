#include <iostream>
#include <fstream>
#include <regex>
#include <string>
#include <cstdint>
#include <iomanip>
#include <vector>

#include <dbcppp/Network.h>
#include <unordered_map>
#include <memory>

#include <array>
#include <algorithm>

class CanFrame {
public:
    // members
    double ts;                 // timestamp
    std::string iface;          // CAN interface
    uint32_t id;                // CAN ID
    std::vector<uint8_t> data;  // payload bytes

    // constructor
    CanFrame(double ts_, std::string iface_, uint32_t id_, std::vector<uint8_t> data_)
        : ts(ts_), iface(std::move(iface_)), id(id_), data(std::move(data_)) {}

    // pretty-printer for raw frame line (debugging/visibility)
    void print() const {
        std::cout << std::fixed << std::setprecision(6)
                  << ts << " " << iface
                  << " ID=0x" << std::hex << std::uppercase << id << std::dec
                  << " DATA:";
        for (auto b : data) {
            std::cout << " "
                      << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(b) << std::dec;
        }
        std::cout << "\n";
    }
};

int main() {
    // open input file dump.log
    std::ifstream in("dump.log");   // lives at /app/dump.log in your container
    if (!in) {
        std::cerr << "Failed to open dump.log\n";
        return 1;
    }

    // --- added: open output file ---
    std::ofstream out("output.txt");
    if (!out) {
        std::cerr << "Failed to open output.txt for writing\n";
        return 1;
    }
    // --------------------------------


    // Regex to parse lines like: (timestamp) iface HEX_CAN_ID#HEX_PAYLOAD
    std::regex rx(R"(\((\d+\.\d+)\)\s+(\S+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*))");
    std::string line;

    // Load all DBC networks 
    std::vector<std::unique_ptr<dbcppp::INetwork>> nets;

    // per-bus lookup tables and DBC→iface bookkeeping 
    std::vector<std::string> net_ifaces;
    std::unordered_map<std::string, std::unordered_map<uint32_t, const dbcppp::IMessage*>> bus2id2msg;

    for (const char* path : {
            "dbc-files/ControlBus.dbc",
            "dbc-files/SensorBus.dbc",
            "dbc-files/TractiveBus.dbc"
        })
    {
        std::ifstream f(path);
        if (!f) {
            std::cerr << "Failed to open DBC: " << path << "\n";
            continue;
        }
        auto net = dbcppp::INetwork::LoadDBCFromIs(f);
        if (!net) {
            std::cerr << "Failed to parse DBC: " << path << "\n";
            continue;
        }

        // choose interface for this DBC by its filename 
        std::string ifaceForDbc = "can0";
        std::string spath = path;
        if      (spath.find("Sensor")   != std::string::npos) ifaceForDbc = "can1";
        else if (spath.find("Tractive") != std::string::npos) ifaceForDbc = "can2";

        nets.push_back(std::move(net));

        // remember which iface this network belongs to
        net_ifaces.push_back(ifaceForDbc);
    }

    // build one ID->message map *per* interface (bus) ---
    for (size_t i = 0; i < nets.size(); ++i) {
        const auto& net   = nets[i];
        const auto& iface = net_ifaces[i];
        auto& idmap = bus2id2msg[iface];
        for (const dbcppp::IMessage& msg : net->Messages()) {
            idmap.emplace(msg.Id(), &msg);
        }
    }

    // Read the log line-by-line, parse fields, convert payload hex->bytes, then decode.
    while (std::getline(in, line)) {
        // parse dump.log for ts, iface, id, and data
        std::smatch m;
        if (!std::regex_search(line, m, rx)) continue;

        const double     ts    = std::stod(m[1].str());
        const std::string iface = m[2].str();
        const uint32_t   id    = static_cast<uint32_t>(std::stoul(m[3].str(), nullptr, 16));
        const std::string raw_data = m[4].str();

        if (raw_data.size() % 2 != 0) continue; // skip malformed payloads

         // Convert hex data string to bytes
        std::vector<uint8_t> data;
        data.reserve(raw_data.size() / 2);
        for (size_t i = 0; i < raw_data.size(); i += 2) {
            uint8_t byte = static_cast<uint8_t>(
                std::stoul(raw_data.substr(i, 2), nullptr, 16)
            );
            data.push_back(byte);
        }

        // Wrap parsed values into a CanFrame object, print the raw frame
        CanFrame frame(ts, iface, id, std::move(data));
        // frame.print();

        // pick the right per-bus table using the frame's iface 
        auto busIt = bus2id2msg.find(frame.iface);
        if (busIt != bus2id2msg.end()) {
            const auto& idmap = busIt->second;
            auto it = idmap.find(frame.id);
            if (it != idmap.end()) {
                const dbcppp::IMessage* msg = it->second;

                // Optional: print message name (kept simple)
                // std::cout << "  MSG=" << msg->Name() << "\n";

                // make zero-padded 8-byte buffer for decoding
                std::array<uint8_t, 8> payload8{}; // zero-initialized
                const size_t n = std::min<size_t>(frame.data.size(), payload8.size());
                std::copy_n(frame.data.begin(), n, payload8.begin());
                const uint8_t* bytes = payload8.data();

                // Iterate all signals; respect multiplexing rules before decoding
                for (const dbcppp::ISignal& sig : msg->Signals()) {
                    const dbcppp::ISignal* mux_sig = msg->MuxSignal();
                    const bool selected_mux =
                        sig.MultiplexerIndicator() != dbcppp::ISignal::EMultiplexer::MuxValue ||
                        (mux_sig && mux_sig->Decode(bytes) == sig.MultiplexerSwitchValue());

                    if (selected_mux) {
                        const double phys = sig.RawToPhys(sig.Decode(bytes));    
                        out << "(" << std::fixed << std::setprecision(6) 
                        << ts << "): " << sig.Name() << ": " << std::defaultfloat 
                        << std::setprecision(6) << phys << "\n";
                    }
                }
            }
        }
    }
    out.close();
    return 0;
}