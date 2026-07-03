#include "Style.hh"

#include <algorithm>

namespace bbai {

namespace {

  // Classic 9x9 xbm bitmaps (window buttons, toolbar arrows, menu check) -
  // the constant the classic metric formulas measured. Our glyphs are drawn
  // procedurally but keep the same nominal box.
  constexpr int kGlyphSize = 9;

  // Style font keys are fontconfig patterns; an absent key gets our M3 font.
  std::unique_ptr<bt::TextRenderer> makeFont(const std::string &pattern) {
    if (pattern.empty())
      return std::make_unique<bt::TextRenderer>("monospace", 16);
    return std::make_unique<bt::TextRenderer>(pattern);
  }

  unsigned maxBw(const bt::Texture &a, const bt::Texture &b) {
    return std::max(a.borderWidth(), b.borderWidth());
  }

  bt::Texture flatBlack() {
    bt::Texture t;
    t.setDescription("flat solid");
    t.setColor1(bt::Color(0, 0, 0));
    return t;
  }

  // PR sanity for containers (ScreenResource.cc:514-537): a container drawn
  // parentrelative had the ROOT show through in X; we have no root window, so
  // classic's own flat-black replacement applies. Labels/buttons stay PR and
  // are honored by parent pixel-copy at emit time.
  void sanitizeContainer(bt::Texture &t) {
    if (t.texture() == bt::Texture::Parent_Relative) t = flatBlack();
  }

  bt::Color color(const bt::Resource &res, const std::string &n,
                  const std::string &c, const char *dflt) {
    bt::Color col = bt::Color::fromString(res.read(n, c, dflt));
    return col.valid() ? col : bt::Color(0, 0, 0);
  }

