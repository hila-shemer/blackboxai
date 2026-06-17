#ifndef BLACKBOXAI_DECORATION_PALETTE_HH
#define BLACKBOXAI_DECORATION_PALETTE_HH
#include "Color.hh"
#include <string>
namespace bbai::deco {
  struct Look { const char *desc; const char *c1; const char *c2; };
  enum class Element { Title, Label, Handle, Grip, Button };
  inline Look lookFor(Element e, bool f) {
    switch (e) {
      case Element::Title:  return f ? Look{"raised gradient diagonal","#c0c0c0","#808080"}
                                     : Look{"raised gradient diagonal","#909090","#606060"};
      case Element::Label:  return f ? Look{"sunken gradient diagonal","#b8b8b8","#888888"}
                                     : Look{"sunken gradient diagonal","#909090","#686868"};
      case Element::Handle: return f ? Look{"raised gradient diagonal","#c0c0c0","#808080"}
                                     : Look{"raised gradient diagonal","#909090","#606060"};
      case Element::Grip:   return f ? Look{"raised gradient diagonal","#d8d8d8","#909090"}
                                     : Look{"raised gradient diagonal","#a0a0a0","#707070"};
      case Element::Button: return f ? Look{"raised gradient diagonal","#e0e0e0","#a8a8a8"}
                                     : Look{"raised gradient diagonal","#a8a8a8","#808080"};
    }
    return {"","",""};
  }
  inline bt::Color textColorFor(bool f)   { return f ? bt::Color(0,0,0)    : bt::Color(96,96,96); }
  inline bt::Color borderColorFor(bool f) { return f ? bt::Color(48,48,48) : bt::Color(72,72,72); }
  inline bt::Color picColorFor(bool f)    { return f ? bt::Color(32,32,32) : bt::Color(96,96,96); }
}
#endif
