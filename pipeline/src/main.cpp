// QuantumFlow market-data pipeline (C++).
//
// Native rewrite of the former Python pipeline (pipeline/src/app.py): ingests the
// OKX WebSocket feed, normalizes frames, and fans them out to sinks (stdout, JSONL,
// C++ bridge). Exposes rolling latency metrics + CSV export and listens on a Unix
// datagram control socket for runtime symbol updates.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "control_socket.hpp"
#include "metrics/rolling_metrics.hpp"
#include "normalizer.hpp"
#include "okx_ws.hpp"
#include "sinks/bridge_sink.hpp"
#include "sinks/jsonl_sink.hpp"
#include "sinks/sink.hpp"
#include "sinks/stdout_sink.hpp"

namespace qf = quantumflow::pipeline;

namespace {

std::atomic<bool> g_stop{false};
std::condition_variable g_cv;
std::mutex g_mu;

void handle_signal(int) {
    g_stop.store(true, std::memory_order_relaxed);
}

void log_info(const std::string& msg) {
    std::time_t now = std::time(nullptr);
    std::tm tm_local{};
#if defined(_WIN32)
    localtime_s(&tm_local, &now);
#else
    localtime_r(&now, &tm_local);
#endif
    char ts[24];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_local);
    std::fprintf(stderr, "%s [INFO] %s\n", ts, msg.c_str());
}

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t comma = s.find(',', start);
        std::string tok = s.substr(start, comma == std::string::npos ? std::string::npos
                                                                     : comma - start);
        // Trim.
        size_t b = tok.find_first_not_of(" \t");
        if (b != std::string::npos) {
            size_t e = tok.find_last_not_of(" \t");
            tok = tok.substr(b, e - b + 1);
            if (!tok.empty()) out.push_back(tok);
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

struct Args {
    std::string symbols = "BTC-USDT-SWAP,ETH-USDT-SWAP";
    std::string channels = "books5,trades";
    std::string csv_export;  // empty = disabled
    double csv_export_interval = 30.0;
    std::string url = "wss://ws.okx.com:8443/ws/v5/public";
    bool no_stdout = false;
    bool no_jsonl = false;
    bool cpp_bridge = false;
    std::string bridge_socket = "/tmp/quantumflow_bridge.sock";
    std::string control_socket = "/tmp/quantumflow_pipeline_ctrl.sock";
};

void print_usage(const char* prog) {
    std::printf(
        "Usage: %s [options]\n"
        "  --symbols S              Comma-separated trading pairs "
        "(default BTC-USDT-SWAP,ETH-USDT-SWAP)\n"
        "  --channels C             Comma-separated channels (default books5,trades)\n"
        "  --url U                  WebSocket URL (default wss://ws.okx.com:8443/ws/v5/public)\n"
        "  --csv-export PATH        CSV export file (default disabled)\n"
        "  --csv-export-interval S  CSV export interval seconds (default 30)\n"
        "  --no-stdout              Disable stdout sink\n"
        "  --no-jsonl               Disable JSONL file sink\n"
        "  --cpp-bridge             Enable C++ bridge sink (Unix socket)\n"
        "  --bridge-socket PATH     Bridge socket (default /tmp/quantumflow_bridge.sock)\n"
        "  --control-socket PATH    Control socket (default /tmp/quantumflow_pipeline_ctrl.sock)\n"
        "  -h, --help               Show this help\n",
        prog);
}

bool parse_args(int argc, char** argv, Args& a) {
    auto need_val = [&](int& i) -> const char* {
        if (i + 1 >= argc) {
            std::fprintf(stderr, "Missing value for %s\n", argv[i]);
            return nullptr;
        }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "--symbols") {
            const char* v = need_val(i); if (!v) return false; a.symbols = v;
        } else if (arg == "--channels") {
            const char* v = need_val(i); if (!v) return false; a.channels = v;
        } else if (arg == "--url") {
            const char* v = need_val(i); if (!v) return false; a.url = v;
        } else if (arg == "--csv-export") {
            const char* v = need_val(i); if (!v) return false; a.csv_export = v;
        } else if (arg == "--csv-export-interval") {
            const char* v = need_val(i); if (!v) return false; a.csv_export_interval = std::atof(v);
        } else if (arg == "--no-stdout") {
            a.no_stdout = true;
        } else if (arg == "--no-jsonl") {
            a.no_jsonl = true;
        } else if (arg == "--cpp-bridge") {
            a.cpp_bridge = true;
        } else if (arg == "--bridge-socket") {
            const char* v = need_val(i); if (!v) return false; a.bridge_socket = v;
        } else if (arg == "--control-socket") {
            const char* v = need_val(i); if (!v) return false; a.control_socket = v;
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", arg.c_str());
            print_usage(argv[0]);
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) return 1;

    std::vector<std::string> symbols = qf::normalize_symbols(split_csv(args.symbols));
    if (symbols.empty()) {
        std::fprintf(stderr, "No symbols provided\n");
        return 1;
    }
    std::vector<std::string> channels = split_csv(args.channels);
    if (channels.empty()) {
        std::fprintf(stderr, "No channels provided\n");
        return 1;
    }

    log_info("Starting pipeline: url=" + args.url);
    {
        std::string sink_info = "Sinks: stdout=" + std::string(args.no_stdout ? "0" : "1") +
                                " jsonl=" + std::string(args.no_jsonl ? "0" : "1") +
                                " cpp_bridge=" + std::string(args.cpp_bridge ? "1" : "0") +
                                " socket=" + args.bridge_socket;
        log_info(sink_info);
    }
    log_info("Control socket: " + args.control_socket);

    // ── Sinks ──────────────────────────────────────────────
    std::vector<std::unique_ptr<qf::Sink>> sinks;
    if (!args.no_stdout) sinks.push_back(std::make_unique<qf::StdoutSink>());
    if (!args.no_jsonl) sinks.push_back(std::make_unique<qf::JsonlSink>("data", 1.0, 100));
    if (args.cpp_bridge) sinks.push_back(std::make_unique<qf::CppBridgeSink>(args.bridge_socket));

    qf::RollingMetrics metrics(5.0);

    // ── WebSocket ingest → normalize → metrics + sinks (runs on IX thread) ──
    qf::OkxWsClient ws(args.url, channels,
        [&metrics, &sinks](int64_t ts_epoch_ms, int64_t ts_recv_ns, int64_t ts_dec_ns,
                           const nlohmann::json& msg) {
            auto events = qf::normalize_okx(ts_epoch_ms, ts_recv_ns, ts_dec_ns, msg);
            for (const auto& ev : events) {
                metrics.update(ev);
                for (auto& sink : sinks) {
                    sink->write(ev);
                }
            }
        });

    ws.set_symbols(symbols);
    log_info("Opening OKX stream");
    ws.start();

    // ── Control socket: runtime symbol updates ─────────────
    qf::ControlSocket control(args.control_socket, [&ws](std::vector<std::string> next) {
        std::string joined;
        for (size_t i = 0; i < next.size(); ++i) {
            if (i) joined += ",";
            joined += next[i];
        }
        log_info("Updated active symbol subscriptions: " + joined);
        ws.set_symbols(std::move(next));
    });
    control.start();

    // ── Metrics printer thread (~1 Hz) ─────────────────────
    std::thread printer([&metrics] {
        while (!g_stop.load(std::memory_order_relaxed)) {
            std::unique_lock<std::mutex> lk(g_mu);
            g_cv.wait_for(lk, std::chrono::seconds(1),
                          [] { return g_stop.load(std::memory_order_relaxed); });
            if (g_stop.load(std::memory_order_relaxed)) break;
            lk.unlock();
            metrics.print_stats();
        }
    });

    // ── CSV exporter thread (optional) ─────────────────────
    std::thread csv_exporter;
    if (!args.csv_export.empty()) {
        const std::string path = args.csv_export;
        const auto interval = std::chrono::duration<double>(args.csv_export_interval);
        csv_exporter = std::thread([&metrics, path, interval] {
            while (!g_stop.load(std::memory_order_relaxed)) {
                std::unique_lock<std::mutex> lk(g_mu);
                g_cv.wait_for(lk, interval,
                              [] { return g_stop.load(std::memory_order_relaxed); });
                if (g_stop.load(std::memory_order_relaxed)) break;
                lk.unlock();
                metrics.export_csv(path);
                log_info("Exported metrics to " + path);
            }
        });
        log_info("CSV export: " + args.csv_export);
    }

    // ── Signals + main wait loop ───────────────────────────
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    while (!g_stop.load(std::memory_order_relaxed)) {
        std::unique_lock<std::mutex> lk(g_mu);
        g_cv.wait_for(lk, std::chrono::milliseconds(200),
                      [] { return g_stop.load(std::memory_order_relaxed); });
    }

    log_info("Received shutdown signal, stopping...");
    g_cv.notify_all();  // Wake helper threads promptly.

    // ── Shutdown ───────────────────────────────────────────
    ws.stop();
    control.stop();
    if (printer.joinable()) printer.join();
    if (csv_exporter.joinable()) csv_exporter.join();

    for (auto& sink : sinks) {
        sink->close();
    }

    metrics.print_stats(/*force=*/true);
    if (!args.csv_export.empty()) {
        metrics.export_csv(args.csv_export);
        log_info("Final metrics exported to " + args.csv_export);
    }
    log_info("Shutdown complete");
    return 0;
}
