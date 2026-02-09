#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <unordered_map>
#include "LOB/Types.h"

namespace quantumflow {

/// Converts between double prices (market data) and uint32_t PRICE (LOB internal).
/// Scale factor determines precision: e.g., 100 means 2 decimal places (cents).
class PriceConverter {
public:
    explicit PriceConverter(double scale_factor = 100.0)
        : scale_factor_(scale_factor)
        , inv_scale_(1.0 / scale_factor)
    {}

    PRICE to_internal(double external_price) const {
        return static_cast<PRICE>(std::llround(external_price * scale_factor_));
    }

    double to_external(PRICE internal_price) const {
        return static_cast<double>(internal_price) * inv_scale_;
    }

    double scale_factor() const { return scale_factor_; }

private:
    double scale_factor_;
    double inv_scale_;
};

/// Per-symbol registry of price converters.
class PriceConverterRegistry {
public:
    PriceConverterRegistry(double default_scale = 100.0)
        : default_scale_(default_scale)
    {}

    void set_scale(const std::string& symbol, double scale) {
        converters_.insert_or_assign(symbol, PriceConverter(scale));
    }

    const PriceConverter& get(const std::string& symbol) const {
        auto it = converters_.find(symbol);
        if (it != converters_.end()) {
            return it->second;
        }
        return default_;
    }

private:
    double default_scale_;
    PriceConverter default_{100.0};
    std::unordered_map<std::string, PriceConverter> converters_;
};

} // namespace quantumflow
