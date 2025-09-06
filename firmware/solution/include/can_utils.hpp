#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <optional>
#include <unordered_map>
#include <memory>
#include <fstream>

#include <dbcppp/Network.h>

// ---- Data types ----
class CanFrame {
public:
    double ts;
    std::string iface;
    uint32_t id;
    std::vector<uint8_t> data;

    CanFrame(double ts_, std::string iface_, uint32_t id_, std::vector<uint8_t> data_)
        : ts(ts_), iface(std::move(iface_)), id(id_), data(std::move(data_)) {}
};

class BusMap {
public:
    void add_network(const dbcppp::INetwork& net);
    const dbcppp::IMessage* find(uint32_t id) const;
    void dump(const std::string& iface) const;
private:
    std::unordered_map<uint32_t, const dbcppp::IMessage*> idmap_;
};

struct DecodedSignal {
    std::string name;
    double value;
};

// ---- API ----
std::optional<CanFrame> parse_frame(const std::string& line);
std::array<uint8_t, 8> pad_payload_8(const std::vector<uint8_t>& data);
std::string iface_for_dbc_path(std::string_view path);

std::vector<std::unique_ptr<dbcppp::INetwork>>
load_networks(const std::vector<std::string>& dbc_paths,
              std::vector<std::string>& out_ifaces);

std::unordered_map<std::string, BusMap>
build_bus_maps(const std::vector<std::unique_ptr<dbcppp::INetwork>>& nets,
               const std::vector<std::string>& ifaces);

std::string format_decoded_line(double ts, const std::string& sigName, double physValue);

std::vector<DecodedSignal> decode_signals(const dbcppp::IMessage* msg,
                                          const CanFrame& frame);

// I/O helpers
std::ifstream open_input_file(const std::string& path);
std::ofstream open_output_file(const std::string& path);

