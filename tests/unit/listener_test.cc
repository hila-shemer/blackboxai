#include <doctest/doctest.h>
#include "listener.hpp"

TEST_CASE("Listener fires on signal and stops after disconnect") {
    wl_signal sig;
    wl_signal_init(&sig);
    int count = 0;
    {
        bt::Listener l;
        l.connect(&sig, [&](void *) { ++count; });
        wl_signal_emit_mutable(&sig, nullptr);
        CHECK(count == 1);
    } // dtor disconnects
    wl_signal_emit_mutable(&sig, nullptr);
    CHECK(count == 1); // no further increments
}

TEST_CASE("Listener reconnect replaces the previous subscription") {
    wl_signal a, b;
    wl_signal_init(&a);
    wl_signal_init(&b);
    int hits = 0;
    bt::Listener l;
    l.connect(&a, [&](void *) { ++hits; });
    l.connect(&b, [&](void *) { ++hits; }); // moves the subscription to b
    wl_signal_emit_mutable(&a, nullptr);     // old signal: no effect
    CHECK(hits == 0);
    wl_signal_emit_mutable(&b, nullptr);
    CHECK(hits == 1);
}
