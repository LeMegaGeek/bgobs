// SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "semver.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace bgobs {
namespace {

struct SemVerIdentifier {
	std::string_view value;
	bool numeric;
};

struct SemVer {
	std::array<std::string_view, 3> core;
	std::vector<SemVerIdentifier> prerelease;
};

bool isAsciiDigit(char value)
{
	return value >= '0' && value <= '9';
}

bool isIdentifierCharacter(char value)
{
	return isAsciiDigit(value) || (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '-';
}

bool parseIdentifiers(std::string_view value, bool enforceNumericLeadingZeroRule,
		      std::vector<SemVerIdentifier> &identifiers)
{
	if (value.empty())
		return false;

	size_t begin = 0;
	while (begin <= value.size()) {
		const size_t end = value.find('.', begin);
		const std::string_view identifier =
			value.substr(begin, end == std::string_view::npos ? end : end - begin);
		if (identifier.empty())
			return false;

		bool numeric = true;
		for (const char character : identifier) {
			if (!isIdentifierCharacter(character))
				return false;
			numeric = numeric && isAsciiDigit(character);
		}
		if (enforceNumericLeadingZeroRule && numeric && identifier.size() > 1 && identifier.front() == '0')
			return false;

		identifiers.push_back({identifier, numeric});
		if (end == std::string_view::npos)
			break;
		begin = end + 1;
	}

	return true;
}

bool parseCore(std::string_view value, std::array<std::string_view, 3> &components)
{
	size_t begin = 0;
	for (size_t index = 0; index < components.size(); ++index) {
		const size_t end = value.find('.', begin);
		if ((index + 1 < components.size() && end == std::string_view::npos) ||
		    (index + 1 == components.size() && end != std::string_view::npos)) {
			return false;
		}

		const std::string_view component =
			value.substr(begin, end == std::string_view::npos ? end : end - begin);
		if (component.empty() || (component.size() > 1 && component.front() == '0'))
			return false;
		for (const char character : component) {
			if (!isAsciiDigit(character))
				return false;
		}

		components[index] = component;
		begin = end + 1;
	}

	return true;
}

std::optional<SemVer> parseSemanticVersion(std::string_view value)
{
	SemVer result;

	const size_t buildSeparator = value.find('+');
	if (buildSeparator != std::string_view::npos) {
		std::vector<SemVerIdentifier> buildIdentifiers;
		if (!parseIdentifiers(value.substr(buildSeparator + 1), false, buildIdentifiers))
			return std::nullopt;
		value = value.substr(0, buildSeparator);
	}

	const size_t prereleaseSeparator = value.find('-');
	if (prereleaseSeparator != std::string_view::npos) {
		if (!parseIdentifiers(value.substr(prereleaseSeparator + 1), true, result.prerelease))
			return std::nullopt;
		value = value.substr(0, prereleaseSeparator);
	}

	if (!parseCore(value, result.core))
		return std::nullopt;

	return result;
}

int compareNumericIdentifiers(std::string_view left, std::string_view right)
{
	if (left.size() != right.size())
		return left.size() < right.size() ? -1 : 1;
	if (left == right)
		return 0;
	return left < right ? -1 : 1;
}

int compareParsedVersions(const SemVer &left, const SemVer &right)
{
	for (size_t index = 0; index < left.core.size(); ++index) {
		const int comparison = compareNumericIdentifiers(left.core[index], right.core[index]);
		if (comparison != 0)
			return comparison;
	}

	if (left.prerelease.empty() || right.prerelease.empty()) {
		if (left.prerelease.empty() == right.prerelease.empty())
			return 0;
		return left.prerelease.empty() ? 1 : -1;
	}

	const size_t sharedIdentifierCount = std::min(left.prerelease.size(), right.prerelease.size());
	for (size_t index = 0; index < sharedIdentifierCount; ++index) {
		const SemVerIdentifier &leftIdentifier = left.prerelease[index];
		const SemVerIdentifier &rightIdentifier = right.prerelease[index];
		if (leftIdentifier.numeric != rightIdentifier.numeric)
			return leftIdentifier.numeric ? -1 : 1;

		const int comparison = leftIdentifier.numeric
					       ? compareNumericIdentifiers(leftIdentifier.value, rightIdentifier.value)
					       : leftIdentifier.value.compare(rightIdentifier.value);
		if (comparison != 0)
			return comparison < 0 ? -1 : 1;
	}

	if (left.prerelease.size() == right.prerelease.size())
		return 0;
	return left.prerelease.size() < right.prerelease.size() ? -1 : 1;
}

} // namespace

std::optional<int> compareSemanticVersions(std::string_view candidate, std::string_view current)
{
	const std::optional<SemVer> parsedCandidate = parseSemanticVersion(candidate);
	const std::optional<SemVer> parsedCurrent = parseSemanticVersion(current);
	if (!parsedCandidate || !parsedCurrent)
		return std::nullopt;

	return compareParsedVersions(*parsedCandidate, *parsedCurrent);
}

} // namespace bgobs
