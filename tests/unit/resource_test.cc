#include <doctest/doctest.h>
#include "Resource.hh"

TEST_CASE("parse key/value lines with comments and blanks") {
    bt::Resource r;
    r.loadFromString(
        "! a comment\n"
        "BlackboxAI.desktop:  raised gradient diagonal\n"
        "BlackboxAI.desktop.color:  #204060\n"
        "\n"
        "BlackboxAI.desktop.colorTo: #6080a0\n");
    CHECK(r.valid());
    CHECK(r.read("BlackboxAI.desktop", "BlackboxAI.Desktop", "") == "raised gradient diagonal");
    CHECK(r.read("BlackboxAI.desktop.color", "", "") == "#204060");
    CHECK(r.read("BlackboxAI.desktop.colorTo", "", "") == "#6080a0");
    CHECK(r.read("missing.key", "", "fallback") == "fallback");
}

TEST_CASE("classname fallback resolves when the instance key is absent") {
    bt::Resource r;
    r.loadFromString("Foo.Class: viaclass\n");
    CHECK(r.read("foo.instance", "Foo.Class", "") == "viaclass");
}

TEST_CASE("integer and bool reads") {
    bt::Resource r;
    r.loadFromString("a.num: 42\na.flag: True\na.no: false\na.bad: notanint\n");
    CHECK(r.read("a.num", "", 0) == 42);
    CHECK(r.read("a.flag", "", false) == true);
    CHECK(r.read("a.no", "", true) == false);
    CHECK(r.read("a.bad", "", 7) == 7);     // non-numeric -> default
    CHECK(r.read("a.missing", "", 9) == 9); // absent -> default
}

TEST_CASE("a default-constructed Resource is not valid") {
    bt::Resource r;
    CHECK_FALSE(r.valid());
}

TEST_CASE("Xrm loose-binding wildcards (the Gray-style subset)") {
    bt::Resource r;
    r.loadFromString(
        "*font: Sans Serif-9\n"
        "*marginWidth: 2\n"
        "*borderWidth: 1\n"
        "toolbar*textColor: black\n"
        "window*alignment: center\n"
        "*button.pressed.appearance: sunken solid\n"
        "window.label.marginWidth: 1\n");

    // leading '*' spans any number of components, including several
    CHECK(r.read("toolbar.font", "Toolbar.Font", "") == "Sans Serif-9");
    CHECK(r.read("menu.frame.font", "Menu.Frame.Font", "") == "Sans Serif-9");
    // infix '*' spans zero components...
    CHECK(r.read("window.alignment", "Window.Alignment", "") == "center");
    // ...or several
    CHECK(r.read("toolbar.clock.textColor", "Toolbar.Label.TextColor", "") == "black");
    CHECK(r.read("toolbar.windowLabel.textColor", "Toolbar.Label.TextColor", "") == "black");
    // exact match always beats a wildcard
    CHECK(r.read("window.label.marginWidth", "Window.Label.MarginWidth", 0) == 1);
    CHECK(r.read("window.title.marginWidth", "Window.Title.MarginWidth", 0) == 2);
    // '*' spans WHOLE components: *button.appearance must not match
    // window.button.focus.appearance (focus intervenes inside the literal run)
    bt::Resource r2;
    r2.loadFromString("*button.appearance: parentrelative\n");
    CHECK(r2.read("window.button.focus.appearance",
                  "Window.Button.Focus.Appearance", "MISS") == "MISS");
    CHECK(r2.read("toolbar.button.appearance",
                  "Toolbar.Button.Appearance", "") == "parentrelative");
}

TEST_CASE("wildcard precedence: more literal characters win") {
    bt::Resource r;
    r.loadFromString(
        "*textColor: red\n"
        "toolbar*textColor: black\n");
    CHECK(r.read("toolbar.label.textColor", "Toolbar.Label.TextColor", "") == "black");
    CHECK(r.read("menu.frame.textColor", "Menu.Frame.TextColor", "") == "red");
}