  // The current hardcoded look (DecorationPalette.hh + Toolbar.cc kLooks +
  // Menu.cc kLooks + Server.cc:108's desktop), spelled as a style file. When
  // this string and those tables disagree, the golden suite catches it.
  const char kBuiltinStyle[] =
    "BlackboxAI.desktop: flat gradient diagonal\n"
    "BlackboxAI.desktop.color:   #204060\n"
    "BlackboxAI.desktop.colorTo: #6080a0\n"
    "window.title.focus: raised gradient diagonal\n"
    "window.title.focus.color: #c0c0c0\n"
    "window.title.focus.colorTo: #808080\n"
    "window.title.unfocus: raised gradient diagonal\n"
    "window.title.unfocus.color: #909090\n"
    "window.title.unfocus.colorTo: #606060\n"
    "window.label.focus: sunken gradient diagonal\n"
    "window.label.focus.color: #b8b8b8\n"
    "window.label.focus.colorTo: #888888\n"
    "window.label.unfocus: sunken gradient diagonal\n"
    "window.label.unfocus.color: #909090\n"
    "window.label.unfocus.colorTo: #686868\n"
    "window.handle.focus: raised gradient diagonal\n"
    "window.handle.focus.color: #c0c0c0\n"
    "window.handle.focus.colorTo: #808080\n"
    "window.handle.unfocus: raised gradient diagonal\n"
    "window.handle.unfocus.color: #909090\n"
    "window.handle.unfocus.colorTo: #606060\n"
    "window.grip.focus: raised gradient diagonal\n"
    "window.grip.focus.color: #d8d8d8\n"
    "window.grip.focus.colorTo: #909090\n"
    "window.grip.unfocus: raised gradient diagonal\n"
    "window.grip.unfocus.color: #a0a0a0\n"
    "window.grip.unfocus.colorTo: #707070\n"
    "window.button.focus: raised gradient diagonal\n"
    "window.button.focus.color: #e0e0e0\n"
    "window.button.focus.colorTo: #a8a8a8\n"
    "window.button.unfocus: raised gradient diagonal\n"
    "window.button.unfocus.color: #a8a8a8\n"
    "window.button.unfocus.colorTo: #808080\n"
    "window.label.focus.textColor: black\n"
    "window.label.unfocus.textColor: #606060\n"
    "window.button.focus.picColor: #202020\n"
    "window.button.unfocus.picColor: #606060\n"
    "window.frame.focus.borderColor: #303030\n"
    "window.frame.unfocus.borderColor: #484848\n"
    "toolbar: raised gradient diagonal\n"
    "toolbar.color: #c0c0c0\n"
    "toolbar.colorTo: #808080\n"
    "toolbar.label: sunken gradient diagonal\n"
    "toolbar.label.color: #b8b8b8\n"
    "toolbar.label.colorTo: #888888\n"
    "toolbar.windowLabel: sunken gradient diagonal\n"
    "toolbar.windowLabel.color: #b8b8b8\n"
    "toolbar.windowLabel.colorTo: #888888\n"
    "toolbar.clock: sunken gradient diagonal\n"
    "toolbar.clock.color: #b8b8b8\n"
    "toolbar.clock.colorTo: #888888\n"
    "toolbar.button: raised gradient diagonal\n"
    "toolbar.button.color: #e0e0e0\n"
    "toolbar.button.colorTo: #a8a8a8\n"
    "toolbar.label.textColor: black\n"
    "toolbar.windowLabel.textColor: black\n"
    "toolbar.clock.textColor: black\n"
    "toolbar.button.picColor: #202020\n"
    "menu.title: raised gradient diagonal\n"
    "menu.title.color: #b0b0b0\n"
    "menu.title.colorTo: #888888\n"
    "menu.title.textColor: black\n"
    "menu.frame: raised gradient diagonal\n"
    "menu.frame.color: #c8c8c8\n"
    "menu.frame.colorTo: #a8a8a8\n"
    "menu.frame.textColor: black\n"
    "menu.frame.foregroundColor: #606060\n"
    "menu.frame.disabledColor: #707070\n"
    "menu.active: raised gradient diagonal\n"
    "menu.active.color: #5a7abf\n"
    "menu.active.colorTo: #33558f\n"
    "menu.active.textColor: white\n";

} // namespace

std::shared_ptr<const Style> Style::load(const std::string &path) {
  bt::Resource res(path);
  if (!res.valid()) return nullptr;
  return fromResource(res, path);
}

std::shared_ptr<const Style> Style::fromResource(const bt::Resource &res,
                                                 std::string source_path) {
  auto s = std::shared_ptr<Style>(new Style);
  s->source_path_ = std::move(source_path);

  // --- fonts (fontconfig patterns; Task 5's ctor) ---
  s->window_font_     = makeFont(res.read("window.font", "Window.Font", ""));
  s->toolbar_font_    = makeFont(res.read("toolbar.font", "Toolbar.Font", ""));
  s->menu_title_font_ = makeFont(res.read("menu.title.font", "Menu.Title.Font", ""));
  s->menu_frame_font_ = makeFont(res.read("menu.frame.font", "Menu.Frame.Font", ""));

  // --- window looks (ScreenResource.cc:269-361, defaults verbatim) ---
  WindowLook &f = s->window_focus_;
  f.text = color(res, "window.label.focus.textColor", "Window.Label.Focus.TextColor", "black");
  f.foreground = bt::Color::fromString(
    res.read("window.button.focus.foregroundColor", "Window.Button.Focus.ForegroundColor",
             res.read("window.button.focus.picColor", "Window.Button.Focus.PicColor", "black")));
  if (!f.foreground.valid()) f.foreground = bt::Color(0, 0, 0);
  f.title  = bt::textureResource(res, "window.title.focus",  "Window.Title.Focus",  "white");
  f.label  = bt::textureResource(res, "window.label.focus",  "Window.Label.Focus",  "white");
  f.button = bt::textureResource(res, "window.button.focus", "Window.Button.Focus", "white");
  f.handle = bt::textureResource(res, "window.handle.focus", "Window.Handle.Focus", "white");
  f.grip   = bt::textureResource(res, "window.grip.focus",   "Window.Grip.Focus",   "white");
  f.frameBorder = color(res, "window.frame.focus.borderColor",
                        "Window.Frame.Focus.BorderColor", "white");

  WindowLook &u = s->window_unfocus_;
  u.text = color(res, "window.label.unfocus.textColor", "Window.Label.Unfocus.TextColor", "white");
  u.foreground = bt::Color::fromString(
    res.read("window.button.unfocus.foregroundColor", "Window.Button.Unfocus.ForegroundColor",
             res.read("window.button.unfocus.picColor", "Window.Button.Unfocus.PicColor", "white")));
  if (!u.foreground.valid()) u.foreground = bt::Color(0, 0, 0);
  u.title  = bt::textureResource(res, "window.title.unfocus",  "Window.Title.Unfocus",  "black");
  u.label  = bt::textureResource(res, "window.label.unfocus",  "Window.Label.Unfocus",  "black");
  u.button = bt::textureResource(res, "window.button.unfocus", "Window.Button.Unfocus", "black");
  u.handle = bt::textureResource(res, "window.handle.unfocus", "Window.Handle.Unfocus", "black");
  u.grip   = bt::textureResource(res, "window.grip.unfocus",   "Window.Grip.Unfocus",   "black");
  u.frameBorder = color(res, "window.frame.unfocus.borderColor",
                        "Window.Frame.Unfocus.BorderColor", "black");

  s->window_pressed_ =
    bt::textureResource(res, "window.button.pressed", "Window.Button.Pressed", "black");

  const int title_margin  = res.read("window.title.marginWidth",  "Window.Title.MarginWidth", 2);
  const int label_margin  = res.read("window.label.marginWidth",  "Window.Label.MarginWidth", 2);
  const int button_margin = res.read("window.button.marginWidth", "Window.Button.MarginWidth", 2);
  const int frame_bw      = res.read("window.frame.borderWidth",  "Window.Frame.BorderWidth", 1);
  const int handle_height = res.read("window.handleHeight",       "Window.HandleHeight", 6);

  // --- computed frame metrics (ScreenResource.cc:377-407, bitmaps = 9x9) ---
  {
    frame::FrameMetrics m;
    int button_w = kGlyphSize
      + (static_cast<int>(maxBw(f.button, u.button)) + button_margin) * 2;
    int label_h = std::max(
      s->window_font_->height()
        + (static_cast<int>(maxBw(f.label, u.label)) + label_margin) * 2,
      button_w);
    button_w = std::max(button_w, label_h);
    m.labelHeight = label_h;
    m.buttonWidth = button_w;
    m.titleMargin = static_cast<int>(maxBw(f.title, u.title)) + title_margin;
    m.titleHeight = label_h + m.titleMargin * 2;
    m.gripWidth = button_w * 2;
    m.handleHeight = handle_height + static_cast<int>(maxBw(f.handle, u.handle)) * 2;
    m.border = frame_bw;
    s->frame_ = m;
  }

  // --- toolbar look (ScreenResource.cc:409-496) ---
  ToolbarLook &t = s->toolbar_;
  t.bar     = bt::textureResource(res, "toolbar",             "Toolbar",              "white");
  t.slabel  = bt::textureResource(res, "toolbar.label",       "Toolbar.Label",        "white");
  t.wlabel  = bt::textureResource(res, "toolbar.windowLabel", "Toolbar.Label",        "white");
  t.button  = bt::textureResource(res, "toolbar.button",      "Toolbar.Button",       "white");
  t.pressed = bt::textureResource(res, "toolbar.button.pressed", "Toolbar.Button.Pressed", "black");
  t.clock   = bt::textureResource(res, "toolbar.clock",       "Toolbar.Label",        "white");
  t.slabelText = color(res, "toolbar.label.textColor",       "Toolbar.Label.TextColor", "black");
  t.wlabelText = color(res, "toolbar.windowLabel.textColor", "Toolbar.Label.TextColor", "black");
  t.clockText  = color(res, "toolbar.clock.textColor",       "Toolbar.Label.TextColor", "black");
  t.foreground = bt::Color::fromString(
    res.read("toolbar.button.foregroundColor", "Toolbar.Button.ForegroundColor",
             res.read("toolbar.button.picColor", "Toolbar.Button.PicColor", "black")));
  if (!t.foreground.valid()) t.foreground = bt::Color(0, 0, 0);

  const int tb_frame_margin  = res.read("toolbar.marginWidth",        "Toolbar.MarginWidth", 2);
  const int tb_label_margin  = res.read("toolbar.label.marginWidth",  "Toolbar.Label.MarginWidth", 2);
  const int tb_button_margin = res.read("toolbar.button.marginWidth", "Toolbar.Button.MarginWidth", 2);

  {
    toolbar::ToolbarMetrics m;
    m.frameMargin = tb_frame_margin;
    m.labelMargin = tb_label_margin;
    int button_w = kGlyphSize
      + (static_cast<int>(t.button.borderWidth()) + tb_button_margin) * 2;
    const unsigned lbw = std::max({t.slabel.borderWidth(), t.wlabel.borderWidth(),
                                   t.clock.borderWidth()});
    int label_h = std::max(
      s->toolbar_font_->height() + (static_cast<int>(lbw) + tb_label_margin) * 2,
      button_w);
    button_w = std::max(button_w, label_h);
    m.labelHeight = label_h;
    m.buttonWidth = button_w;
    m.barHeight = label_h
      + (static_cast<int>(t.bar.borderWidth()) + tb_frame_margin) * 2;
    m.hiddenHeight = std::max(
      static_cast<int>(t.bar.borderWidth()) + tb_frame_margin, 1);
    s->toolbar_metrics_ = m;
  }

  // --- menu look (lib/Menu.cc:69-145) ---
  MenuLook &mn = s->menu_;
  mn.title  = bt::textureResource(res, "menu.title",  "Menu.Title",  "black");
  mn.frame  = bt::textureResource(res, "menu.frame",  "Menu.Frame",  "white");
  mn.active = bt::textureResource(res, "menu.active", "Menu.Active", "black");
  mn.titleForeground = color(res, "menu.title.foregroundColor", "Menu.Title.ForegroundColor", "white");
  mn.titleText       = color(res, "menu.title.textColor",       "Menu.Title.TextColor",       "white");
  mn.frameForeground = color(res, "menu.frame.foregroundColor", "Menu.Frame.ForegroundColor", "black");
  mn.frameText       = color(res, "menu.frame.textColor",       "Menu.Frame.TextColor",       "black");
  mn.frameDisabled   = color(res, "menu.frame.disabledColor",   "Menu.Frame.DisabledColor",   "black");
  mn.activeForeground = color(res, "menu.active.foregroundColor", "Menu.Active.ForegroundColor", "white");
  mn.activeText       = color(res, "menu.active.textColor",       "Menu.Active.TextColor",       "white");
  mn.titleMargin = std::max(res.read("menu.title.marginWidth", "Menu.Title.MarginWidth", 1), 0);
  mn.frameMargin = std::max(res.read("menu.frame.marginWidth", "Menu.Frame.MarginWidth", 1), 0);

  // --- slit (defaults to the toolbar texture, ScreenResource.cc:499-505) ---
  s->slit_ = bt::textureResource(res, "slit", "Slit", t.bar);

  // --- top-level keys ---
  s->root_command_ = res.read("rootCommand", "RootCommand", "");
  s->border_width_ = res.read("borderWidth", "BorderWidth", 1);
  s->bevel_width_  = std::max(res.read("bevelWidth", "BevelWidth", 3), 1);

  // --- desktop background: BlackboxAI.desktop keys win; bsetroot
  //     interpretation slots in here in Task 9; else flat black. ---
  {
    bt::Texture none;   // empty description sentinel via the defaultTexture overload
    bt::Texture d = bt::textureResource(res, "BlackboxAI.desktop", "BlackboxAI.Desktop", none);
    if (!d.description().empty() && d.texture() != bt::Texture::Parent_Relative) {
      s->desktop_.kind = DesktopBackground::Kind::TextureBg;
      s->desktop_.texture = d;
    } else {
      s->desktop_.texture = flatBlack();
    }
  }

  // --- PR sanity rules (containers force flat black) ---
  sanitizeContainer(s->window_focus_.title);
  sanitizeContainer(s->window_unfocus_.title);
  sanitizeContainer(s->window_focus_.handle);
  sanitizeContainer(s->window_unfocus_.handle);
  sanitizeContainer(s->window_focus_.grip);
  sanitizeContainer(s->window_unfocus_.grip);
  sanitizeContainer(s->toolbar_.bar);
  sanitizeContainer(s->slit_);
  sanitizeContainer(s->menu_.title);
  sanitizeContainer(s->menu_.frame);
  sanitizeContainer(s->menu_.active);

  return s;
}

std::shared_ptr<const Style> Style::builtin() {
  bt::Resource res;
  res.loadFromString(kBuiltinStyle);
  auto loaded = fromResource(res);
  // fromResource returns shared_ptr<const>; we own the only reference, so the
  // const_cast to pin metrics/fonts is contained here.
  Style *s = const_cast<Style *>(loaded.get());
  // Pin the M3/M4 metrics + font: the builtin IS the pre-style theme and the
  // existing golden suite is keyed to these exact numbers. Loaded style FILES
  // get the computed formulas above.
  s->frame_ = frame::FrameMetrics{};
  s->toolbar_metrics_ = toolbar::ToolbarMetrics{};
  s->window_font_     = std::make_unique<bt::TextRenderer>("monospace", 16);
  s->toolbar_font_    = std::make_unique<bt::TextRenderer>("monospace", 16);
  s->menu_title_font_ = std::make_unique<bt::TextRenderer>("monospace", 16);
  s->menu_frame_font_ = std::make_unique<bt::TextRenderer>("monospace", 16);
  return loaded;
}

} // namespace bbai
