// The loaded Blackbox style: every texture, color, font and computed metric
// the renderers consume, built from a style file with blackboxwm's exact key
// policy (ScreenResource::loadStyle + MenuStyle::load, ported verbatim).
// Noncopyable (owns TextRenderers); passed around as shared_ptr<const Style>
// so a live re-theme is "swap the pointer, re-render".
#ifndef BLACKBOXAI_STYLE_HH
#define BLACKBOXAI_STYLE_HH

#include "Resource.hh"
#include "Texture.hh"
#include "Color.hh"
#include "Text.hh"
#include "Frame.hh"
#include "Toolbar.geom.hh"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bbai {

  // One focus-state's window element set (classic WindowStyle halves).
  struct WindowLook {
    bt::Texture title, label, button, handle, grip;
    bt::Color text;         // label textColor
    bt::Color foreground;   // button glyph (foregroundColor -> picColor chain)
    bt::Color frameBorder;  // window.frame.<state>.borderColor
  };

  struct ToolbarLook {
    bt::Texture bar, slabel, wlabel, clock, button, pressed;
    bt::Color slabelText, wlabelText, clockText;
    bt::Color foreground;   // arrow glyphs
  };

  struct MenuLook {
    bt::Texture title, frame, active;
    bt::Color titleText, titleForeground;
    bt::Color frameText, frameForeground, frameDisabled;
    bt::Color activeText, activeForeground;
    // marginWidth/alignment/bullet keys are parsed into Style but menu
    // GEOMETRY stays Menu.geom.hh's pinned constants this wave (deferral:
    // wave-2 menus owns menu layout).
    int titleMargin = 1, frameMargin = 1;
  };

  // The desktop background: a texture (BlackboxAI.desktop keys or bsetroot
  // -solid/-gradient) or the bsetroot modula pattern (Task 9).
  struct DesktopBackground {
    enum class Kind { TextureBg, Modula } kind = Kind::TextureBg;
    bt::Texture texture;                // Kind::TextureBg
    int modX = 1, modY = 1;             // Kind::Modula
    bt::Color modFg, modBg;
  };

  // Native interpretation of a style file's bsetroot rootCommand (locked
  // policy: style rootCommand NEVER reaches a shell; we render what bsetroot
  // would have painted). Grammar per util/bsetroot.cc: -solid <c>,
  // -mod <x> <y> with -fg/-foreground and -bg/-background, -gradient <texture>
  // with -from/-to. bsetbg is accepted as an alias (Cthulhain uses it with the
  // same -solid grammar). Anything else -> Kind::None.
  namespace bsetroot {
    struct Spec {
      enum class Kind { None, Solid, Mod, Gradient } kind = Kind::None;
      std::string fore, back, texture;
      int modX = 1, modY = 1;
    };
    Spec parse(const std::string &command);
    // The classic 16x16 modula tile expanded to w x h ARGB8888 (alpha 0xFF).
    std::vector<uint32_t> renderModula(int w, int h, int x, int y,
                                       bt::Color fg, bt::Color bg);
  }

  class Style {
  public:
    // nullptr if the file is unreadable. The requested->default->builtin
    // ladder lives in Server::loadStyleWithFallback, not here.
    // rc_root_command is the RC FILE's rootCommand: classic resolves one root
    // command with rc priority (ScreenResource::loadStyle), so when it is
    // nonempty the style's own rootCommand never paints - the rc line does if
    // it is a bsetroot call, else the desktop falls back to the style's
    // explicit BlackboxAI.desktop keys / flat black.
    static std::shared_ptr<const Style> load(const std::string &path,
                                             const std::string &rc_root_command = {});
    static std::shared_ptr<const Style> fromResource(const bt::Resource &res,
                                                     std::string source_path = {},
                                                     const std::string &rc_root_command = {});
    // Today's pre-style look: the M3 grey palette as a style string with the
    // M3/M4 metrics and font PINNED (not formula-computed) - the existing
    // golden suite is keyed to those numbers, and the builtin IS that theme.
    static std::shared_ptr<const Style> builtin(const std::string &rc_root_command = {});

    Style(const Style &) = delete;
    Style &operator=(const Style &) = delete;

    const WindowLook &windowLook(bool focused) const
    { return focused ? window_focus_ : window_unfocus_; }
    const bt::Texture &windowPressed() const { return window_pressed_; }
    const ToolbarLook &toolbarLook() const { return toolbar_; }
    const MenuLook &menuLook() const { return menu_; }
    const bt::Texture &slitTexture() const { return slit_; }

    const frame::FrameMetrics &frameMetrics() const { return frame_; }
    const toolbar::ToolbarMetrics &toolbarMetrics() const { return toolbar_metrics_; }

    bt::TextRenderer *windowFont() const { return window_font_.get(); }
    bt::TextRenderer *toolbarFont() const { return toolbar_font_.get(); }
    bt::TextRenderer *menuTitleFont() const { return menu_title_font_.get(); }
    bt::TextRenderer *menuFrameFont() const { return menu_frame_font_.get(); }

    const DesktopBackground &desktop() const { return desktop_; }
    // The style file's own rootCommand - carried for inspection, interpreted
    // into desktop() when it is a bsetroot call, and NEVER exec'd (a theme
    // file doesn't get shell; the rc-file rootCommand, which the user wrote,
    // is the one Server runs via /bin/sh).
    const std::string &rootCommand() const { return root_command_; }
    int borderWidth() const { return border_width_; }
    int bevelWidth() const { return bevel_width_; }
    const std::string &sourcePath() const { return source_path_; }  // "" = builtin

  private:
    Style() = default;

    WindowLook window_focus_, window_unfocus_;
    bt::Texture window_pressed_;
    ToolbarLook toolbar_;
    MenuLook menu_;
    bt::Texture slit_;
    frame::FrameMetrics frame_;
    toolbar::ToolbarMetrics toolbar_metrics_;
    std::unique_ptr<bt::TextRenderer> window_font_, toolbar_font_,
                                      menu_title_font_, menu_frame_font_;
    DesktopBackground desktop_;
    std::string root_command_;
    int border_width_ = 1, bevel_width_ = 3;
    std::string source_path_;
  };

} // namespace bbai

#endif // BLACKBOXAI_STYLE_HH
