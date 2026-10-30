/**
 * @file count_format.cpp
 * @brief Index/total counter formatting implementation.
 */

#include "count_format.h"

#include <cstdio>

namespace adversary {
namespace utils {

void formatCount(int index, int total, char* out, size_t outLen) {
    if (out == nullptr || outLen == 0) {
        return;
    }

    snprintf(out, outLen, "%d/%d", index, total);
}

} // namespace utils
} // namespace adversary
