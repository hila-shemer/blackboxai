// T2 boundary smoke: proves the M3 wlroots headers (seat, cursor, xcursor,
// edges, xdg-decoration, server-decoration) parse through toolkit/wlr.hpp under
// C++20. Referencing each type forces its header to be complete; a missing
// include fails to *compile*.
#include <doctest/doctest.h>
#include "wlr.hpp"

TEST_CASE("M3 input + decoration headers parse through wlr.hpp") {
    // Pointers to the newly-exposed types (complete types not required to
    // declare, but referencing the tags proves the headers were included).
    wlr_seat *seat = nullptr;
    wlr_cursor *cursor = nullptr;
    wlr_xcursor_manager *xcursor = nullptr;
    wlr_xdg_decoration_manager_v1 *deco_mgr = nullptr;
    wlr_server_decoration_manager *kde_deco = nullptr;
    wlr_pointer_button_event *btn = nullptr;  // via wlr_seat.h -> wlr_pointer.h
    (void)seat; (void)cursor; (void)xcursor; (void)deco_mgr; (void)kde_deco; (void)btn;

    // The resize-edge bitmask the grip code uses.
    CHECK((WLR_EDGE_BOTTOM | WLR_EDGE_LEFT) == ((1 << 1) | (1 << 2)));
    // The decoration mode enum the SSD/CSD policy switches on.
    CHECK(WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE !=
          WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
}
