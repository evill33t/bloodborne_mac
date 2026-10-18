// FrpgNetConnectManStep / FrpgNetConnectStep debug controls, routes, QoS window and telemetry offsets.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// These workloads manage signaling peer connections; the lobby route
// corresponds to Matching2 lobbies, not a BB-specific backend.
//
// The module-level constants are kept in bb::connection_debug; the types are
// in bb.
namespace connection_debug {

inline constexpr std::size_t MANAGER_OFFSET = 0x30;
inline constexpr std::size_t CONNECTION_ID_OFFSET = 0x58;
inline constexpr std::size_t AVAILABLE_OFFSET = 0x5c;
inline constexpr std::size_t ROUTE_OFFSET = 0x60;
inline constexpr std::size_t SESSION_PROTECTED_OFFSET = 0xe4;
inline constexpr std::size_t PROTECTION_SECONDS_OFFSET = 0xe8;
inline constexpr std::size_t STEP_DEBUG_NODE_OFFSET = 0xf0;
inline constexpr std::int32_t INITIALIZE_STATE = 0;
inline constexpr std::int32_t HOLDER_COMPLETE_STATE = -1;
inline constexpr float PROTECTION_RENEWAL_SECONDS = 30.0f;

inline constexpr Rva STEP_DISPATCH{0x14808c0};
inline constexpr Rva STEP_RENEW_SESSION_PROTECTION{0x1480d10};
inline constexpr Rva STEP_INITIALIZE{0x1480e70};
inline constexpr Rva STEP_DEBUG_HEADER{0x14824e0};
inline constexpr Rva STEP_DEBUG_INPUT_NOOP{0x14829c0};
inline constexpr Rva STEP_DEBUG_STATISTICS{0x14829d0};
inline constexpr Rva DRAW_CONNECTION_DIAGRAM{0x1481d50};
inline constexpr Rva LOCKED_CONNECTION_STATUS{0x0c8dba0};
inline constexpr Rva LOCKED_CONNECTION_RESET{0x0c8dd60};

inline constexpr Rva START_BUDGET_CAP{0x5127ad8};
inline constexpr Rva START_REFILL_INTERVAL_SECONDS{0x5127adc};
inline constexpr Rva END_REFILL_INTERVAL_SECONDS{0x5127ae0};
inline constexpr std::int32_t INITIAL_START_BUDGET_CAP = 5;
inline constexpr float INITIAL_START_REFILL_SECONDS = 18.0f;
inline constexpr float INITIAL_END_REFILL_SECONDS = 36.0f;

inline constexpr std::size_t MANAGER_ACTIVE_COUNT_OFFSET = 0xb0;
inline constexpr std::size_t MANAGER_CONNECTED_ACTIVE_COUNT_OFFSET = 0xb4;
inline constexpr std::size_t MANAGER_AMBASSADOR_COUNT_OFFSET = 0xb8;
// The unmet portion, not the full FrpgNetMan+0xad0 input count.
inline constexpr std::size_t MANAGER_UNCOVERED_MAP_COUNT_OFFSET = 0xbc;
inline constexpr std::size_t MANAGER_NODE_SKIP_THRESHOLD_OFFSET = 0xc0;
inline constexpr std::size_t MANAGER_DRAW_DIAGRAM_OFFSET = 0xc4;

inline constexpr std::size_t FRPG_DESIRED_MAP_COUNT_OFFSET = 0xad0;
inline constexpr std::size_t FRPG_DESIRED_MAPS_OFFSET = 0xad4;
inline constexpr std::size_t FRPG_CONNECTED_COUNT_OFFSET = 0xbf0;
inline constexpr std::size_t FRPG_AMBASSADOR_COUNT_OFFSET = 0xbf4;
inline constexpr std::size_t FRPG_UNCOVERED_MAP_COUNT_OFFSET = 0xbf8;
inline constexpr std::size_t FRPG_UNCOVERED_MAPS_OFFSET = 0xbfc;
// A skip threshold, NOT the length of the preceding map array.
inline constexpr std::size_t FRPG_NODE_SKIP_THRESHOLD_OFFSET = 0xc3c;
inline constexpr Rva SELECT_NODE_CANDIDATES{0x149c210};

inline constexpr std::size_t TELEMETRY_QOS_OFFSET = 0x300;
inline constexpr std::size_t TELEMETRY_SEND_S_BYTES_OFFSET = 0x118;
inline constexpr std::size_t TELEMETRY_SEND_M_BYTES_OFFSET = 0x11c;
inline constexpr std::size_t TELEMETRY_SEND_P_COUNT_OFFSET = 0x120;
inline constexpr std::size_t TELEMETRY_RECV_S_BYTES_OFFSET = 0x21c;
inline constexpr std::size_t TELEMETRY_RECV_M_BYTES_OFFSET = 0x220;
inline constexpr std::size_t TELEMETRY_RECV_P_COUNT_OFFSET = 0x224;
inline constexpr std::size_t TELEMETRY_MAP_ID_OFFSET = 0xcc;
inline constexpr std::size_t TELEMETRY_MAP_REPRESENTATIVE_FLAG_OFFSET = 0x329;
inline constexpr std::size_t TELEMETRY_PROTECTED_COPY_OFFSET = 0x32c;

}  // namespace connection_debug

// Values printed literally in the connection-step headline. These describe
// discovery/connection routes, not SprjSessionManager host/client roles.
enum class ConnectionRoute : std::uint32_t {
    None = 0,
    Passive = 1,
    Lobby = 2,
    Node = 3,
    Sos = 4,
};

// Field window at telemetry+0x300. Units and order are confirmed by the debug
// formatting callback, including latest/average pairs. Not a timer to write.
struct ConnectionQosWindow {
    std::uint32_t srtt_latest_ms;
    std::uint32_t rtt_variation_latest_ms;
    std::uint32_t srtt_average_ms;
    std::uint32_t rtt_variation_average_ms;
    std::uint32_t sent_packets;
    std::uint32_t retransmitted_packets;
    std::uint32_t sent_bytes_per_second;
    std::uint32_t received_packets;
    std::uint32_t received_valid_packets;
    std::uint32_t received_bytes_per_second;
};

}  // namespace bb
