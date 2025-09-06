#include <iostream>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>
#include <cstdint>
#include <iomanip>
#include <vector>

#include <dbcppp/Network.h>
#include <unordered_map>
#include <memory>

#include <array>
#include <algorithm>
#include <optional>

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
};

class BusMap {
public:
    // Add all messages from one DBC network into this bus map
    void add_network(const dbcppp::INetwork& net) {
        for (const dbcppp::IMessage& msg : net.Messages()) {
            idmap_.emplace(msg.Id(), &msg);
        }
    }

    // Lookup a message by CAN ID
    const dbcppp::IMessage* find(uint32_t id) const {
        auto it = idmap_.find(id);
        return (it != idmap_.end()) ? it->second : nullptr;
    }

    // For debugging/inspection
    void dump(const std::string& iface) const {
        std::cout << iface << " bus map:\n";
        for (auto& [id, msg] : idmap_) {
            std::cout << "  0x" << std::hex << id << " → " 
                      << msg->Name() << std::dec << "\n";
        }
    }

private:
    std::unordered_map<uint32_t, const dbcppp::IMessage*> idmap_;
};

struct DecodedSignal {
    std::string name;
    double value;
};


// function signatures
std::optional<CanFrame> parse_frame(const std::string& line);
std::array<uint8_t, 8> pad_payload_8(const std::vector<uint8_t>& data);
std::string iface_for_dbc_path(std::string_view path);
std::vector<std::unique_ptr<dbcppp::INetwork>>
load_networks(const std::vector<std::string>& dbc_paths,
              std::vector<std::string>& out_ifaces);
std::unordered_map<std::string, BusMap>
build_bus_maps(const std::vector<std::unique_ptr<dbcppp::INetwork>>& nets,
               const std::vector<std::string>& ifaces);
std::string format_decoded_line(double ts, const std::string& sigName,
                                double physValue);
std::vector<DecodedSignal> decode_signals(const dbcppp::IMessage* msg,
                                          const CanFrame& frame);
std::ifstream open_input_file(const std::string& path);
std::ofstream open_output_file(const std::string& path);


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


std::ofstream open_output_file(const std::string& path) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("Failed to open output file: " + path);
    }
    return out;
}


std::ifstream open_input_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Failed to open input file: " + path);
    }
    return in;
}


std::vector<DecodedSignal> decode_signals(const dbcppp::IMessage* msg,
                                          const CanFrame& frame)
{
    // pad to 8 bytes once, inside this function
    auto payload8 = pad_payload_8(frame.data);
    const uint8_t* bytes = payload8.data();

    std::vector<DecodedSignal> decoded;
    for (const dbcppp::ISignal& sig : msg->Signals()) {
        const dbcppp::ISignal* mux_sig = msg->MuxSignal();
        const bool selected_mux =
            sig.MultiplexerIndicator() != dbcppp::ISignal::EMultiplexer::MuxValue ||
            (mux_sig && mux_sig->Decode(bytes) == sig.MultiplexerSwitchValue());

        if (selected_mux) {
            const double phys = sig.RawToPhys(sig.Decode(bytes));
            decoded.push_back({sig.Name(), phys});
        }
    }
    return decoded;
}


std::string format_decoded_line(double ts,
                                const std::string& sigName,
                                double physValue)
{
    std::ostringstream oss;
    oss << "(" << std::fixed << std::setprecision(6)
        << ts << "): " << sigName << ": " << std::defaultfloat
        << std::setprecision(6) << physValue;
    return oss.str();
}


std::unordered_map<std::string, BusMap>
build_bus_maps(const std::vector<std::unique_ptr<dbcppp::INetwork>>& nets,
               const std::vector<std::string>& ifaces)
{
    std::unordered_map<std::string, BusMap> busMaps;
    for (size_t i = 0; i < nets.size(); ++i) {
        const auto& net   = nets[i];
        const auto& iface = ifaces[i];
        busMaps[iface].add_network(*net);
    }
    return busMaps;
}


std::vector<std::unique_ptr<dbcppp::INetwork>>
load_networks(const std::vector<std::string>& dbc_paths,
              std::vector<std::string>& out_ifaces)
{
    std::vector<std::unique_ptr<dbcppp::INetwork>> nets;
    nets.reserve(dbc_paths.size());
    out_ifaces.clear();
    out_ifaces.reserve(dbc_paths.size());

    for (const std::string& path : dbc_paths) {
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

        out_ifaces.push_back(iface_for_dbc_path(path));
        nets.push_back(std::move(net));
    }
    return nets;
}


std::string iface_for_dbc_path(std::string_view path) {
    // default
    std::string iface = "can0";
    if (path.find("Sensor")   != std::string_view::npos) iface = "can1";
    if (path.find("Tractive") != std::string_view::npos) iface = "can2";
    return iface;
}


std::array<uint8_t, 8> pad_payload_8(const std::vector<uint8_t>& data) {
    std::array<uint8_t, 8> payload{}; // zero-initialized
    const size_t n = std::min<size_t>(data.size(), payload.size());
    std::copy_n(data.begin(), n, payload.begin());
    return payload;
}


std::optional<CanFrame> parse_frame(const std::string& line) {
    // Regex to parse lines like: (timestamp) iface HEX_CAN_ID#HEX_PAYLOAD
    static const std::regex rx(R"(\((\d+\.\d+)\)\s+(\S+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*))");

    std::smatch m;
    if (!std::regex_search(line, m, rx)) {
        return std::nullopt;   // invalid format
    }

    const double ts = std::stod(m[1].str());
    const std::string iface = m[2].str();
    const uint32_t id = static_cast<uint32_t>(
        std::stoul(m[3].str(), nullptr, 16)
    );
    const std::string raw_data = m[4].str();

    if (raw_data.size() % 2 != 0) {
        return std::nullopt;   // malformed payload
    }

    // Convert hex data string to bytes
    std::vector<uint8_t> data;
    data.reserve(raw_data.size() / 2);
    for (size_t i = 0; i < raw_data.size(); i += 2) {
        uint8_t byte = static_cast<uint8_t>(
            std::stoul(raw_data.substr(i, 2), nullptr, 16)
        );
        data.push_back(byte);
    }

    return CanFrame(ts, iface, id, std::move(data));
}