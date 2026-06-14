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
