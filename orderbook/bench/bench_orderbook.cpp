#include <iostream>
#include <vector>
#include <random>
#include <chrono>
#include <cassert>
#include <iomanip>
#include "LOB/Book.h"
#include "LOB/Types.h"

using std::cout;
using std::endl;
using std::vector;
using std::mt19937;
using std::uniform_int_distribution;
using std::uniform_real_distribution;

using namespace std;
using namespace std::chrono;

struct Message {
    enum Type { NEW, CANCEL } type;
    ID order_id;
    ID agent_id;
    OrderType order_type;
    PRICE price;
    Volume volume;
    
    Message(Type t, ID oid, ID aid, OrderType ot, PRICE p, Volume v)
        : type(t), order_id(oid), agent_id(aid), order_type(ot), price(p), volume(v) {}
};

// Realistic order book simulation parameters
struct SimulationParams {
    size_t total_messages;
    double cancel_rate;           // Fraction of messages that are cancels
    double match_rate;            // Fraction of new orders that match
    PRICE price_range_start;
    PRICE price_range_end;
    Volume min_volume;
    Volume max_volume;
    size_t num_agents;
};

// Generate realistic order book messages
vector<Message> generate_realistic_messages(const SimulationParams& params, uint32_t seed = 42) {
    vector<Message> messages;
    messages.reserve(params.total_messages);
    
    mt19937 rng(seed);
    uniform_int_distribution<PRICE> price_dist(params.price_range_start, params.price_range_end);
    uniform_int_distribution<Volume> volume_dist(params.min_volume, params.max_volume);
    uniform_int_distribution<int> type_dist(0, 1);
    uniform_real_distribution<double> cancel_prob(0.0, 1.0);
    uniform_int_distribution<ID> agent_dist(1, params.num_agents);
    
    vector<ID> active_orders;
    ID next_order_id = 1;
    PRICE current_mid = (params.price_range_start + params.price_range_end) / 2;
    
    for (size_t i = 0; i < params.total_messages; ++i) {
        // Determine if this is a cancel
        if (cancel_prob(rng) < params.cancel_rate && !active_orders.empty()) {
            // Cancel message
            uniform_int_distribution<size_t> idx_dist(0, active_orders.size() - 1);
            size_t idx = idx_dist(rng);
            ID order_id = active_orders[idx];
            // Swap and pop for O(1)
            active_orders[idx] = active_orders.back();
            active_orders.pop_back();
            
            messages.emplace_back(Message::CANCEL, order_id, 0, BUY, 0, 0);
        } else {
            // New order message
            OrderType ot = (type_dist(rng) == 0) ? BUY : SELL;
            
            // Realistic price distribution around mid price
            PRICE price;
            if (params.match_rate > 0.0 && uniform_real_distribution<double>(0.0, 1.0)(rng) < params.match_rate) {
                // Aggressive order that will match
                int offset = uniform_int_distribution<int>(-5, 5)(rng);
                int raw_price;
                if (ot == BUY) {
                    raw_price = static_cast<int>(current_mid) + offset;
                } else {
                    raw_price = static_cast<int>(current_mid) - offset;
                }
                price = static_cast<PRICE>(std::max(1, raw_price));
            } else {
                // Passive order
                price = price_dist(rng);
            }
            
            Volume volume = volume_dist(rng);
            ID agent_id = agent_dist(rng);
            
            messages.emplace_back(Message::NEW, next_order_id, agent_id, ot, price, volume);
            active_orders.push_back(next_order_id);
            ++next_order_id;
            
            // Update mid price occasionally
            if (i % 1000 == 0) {
                current_mid = price_dist(rng);
            }
        }
    }
    
    return messages;
}

// Run simulation and collect metrics
struct Metrics {
    size_t messages_processed;
    size_t orders_placed;
    size_t orders_cancelled;
    size_t trades_generated;
    double total_time_ms;
    double avg_latency_ns;
    double ops_per_sec;
    double trades_per_sec;
    size_t peak_resting_orders;
    size_t final_resting_orders;
    size_t peak_levels;
    size_t final_levels;
};

