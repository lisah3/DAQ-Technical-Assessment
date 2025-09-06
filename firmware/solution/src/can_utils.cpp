#include "can_utils.hpp"

#include <regex>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <iostream>

// ---- BusMap ----
void BusMap::add_network(const dbcppp::INetwork& net) {
    for (const dbcppp::IMessage& msg : net.Messages()) {
        idmap_.emplace(msg.Id(), &msg);
    }
}

const dbcppp::IMessage* BusMap::find(uint32_t id) const {
    auto it = idmap_.find(id);
    return (it != idmap_.end()) ? it->second : nullptr;
}

void BusMap::dump(const std::string& iface) const {
    std::cout << iface << " bus map:\n";
    for (auto& [id, msg] : idmap_) {
        std::cout << "  0x" << std::hex << id << " → "
                  << msg->Name() << std::dec << "\n";
    }
}

// ---- I/O helpers ----
std::ifstream open_input_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Failed to open input file: " + path);
    return in;
}
std::ofstream open_output_file(const std::string& path) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Failed to open output file: " + path);
    return out;
}

// ---- Utility fns (moved over from your main.cpp) ----
std::string iface_for_dbc_path(std::string_view path) {
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
    static const std::regex rx(R"(\((\d+\.\d+)\)\s+(\S+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*))");
    std::smatch m;
    if (!std::regex_search(line, m, rx)) return std::nullopt;

    const double ts = std::stod(m[1].str());
    const std::string iface = m[2].str();
    const uint32_t id = static_cast<uint32_t>(std::stoul(m[3].str(), nullptr, 16));
    const std::string raw_data = m[4].str();
    if (raw_data.size() % 2 != 0) return std::nullopt;

    std::vector<uint8_t> data;
    data.reserve(raw_data.size() / 2);
    for (size_t i = 0; i < raw_data.size(); i += 2) {
        uint8_t byte = static_cast<uint8_t>(std::stoul(raw_data.substr(i, 2), nullptr, 16));
        data.push_back(byte);
    }
    return CanFrame(ts, iface, id, std::move(data));
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

std::vector<DecodedSignal> decode_signals(const dbcppp::IMessage* msg,
                                          const CanFrame& frame)
{
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