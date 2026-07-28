#include <cstdio>
#include "test_util.h"

int main() {
    std::printf("Running %zu test case(s)...\n", testfx::registry().size());
    for (auto& c : testfx::registry()) {
        std::printf("- %s\n", c.name.c_str());
        c.fn();
    }
    if (testfx::failures() == 0) {
        std::printf("ALL PASSED\n");
        return 0;
    }
    std::printf("FAILED: %d check(s)\n", testfx::failures());
    return 1;
}