Metrics run_simulation(const vector<Message>& messages, size_t warmup_iterations = 10000) {
    Book book(100000); // Large initial pool
    Metrics metrics = {};
    
    // Warmup phase
    size_t warmup_count = std::min(warmup_iterations, messages.size());
    for (size_t i = 0; i < warmup_count; ++i) {
        const auto& msg = messages[i];
        if (msg.type == Message::NEW) {
            book.place_order(msg.order_id, msg.agent_id, msg.order_type, msg.price, msg.volume);
        } else {
            book.delete_order(msg.order_id);
        }
    }
    
    // Reset for actual benchmark
    Book benchmark_book(100000);
    metrics.messages_processed = 0;
    metrics.orders_placed = 0;
    metrics.orders_cancelled = 0;
    metrics.trades_generated = 0;
    metrics.peak_resting_orders = 0;
    metrics.peak_levels = 0;
    
    auto start = high_resolution_clock::now();
    
    for (const auto& msg : messages) {
        if (msg.type == Message::NEW) {
            const Trades& trades = benchmark_book.place_order(
                msg.order_id, msg.agent_id, msg.order_type,
                msg.price, msg.volume);
            metrics.trades_generated += trades.size();
            metrics.orders_placed++;
        } else {
            benchmark_book.delete_order(msg.order_id);
            metrics.orders_cancelled++;
        }
        
        metrics.messages_processed++;
        
        // Track peak metrics
        size_t resting = benchmark_book.get_resting_orders_count();
        if (resting > metrics.peak_resting_orders) {
            metrics.peak_resting_orders = resting;
        }
        size_t levels = benchmark_book.get_buy_levels_count() + benchmark_book.get_sell_levels_count();
        if (levels > metrics.peak_levels) {
            metrics.peak_levels = levels;
        }
        
        // Progress reporting for large runs
        if (metrics.messages_processed % 10000000 == 0) {
            auto now = high_resolution_clock::now();
            auto elapsed = duration_cast<milliseconds>(now - start).count();
            double progress = 100.0 * metrics.messages_processed / messages.size();
            cout << "Progress: " << std::fixed << std::setprecision(1) << progress 
                 << "% (" << metrics.messages_processed << "/" << messages.size() 
                 << ") - " << elapsed << " ms elapsed" << endl;
        }
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<nanoseconds>(end - start);
    
    metrics.total_time_ms = duration.count() / 1e6;
    metrics.avg_latency_ns = static_cast<double>(duration.count()) / metrics.messages_processed;
    metrics.ops_per_sec = 1e9 / metrics.avg_latency_ns;
    metrics.trades_per_sec = (metrics.trades_generated * 1e9) / duration.count();
    metrics.final_resting_orders = benchmark_book.get_resting_orders_count();
    metrics.final_levels = benchmark_book.get_buy_levels_count() + benchmark_book.get_sell_levels_count();
    
    return metrics;
}

void print_metrics(const Metrics& m, const SimulationParams& params) {
    cout << "\n" << string(80, '=') << endl;
    cout << "REAL-WORLD ORDER BOOK SIMULATION METRICS" << endl;
    cout << string(80, '=') << endl;
    
    cout << "\n--- Simulation Parameters ---" << endl;
    cout << "  Total Messages:        " << std::setw(15) << params.total_messages << endl;
    cout << "  Cancel Rate:           " << std::setw(15) << std::fixed << std::setprecision(2) 
         << params.cancel_rate * 100 << "%" << endl;
    cout << "  Match Rate:            " << std::setw(15) << std::fixed << std::setprecision(2) 
         << params.match_rate * 100 << "%" << endl;
    cout << "  Price Range:           " << std::setw(15) << params.price_range_start 
         << " - " << params.price_range_end << endl;
    cout << "  Volume Range:          " << std::setw(15) << params.min_volume 
         << " - " << params.max_volume << endl;
    cout << "  Number of Agents:      " << std::setw(15) << params.num_agents << endl;
    
    cout << "\n--- Performance Metrics ---" << endl;
    cout << "  Total Time:            " << std::setw(15) << std::fixed << std::setprecision(2) 
         << m.total_time_ms << " ms" << endl;
    cout << "  Total Time:            " << std::setw(15) << std::fixed << std::setprecision(2) 
         << m.total_time_ms / 1000.0 << " seconds" << endl;
    cout << "  Average Latency:      " << std::setw(15) << std::fixed << std::setprecision(2) 
         << m.avg_latency_ns << " ns" << endl;
    cout << "  Average Latency:      " << std::setw(15) << std::fixed << std::setprecision(3) 
         << m.avg_latency_ns / 1000.0 << " μs" << endl;
    cout << "  Operations/Second:    " << std::setw(15) << std::fixed << std::setprecision(2) 
         << m.ops_per_sec / 1e6 << " M ops/sec" << endl;
    cout << "  Operations/Second:    " << std::setw(15) << std::fixed << std::setprecision(0) 
         << m.ops_per_sec << " ops/sec" << endl;
    
    cout << "\n--- Order Book Activity ---" << endl;
    cout << "  Orders Placed:         " << std::setw(15) << m.orders_placed << endl;
    cout << "  Orders Cancelled:     " << std::setw(15) << m.orders_cancelled << endl;
    cout << "  Trades Generated:     " << std::setw(15) << m.trades_generated << endl;
    cout << "  Trade Rate:           " << std::setw(15) << std::fixed << std::setprecision(2) 
         << m.trades_per_sec / 1e6 << " M trades/sec" << endl;
    
    cout << "\n--- Order Book State ---" << endl;
    cout << "  Peak Resting Orders:  " << std::setw(15) << m.peak_resting_orders << endl;
    cout << "  Final Resting Orders: " << std::setw(15) << m.final_resting_orders << endl;
    cout << "  Peak Price Levels:    " << std::setw(15) << m.peak_levels << endl;
    cout << "  Final Price Levels:   " << std::setw(15) << m.final_levels << endl;
    
    cout << "\n--- Performance Analysis ---" << endl;
    double fill_rate = static_cast<double>(m.trades_generated) / m.orders_placed;
    cout << "  Fill Rate:             " << std::setw(15) << std::fixed << std::setprecision(2) 
         << fill_rate * 100 << "%" << endl;
    
    if (m.avg_latency_ns < 100) {
        cout << "  Latency Grade:        " << std::setw(15) << "EXCELLENT (<100ns)" << endl;
    } else if (m.avg_latency_ns < 500) {
        cout << "  Latency Grade:        " << std::setw(15) << "VERY GOOD (<500ns)" << endl;
    } else if (m.avg_latency_ns < 1000) {
        cout << "  Latency Grade:        " << std::setw(15) << "GOOD (<1μs)" << endl;
    } else {
        cout << "  Latency Grade:        " << std::setw(15) << "NEEDS IMPROVEMENT" << endl;
    }
    
    if (m.ops_per_sec > 10e6) {
        cout << "  Throughput Grade:     " << std::setw(15) << "EXCELLENT (>10M ops/sec)" << endl;
    } else if (m.ops_per_sec > 1e6) {
        cout << "  Throughput Grade:     " << std::setw(15) << "VERY GOOD (>1M ops/sec)" << endl;
    } else {
        cout << "  Throughput Grade:     " << std::setw(15) << "GOOD" << endl;
    }
    
    cout << "\n" << string(80, '=') << endl;
}

int main(int argc, char** argv) {
    SimulationParams params;
    
    // Default realistic parameters (can be overridden via command line)
    params.total_messages = 10000000;  // 10 million messages
    if (argc > 1) {
        params.total_messages = std::stoull(argv[1]);
    }
    
    params.cancel_rate = 0.10;
    params.match_rate = 0.40;
    params.price_range_start = 9990;
    params.price_range_end = 10010;     
    params.min_volume = 1;
    params.max_volume = 1000;
    params.num_agents = 1000;         
    
    cout << "Generating " << params.total_messages << " realistic order book messages..." << endl;
    auto messages = generate_realistic_messages(params);
    cout << "Messages generated. Starting simulation..." << endl;
    
    Metrics metrics = run_simulation(messages);
    print_metrics(metrics, params);
    
    return 0;
}
