// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "update-checker/semver.hpp"

#include <iostream>
#include <optional>
#include <string_view>

namespace {

bool expectComparison(std::string_view candidate, std::string_view current, std::optional<int> expected)
{
	const std::optional<int> actual = bgobs::compareSemanticVersions(candidate, current);
	if (actual == expected)
		return true;

	std::cerr << "comparison failed for " << candidate << " and " << current << "\n";
	return false;
}

} // namespace

int main()
{
	bool success = true;
	success &= expectComparison("0.3.20", "0.3.19", 1);
	success &= expectComparison("0.3.18", "0.3.19", -1);
	success &= expectComparison("0.3.19", "0.3.19", 0);
	success &= expectComparison("1.0.0", "1.0.0-rc.1", 1);
	success &= expectComparison("1.0.0-rc.2", "1.0.0-rc.1", 1);
	success &= expectComparison("1.0.0-alpha.1", "1.0.0-alpha.beta", -1);
	success &= expectComparison("1.0.0+build.2", "1.0.0+build.1", 0);
	success &= expectComparison("999999999999999999999.0.0", "99999999999999999999.0.0", 1);
	success &= expectComparison("1.0", "1.0.0", std::nullopt);
	success &= expectComparison("01.0.0", "1.0.0", std::nullopt);
	success &= expectComparison("1.0.0-rc.01", "1.0.0", std::nullopt);
	success &= expectComparison("1.0.0-", "1.0.0", std::nullopt);
	success &= expectComparison("1.0.0+bad+build", "1.0.0", std::nullopt);

	return success ? 0 : 1;
}
