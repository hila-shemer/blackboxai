// Pure MRU list — move-to-front, insert-on-touch, erase, and the frozen-ring
// step() wraparound the alt-tab cycle relies on. No wlroots; main comes from
// smoke_test.cc.
#include <doctest/doctest.h>

#include "Mru.hh"

#include <vector>

using namespace bbai;

TEST_CASE("touch inserts unknown items at the front, most-recent first") {
  Mru<int> m;
  int a = 1, b = 2, c = 3;
  CHECK(m.empty());
  m.touch(&a);
  m.touch(&b);
  m.touch(&c);
  CHECK(m.size() == 3);
  CHECK(m.snapshot() == std::vector<int *>{&c, &b, &a});
}

TEST_CASE("touch on a known item promotes it to the front") {
  Mru<int> m;
  int a = 1, b = 2, c = 3;
  m.touch(&a);
  m.touch(&b);
  m.touch(&c);
  m.touch(&a);   // a was last -> now first, no duplicate
  CHECK(m.snapshot() == std::vector<int *>{&a, &c, &b});
  CHECK(m.size() == 3);
}

TEST_CASE("erase drops an item and preserves the rest of the order") {
  Mru<int> m;
  int a = 1, b = 2, c = 3;
  m.touch(&a);
  m.touch(&b);
  m.touch(&c);
  m.erase(&b);
  CHECK(m.snapshot() == std::vector<int *>{&c, &a});
  m.erase(&b);   // erasing an absent item is a no-op
  CHECK(m.snapshot() == std::vector<int *>{&c, &a});
}

TEST_CASE("step wraps forward and backward over a frozen ring") {
  // Forward from each index, n=3: 0->1->2->0.
  CHECK(Mru<int>::step(0, +1, 3) == 1);
  CHECK(Mru<int>::step(1, +1, 3) == 2);
  CHECK(Mru<int>::step(2, +1, 3) == 0);
  // Backward: 0->2->1->0.
  CHECK(Mru<int>::step(0, -1, 3) == 2);
  CHECK(Mru<int>::step(2, -1, 3) == 1);
  CHECK(Mru<int>::step(1, -1, 3) == 0);
}

TEST_CASE("step is a no-op for degenerate ring sizes") {
  CHECK(Mru<int>::step(0, +1, 0) == 0);   // empty ring
  CHECK(Mru<int>::step(0, -1, 0) == 0);
  CHECK(Mru<int>::step(0, +1, 1) == 0);   // single item: stays put both ways
  CHECK(Mru<int>::step(0, -1, 1) == 0);
}
