#include <nuraft_mesg/nuraft_mesg.hpp>

#include "uuids.h"

namespace nuraft_mesg {

std::string lookup_peer(nuraft_mesg::peer_id_t const& peer) {
    // Provide a method for the service layer to lookup an IPv4:port address
    // from a uuid; however the process wants to do that.
    auto id_str = to_string(peer);
    for (auto i = 0u; i < 5; ++i) {
        if (uuids[i] == id_str) { return fmt::format(FMT_STRING("127.0.0.1:{}"), 9000 + i); }
    }
    RELEASE_ASSERT(false, "Missing Peer: {}", peer);
    return std::string();
}

} // namespace nuraft_mesg
