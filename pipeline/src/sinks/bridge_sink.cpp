#include "bridge_sink.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "common/market_data_packet.hpp"

namespace quantumflow::pipeline {

namespace {
// Quantity conversion: float size -> integer lots (matches _QTY_SCALE in bridge.py
// and QF_DEFAULT_QTY_SCALE in the C-API).
constexpr double kQtyScale = 1e8;

void encode_symbol(char out[16], const std::string& symbol) {
    std::memset(out, 0, 16);
    const size_t n = std::min<size_t>(symbol.size(), 15);
    std::memcpy(out, symbol.data(), n);
}
} // namespace

CppBridgeSink::CppBridgeSink(std::string socket_path)
    : socket_path_(std::move(socket_path)) {
    if (socket_path_.size() >= sizeof(sockaddr_un::sun_path)) {
        std::fprintf(stderr, "[bridge] socket path too long: %s\n", socket_path_.c_str());
        return;
    }
    fd_ = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        std::fprintf(stderr, "[bridge] failed to create socket: %s\n", std::strerror(errno));
        return;
    }
    // Non-blocking, like the Python sink (datagrams are dropped under backpressure).
    int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
}

CppBridgeSink::~CppBridgeSink() {
    close();
}

void CppBridgeSink::send_packet(const std::string& symbol, uint8_t side, uint8_t event_type,
                                double price, uint64_t quantity, uint64_t timestamp_ns,
                                uint64_t order_id) {
    if (fd_ < 0) return;

    MarketDataPacket pkt{};
    encode_symbol(pkt.symbol, symbol);
    pkt.side = side;
    pkt.event_type = event_type;
    pkt.price = price;
    pkt.quantity = quantity;
    pkt.timestamp_ns = timestamp_ns;
    pkt.order_id = order_id;

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path_.c_str());

    const ssize_t n = ::sendto(fd_, &pkt, sizeof(pkt), 0,
                               reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (n == static_cast<ssize_t>(sizeof(pkt))) {
        ++sent_;
    } else {
        ++dropped_;
        if (errno == ENOENT && !warned_missing_socket_) {
            std::fprintf(stderr,
                         "[bridge] socket %s not found. Start the C++ engine first.\n",
                         socket_path_.c_str());
            warned_missing_socket_ = true;
        }
    }
}

void CppBridgeSink::write(const NormalizedEvent& event) {
    const uint64_t ts_ns = static_cast<uint64_t>(event.ts_recv_mono_ns);

    if (event.is_book()) {
        const BookPayload& p = event.book();
        for (const auto& lvl : p.bids) {
            send_packet(event.symbol, /*side=*/0, /*event_type=*/0, lvl.price,
                        static_cast<uint64_t>(lvl.size * kQtyScale), ts_ns);
        }
        for (const auto& lvl : p.asks) {
            send_packet(event.symbol, /*side=*/1, /*event_type=*/0, lvl.price,
                        static_cast<uint64_t>(lvl.size * kQtyScale), ts_ns);
        }
    } else if (event.is_trade()) {
        const TradePayload& p = event.trade();
        const uint8_t side = (p.side == "buy") ? 0 : 1;
        send_packet(event.symbol, side, /*event_type=*/1, p.price,
                    static_cast<uint64_t>(p.size * kQtyScale), ts_ns);
    }
}

void CppBridgeSink::close() {
    if (closed_) return;
    closed_ = true;
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    std::fprintf(stderr, "[bridge] closed - sent=%llu dropped=%llu socket=%s\n",
                 static_cast<unsigned long long>(sent_),
                 static_cast<unsigned long long>(dropped_), socket_path_.c_str());
}

} // namespace quantumflow::pipeline
