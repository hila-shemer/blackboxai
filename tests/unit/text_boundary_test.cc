// T1 boundary smoke: proves toolkit/text.hpp (the sanitized fcft + pixman
// boundary) compiles under C++20 and that libfcft links and initializes. If the
// generalized [static]/restrict sanitize is missing, this TU fails to *compile*;
// if fcft fails to link, it fails to *run*.
#include <doctest/doctest.h>
#include "text.hpp"

TEST_CASE("fcft boundary compiles and initializes") {
    // No fonts touched here (that needs fontconfig isolation — see text_test.cc);
    // fcft_init only configures logging and must return true.
    CHECK(fcft_init(FCFT_LOG_COLORIZE_NEVER, /*do_syslog=*/false,
                    FCFT_LOG_CLASS_ERROR));
    fcft_fini();
}
