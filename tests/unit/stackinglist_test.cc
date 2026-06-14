// A1: the ported 5-sentinel layered StackingList. Pure L0 — no compositor.
#include <doctest/doctest.h>
#include "StackingList.hh"

#include <vector>

using namespace bbai;

namespace {
  struct FakeEntity : StackEntity {
    int id;
    explicit FakeEntity(int i, StackingList::Layer l = StackingList::LayerNormal) : id(i) {
      setLayer(l);
    }
    void *windowID() const override { return const_cast<FakeEntity *>(this); }
  };

  // Non-sentinel entities, top-to-bottom.
  std::vector<StackEntity *> order(StackingList &s) {
    std::vector<StackEntity *> v;
    for (auto it = s.begin(); it != s.end(); ++it)
      if (*it) v.push_back(*it);
    return v;
  }
}

TEST_CASE("a fresh StackingList is empty with 5 sentinels") {
  StackingList s;
  CHECK(s.empty());
  CHECK(s.size() == 0);
  CHECK(order(s).empty());
}

TEST_CASE("insert stacks at the top of the layer; front/back track the ends") {
  StackingList s;
  FakeEntity a(1), b(2), c(3);
  s.insert(&a);
  CHECK_FALSE(s.empty());
  CHECK(s.size() == 1);
  CHECK(s.front() == &a);
  CHECK(s.back() == &a);

  s.insert(&b);   // b goes above a
  s.insert(&c);   // c goes above b
  CHECK(s.size() == 3);
  CHECK(s.front() == &c);
  CHECK(s.back() == &a);
  CHECK(order(s) == std::vector<StackEntity *>{&c, &b, &a});
  // invariant: visible count == size(), and empty() agrees
  CHECK(order(s).size() == s.size());
  CHECK(s.empty() == (s.size() == 0));
}

TEST_CASE("append adds to the bottom of the layer") {
  StackingList s;
  FakeEntity a(1), b(2);
  s.insert(&a);
  s.append(&b);   // b goes below a
  CHECK(order(s) == std::vector<StackEntity *>{&a, &b});
  CHECK(s.front() == &a);
  CHECK(s.back() == &b);
}

TEST_CASE("remove drops an entity and keeps the 5 sentinels") {
  StackingList s;
  FakeEntity a(1), b(2), c(3);
  s.insert(&a); s.insert(&b); s.insert(&c);  // order c,b,a
  s.remove(&b);
  CHECK(s.size() == 2);
  CHECK(order(s) == std::vector<StackEntity *>{&c, &a});
  s.remove(&c);  // remove the layer-top entity (advances the layer iterator)
  CHECK(s.size() == 1);
  CHECK(s.front() == &a);
  s.remove(&a);
  CHECK(s.empty());
  CHECK(s.size() == 0);
}

TEST_CASE("raise/lower reorder within the layer; edges are no-ops") {
  StackingList s;
  FakeEntity a(1), b(2), c(3);
  s.insert(&a); s.insert(&b); s.insert(&c);  // c,b,a

  s.raise(&a);                               // a -> top
  CHECK(order(s) == std::vector<StackEntity *>{&a, &c, &b});
  s.raise(&a);                               // already top: no-op
  CHECK(order(s) == std::vector<StackEntity *>{&a, &c, &b});

  s.lower(&a);                               // a -> bottom
  CHECK(order(s) == std::vector<StackEntity *>{&c, &b, &a});
  s.lower(&a);                               // already bottom: no-op
  CHECK(order(s) == std::vector<StackEntity *>{&c, &b, &a});
}

TEST_CASE("layers order front()/back() across the 5 layers") {
  StackingList s;
  FakeEntity n(1, StackingList::LayerNormal);
  FakeEntity f(2, StackingList::LayerFullScreen);
  FakeEntity b(3, StackingList::LayerBelow);
  FakeEntity d(4, StackingList::LayerDesktop);
  s.insert(&n); s.insert(&f); s.insert(&b); s.insert(&d);
  CHECK(s.size() == 4);
  // fullscreen is topmost; desktop is never returned by front().
  CHECK(s.front() == &f);
  // back() also skips the desktop layer -> the lowest *non-desktop* entity is
  // the Below-layer one. (Desktop entities are wallpaper, never raised/lowered.)
  CHECK(s.back() == &b);
  // top-to-bottom across all layers: fullscreen, normal, below, desktop.
  CHECK(order(s) == std::vector<StackEntity *>{&f, &n, &b, &d});
}

TEST_CASE("changeLayer moves an entity between layers") {
  StackingList s;
  FakeEntity a(1, StackingList::LayerNormal);
  FakeEntity b(2, StackingList::LayerNormal);
  s.insert(&a); s.insert(&b);                 // b,a in normal
  s.changeLayer(&a, StackingList::LayerAbove);
  CHECK(a.layer() == StackingList::LayerAbove);
  CHECK(s.size() == 2);
  // a is now in the Above layer, which stacks over Normal -> a is front.
  CHECK(s.front() == &a);
  CHECK(order(s) == std::vector<StackEntity *>{&a, &b});
}
