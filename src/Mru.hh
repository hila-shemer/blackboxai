// Most-recently-used order over a set of pointers. The single source of
// "last-used order" for alt-tab (spec docs/superpowers/specs/2026-06-28-alt-tab-mru-design.md
// §3.1) — independent of the z-order StackingList. Pure and header-only: the
// ordering + cycle math carry zero wlroots state, so they unit-test headless.
#ifndef BLACKBOXAI_MRU_HH
#define BLACKBOXAI_MRU_HH

#include <algorithm>
#include <cstddef>
#include <vector>

namespace bbai {

  // Non-owning; T outlives the entries (callers erase() on destruction).
  template <class T>
  class Mru {
  public:
    // Promote p to the front (most-recent). Inserts it if not yet tracked, so
    // this doubles as "add a new item at front."
    void touch(T *p) {
      erase(p);
      v_.insert(v_.begin(), p);
    }

    void erase(T *p) { std::erase(v_, p); }

    const std::vector<T *> &snapshot() const { return v_; }
    bool empty() const { return v_.empty(); }
    std::size_t size() const { return v_.size(); }

    // Advance an index over a frozen ring of n items with wraparound: dir>=0
    // steps forward (next-most-recent), dir<0 steps back. n==0 stays put.
    static std::size_t step(std::size_t idx, int dir, std::size_t n) {
      if (n == 0) return 0;
      return (idx + (dir >= 0 ? 1 : n - 1)) % n;
    }

  private:
    std::vector<T *> v_;   // most-recent first
  };

} // namespace bbai

#endif // BLACKBOXAI_MRU_HH
