// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <functional>
#include <limits>

#include <curl/curl.h>

#include <obs.h>
#include "plugin-support.h"

const std::string userAgent = std::string(PLUGIN_NAME) + "/" + PLUGIN_VERSION;
static constexpr std::size_t MAX_RESPONSE_BYTES = 1024 * 1024;

static std::size_t writeFunc(void *ptr, std::size_t size, size_t nmemb, std::string *data)
{
	if (size != 0 && nmemb > std::numeric_limits<std::size_t>::max() / size)
		return 0;
	const std::size_t bytes = size * nmemb;
	if (bytes > MAX_RESPONSE_BYTES || data->size() > MAX_RESPONSE_BYTES - bytes)
		return 0;
	data->append(static_cast<char *>(ptr), bytes);
	return bytes;
}

void fetchStringFromUrl(const char *urlString, std::function<void(std::string, int)> callback)
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		obs_log(LOG_INFO, "Failed to initialize curl");
		callback("", CURL_LAST);
		return;
	}

	std::string responseBody;
	curl_easy_setopt(curl, CURLOPT_URL, urlString);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunc);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 5L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#else
	curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

	CURLcode code = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (code == CURLE_OK) {
		callback(responseBody, 0);
	} else {
		obs_log(LOG_INFO, "Failed to get latest release info");
		callback("", code);
	}
}
