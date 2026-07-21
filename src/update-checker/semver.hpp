// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string_view>

namespace bgobs {

/**
 * Compare two Semantic Versioning 2.0.0 version strings.
 *
 * @return -1 when candidate is older, 0 when both versions have the same
 * precedence, 1 when candidate is newer, or std::nullopt when either string is
 * not a valid semantic version.
 */
std::optional<int> compareSemanticVersions(std::string_view candidate, std::string_view current);

} // namespace bgobs
