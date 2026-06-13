#ifndef LOB_TYPES_H
#define LOB_TYPES_H

#include <cstdint>

namespace quantumflow::lob {

using ID = std::uint64_t;
using PRICE = std::uint32_t;
using Volume = std::uint64_t;
using Length = std::uint64_t;

enum OrderType { BUY, SELL };
enum OrderStatus { ACTIVE, FULFILLED, DELETED };

} // namespace quantumflow::lob

#endif // LOB_TYPES_H