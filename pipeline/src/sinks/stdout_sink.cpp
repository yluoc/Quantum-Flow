#include "stdout_sink.hpp"

#include <cstdio>

// UTF-8 right-arrow (U+2192). Kept as its own literal so adjacent-string
// concatenation prevents the next letter (e.g. 'D') being read as a hex digit.
#define AR "\xE2\x86\x92"

namespace quantumflow::pipeline {

void StdoutSink::write(const NormalizedEvent& event) {
    const long long lat_ex_to_recv_ms = event.ts_recv_epoch_ms - event.ts_exchange_ms;
    const double lat_recv_to_decode_us =
        (event.ts_decoded_mono_ns - event.ts_recv_mono_ns) / 1000.0;
    const double lat_decode_to_proc_us =
        (event.ts_proc_mono_ns - event.ts_decoded_mono_ns) / 1000.0;

    if (event.is_book()) {
        const BookPayload& p = event.book();
        const double spread = p.best_ask - p.best_bid;
        std::printf(
            "%s | bid=%.2f ask=%.2f spread=%.2f | "
            "Ex" AR "Recv=%lldms Recv" AR "Decode=%.3fus Decode" AR "Proc=%.3fus\n",
            event.symbol.c_str(), p.best_bid, p.best_ask, spread,
            lat_ex_to_recv_ms, lat_recv_to_decode_us, lat_decode_to_proc_us);
    } else if (event.is_trade()) {
        const TradePayload& p = event.trade();
        std::printf(
            "%s | trade %s price=%.2f size=%.6f | "
            "Ex" AR "Recv=%lldms Recv" AR "Decode=%.3fus Decode" AR "Proc=%.3fus\n",
            event.symbol.c_str(), p.side.c_str(), p.price, p.size,
            lat_ex_to_recv_ms, lat_recv_to_decode_us, lat_decode_to_proc_us);
    }
}

} // namespace quantumflow::pipeline
