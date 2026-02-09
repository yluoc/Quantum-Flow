#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <cstring>
#include <string>

#include "bridge/shared_memory.hpp"

namespace py = pybind11;

static void push_market_data(
    const std::string& symbol,
    int side,              // 0 = buy, 1 = sell
    int event_type,        // 0 = book_level, 1 = trade
    double price,
    uint64_t quantity,
    uint64_t timestamp_ns,
    uint64_t order_id
) {
    quantumflow::MarketDataPacket packet{};
    std::strncpy(packet.symbol, symbol.c_str(), sizeof(packet.symbol) - 1);
    packet.symbol[sizeof(packet.symbol) - 1] = '\0';
    packet.side = static_cast<uint8_t>(side);
    packet.event_type = static_cast<uint8_t>(event_type);
    packet.price = price;
    packet.quantity = quantity;
    packet.timestamp_ns = timestamp_ns;
    packet.order_id = order_id;

    // Release the GIL while pushing to the lock-free buffer
    py::gil_scoped_release release;
    quantumflow::global_bridge().push(packet);
}

static py::dict bridge_stats() {
    auto& bridge = quantumflow::global_bridge();
    py::dict d;
    d["push_count"] = bridge.push_count();
    d["pop_count"]  = bridge.pop_count();
    d["drop_count"] = bridge.drop_count();
    d["size"]       = bridge.size();
    return d;
}

PYBIND11_MODULE(quantumflow_bridge, m) {
    m.doc() = "QuantumFlow shared memory bridge for Python→C++ market data transfer";

    m.def("push_market_data", &push_market_data,
          py::arg("symbol"),
          py::arg("side"),
          py::arg("event_type"),
          py::arg("price"),
          py::arg("quantity"),
          py::arg("timestamp_ns"),
          py::arg("order_id") = 0,
          "Push a market data packet to the C++ ring buffer");

    m.def("bridge_stats", &bridge_stats,
          "Return dict with push_count, pop_count, drop_count, size");
}
