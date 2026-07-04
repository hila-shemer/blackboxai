#include "Slit.hh"
#include "Server.hh"
#include "Output.hh"
#include "Toolbar.hh"
#include "SniHost.hh"
#include "DataBuffer.hh"
#include "Style.hh"
#include "Image.hh"

namespace bbai {

  std::vector<uint32_t> sliticon::cellPixels(const sni::Item &item, int slot) {
    // Bus pixmap first - exactly what the app rendered. SNI bytes are STRAIGHT
    // alpha; wlroots blends premultiplied - convert here, then nearest-scale
    // on the CPU so the raster is renderer-independent (byte-stable goldens).
    if (const sni::IconFrame *f = sni::pickBestFrame(item.icon_pixmaps, slot)) {
      std::vector<uint32_t> px = f->toNative();
      slit::premultiply(px);
      return slit::nearestScale(px, f->width, f->height, slot);
    }
    // (the icon_name theme-PNG fallback task inserts its rung here)
    // Placeholder: a grey ring inset 4px - visibly "item without an icon",
    // not a hole a user reads as a compositor bug.
    std::vector<uint32_t> px(static_cast<std::size_t>(slot) * slot, 0);
    const int a = 4, b = slot - 5;
    for (int i = a; i <= b; ++i) {
      px[static_cast<std::size_t>(a) * slot + i] = 0xFFAAAAAAu;
      px[static_cast<std::size_t>(b) * slot + i] = 0xFFAAAAAAu;
      px[static_cast<std::size_t>(i) * slot + a] = 0xFFAAAAAAu;
      px[static_cast<std::size_t>(i) * slot + b] = 0xFFAAAAAAu;
    }
    return px;
  }

  Slit::Slit(Server &server, Output &output)
    : server_(server), output_(output),
      ow_(output.wlrOutput()->width), oh_(output.wlrOutput()->height) {
    tree_ = wlr_scene_tree_create(server_.layer_top);
    output_.addStrut(&strut_);
    hide_timer_ = std::make_unique<Timer>(server_.timerRegistry(), hide_handler_);
    refresh();
  }

  Slit::~Slit() {
    output_.removeStrut(&strut_);
    hide_timer_.reset();          // unregister before the registry
    clearNodes();
    if (tree_) wlr_scene_node_destroy(&tree_->node);
  }

  slit::Metrics Slit::metrics() const {
    std::shared_ptr<const Style> st = server_.currentStyle();
    return { st->slitMargin(),
             static_cast<int>(st->slitTexture().borderWidth()),
             slit::kSlot };
  }

  void Slit::clearNodes() {
    for (wlr_scene_node *n : nodes_) wlr_scene_node_destroy(n);
    nodes_.clear();
  }

  void Slit::emit(slit::Rect r, std::vector<uint32_t> px) {
    DataBuffer *buf = DataBuffer::create(static_cast<uint32_t>(r.w),
                                         static_cast<uint32_t>(r.h), std::move(px));
    wlr_scene_buffer *sb = wlr_scene_buffer_create(tree_, buf->base());
    wlr_buffer_drop(buf->base());
    wlr_scene_node_set_position(&sb->node, r.x, r.y);
    nodes_.push_back(&sb->node);
  }

  void Slit::refresh() {
    clearNodes();
    sni::Host *host = server_.sniHostOrNull();
    item_count_ = (host && host->ok()) ? host->items().size() : 0;
    if (item_count_ == 0) {
      // Classic destroys the empty slit (Slit.cc:196-197); we disable - the
      // object stays, the pixels and the strut go.
      wlr_scene_node_set_enabled(&tree_->node, false);
      strut_ = Strut{};
      output_.strutsChanged();
      return;
    }
    wlr_scene_node_set_enabled(&tree_->node, true);

    const slit::Metrics m = metrics();
    const slit::Rect size =
      slit::frameSize(static_cast<int>(item_count_), direction_, m);
    shown_ = slit::placeFrame(size, placement_, ow_, oh_);
    // Classic overlap avoidance: shift off an intersecting toolbar by its
    // exposed height. applyConfig applies toolbar knobs before slit knobs,
    // so this reads the bar's FINAL rect.
    if (Toolbar *tb = server_.toolbarOrNull()) {
      const toolbar::Rect b = tb->currentBarRect();
      shown_.y = slit::toolbarShift(shown_, {b.x, b.y, b.w, b.h}, tb->exposedHeight());
    }
    hidden_rect_ = slit::hiddenRect(shown_, placement_, m.margin, ow_, oh_);

    std::shared_ptr<const Style> st = server_.currentStyle();
    emit({0, 0, shown_.w, shown_.h},
         bt::Image(shown_.w, shown_.h).renderBuffer(st->slitTexture()));
    for (std::size_t i = 0; i < item_count_; ++i)
      emit(slit::itemRect(static_cast<int>(i), direction_, m),
           sliticon::cellPixels(host->items()[i], m.slot));

    applyPosition();
    updateStrut();
  }

  void Slit::applyOptions(const SlitConfig &opts) {
    placement_ = opts.placement;
    direction_ = opts.direction;
    // opts.alwaysOnTop: parsed-but-inert (program law - no layer machinery,
    // no lying knobs; documented once, here).
    refresh();
    setAutoHide(opts.autoHide);
  }

  void Slit::applyPosition() {
    const slit::Rect r = hidden_ ? hidden_rect_ : shown_;
    wlr_scene_node_set_position(&tree_->node, r.x, r.y);
  }

  void Slit::updateStrut() {
    strut_ = (item_count_ == 0)
      ? Strut{}
      : slit::strutFor(direction_, placement_, shown_, hidden_rect_,
                       auto_hide_, metrics(), oh_);
    output_.strutsChanged();
  }

  void Slit::setAutoHide(bool on) {
    auto_hide_ = on;
    hidden_ = on;   // reconfigure re-hides - same semantics as Toolbar::setAutoHide
    applyPosition();
    updateStrut();
  }

  bool Slit::containsGlobal(int gx, int gy) const {
    if (item_count_ == 0) return false;
    return gx >= shown_.x && gx < shown_.x + shown_.w &&
           gy >= shown_.y && gy < shown_.y + shown_.h;
  }

  int Slit::itemIndexAtGlobal(int gx, int gy) const {
    if (item_count_ == 0) return -1;
    return slit::itemIndexAt(gx - shown_.x, gy - shown_.y,
                             static_cast<int>(item_count_), direction_, metrics());
  }

  slit::Rect Slit::itemRectGlobalForTest(int i) const {
    const slit::Rect r = slit::itemRect(i, direction_, metrics());
    return { shown_.x + r.x, shown_.y + r.y, r.w, r.h };
  }

  void Slit::handlePointerMotion(double x, double y) {
    if (item_count_ == 0) return;
    const bool over = (x >= shown_.x && x < shown_.x + shown_.w &&   // SHOWN footprint
                       y >= shown_.y && y < shown_.y + shown_.h);    //  = the hot zone
    onPointerOverSlit(over);
  }

  void Slit::onPointerOverSlit(bool over) {
    if (!auto_hide_) return;
    const bool want_shown = over;
    if (want_shown == !hidden_) { hide_timer_->stop(); return; }
    hide_timer_->start(kHideDelayMs, /*recurring=*/false);
  }

  void Slit::onHideTimeout() {
    hidden_ = !hidden_;
    applyPosition();
  }

} // namespace bbai
