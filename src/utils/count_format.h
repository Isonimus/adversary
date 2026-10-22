#pragma once

/**
 * @file count_format.h
 * @brief Pure index/total counter formatting helper.
 */

#include <cstddef>

namespace adversary {
namespace utils {

// Two signed 32-bit decimal integers, a slash, and the terminating NUL.
constexpr size_t COUNT_FORMAT_BUFFER_SIZE = 24;

/**
 * @brief Format an index and total as "index/total" into @p out.
 *
 * Output is always bounded by @p outLen. A buffer of COUNT_FORMAT_BUFFER_SIZE
 * holds every pair of 32-bit int values without truncation.
 *
 * @param index  Current item index
 * @param total  Total item count
 * @param out    Output buffer
 * @param outLen Size of @p out
 */
void formatCount(int index, int total, char* out, size_t outLen);

} // namespace utils
} // namespace adversary
