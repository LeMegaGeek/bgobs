/*
 * SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "cacam-usb-source.hpp"

#include "plugin-support.h"

#include <obs-module.h>
#include <util/platform.h>

#include <opencv2/imgproc.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <sys/types.h>
#endif

namespace {

#ifdef _WIN32
using libusb_ssize_t = intptr_t;
#else
using libusb_ssize_t = ssize_t;
#endif

struct libusb_context;
struct libusb_device;
struct libusb_device_handle;

struct libusb_device_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
};

struct libusb_endpoint_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bEndpointAddress;
	uint8_t bmAttributes;
	uint16_t wMaxPacketSize;
	uint8_t bInterval;
	uint8_t bRefresh;
	uint8_t bSynchAddress;
	const unsigned char *extra;
	int extra_length;
};

struct libusb_interface_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bInterfaceNumber;
	uint8_t bAlternateSetting;
	uint8_t bNumEndpoints;
	uint8_t bInterfaceClass;
	uint8_t bInterfaceSubClass;
	uint8_t bInterfaceProtocol;
	uint8_t iInterface;
	const libusb_endpoint_descriptor *endpoint;
	const unsigned char *extra;
	int extra_length;
};

struct libusb_interface {
	const libusb_interface_descriptor *altsetting;
	int num_altsetting;
};

struct libusb_config_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t MaxPower;
	const libusb_interface *interface;
	const unsigned char *extra;
	int extra_length;
};

constexpr uint16_t GOOGLE_VENDOR_ID = 0x18d1;
constexpr uint16_t AOA_PID_ACCESSORY = 0x2d00;
constexpr uint16_t AOA_PID_ACCESSORY_ADB = 0x2d01;
constexpr uint16_t AOA_PID_AUDIO = 0x2d02;
constexpr uint16_t AOA_PID_AUDIO_ADB = 0x2d03;
constexpr uint16_t AOA_PID_ACCESSORY_AUDIO = 0x2d04;
constexpr uint16_t AOA_PID_ACCESSORY_AUDIO_ADB = 0x2d05;

constexpr uint8_t LIBUSB_ENDPOINT_IN = 0x80;
constexpr uint8_t LIBUSB_ENDPOINT_OUT = 0x00;
constexpr uint8_t LIBUSB_REQUEST_TYPE_VENDOR = 0x40;
constexpr uint8_t LIBUSB_RECIPIENT_DEVICE = 0x00;
constexpr uint8_t LIBUSB_TRANSFER_TYPE_MASK = 0x03;
constexpr uint8_t LIBUSB_TRANSFER_TYPE_BULK = 0x02;
constexpr int LIBUSB_ERROR_TIMEOUT = -7;

constexpr uint8_t AOA_GET_PROTOCOL = 51;
constexpr uint8_t AOA_SEND_IDENT = 52;
constexpr uint8_t AOA_START = 53;

constexpr uint32_t CACAM_MAGIC = 0x4343414d;
constexpr uint8_t CACAM_PROTOCOL_VERSION = 1;
constexpr uint8_t CACAM_TYPE_HELLO = 1;
constexpr uint8_t CACAM_TYPE_NV21 = 3;
constexpr size_t CACAM_HEADER_SIZE = 20;
constexpr size_t CACAM_FRAME_METADATA_SIZE = 16;
constexpr uint32_t MAX_PAYLOAD_SIZE = 16 * 1024 * 1024;
constexpr size_t BULK_READ_BUFFER_SIZE = 64 * 1024;

constexpr int BULK_TIMEOUT_MS = 1000;
constexpr int CONTROL_TIMEOUT_MS = 1000;
constexpr int RETRY_DELAY_MS = 1200;

struct UsbApi {
#ifdef _WIN32
	HMODULE library = nullptr;
#else
	void *library = nullptr;
#endif
	int (*init)(libusb_context **) = nullptr;
	void (*exit)(libusb_context *) = nullptr;
	libusb_ssize_t (*get_device_list)(libusb_context *, libusb_device ***) = nullptr;
	void (*free_device_list)(libusb_device **, int) = nullptr;
	int (*get_device_descriptor)(libusb_device *, libusb_device_descriptor *) = nullptr;
	int (*open)(libusb_device *, libusb_device_handle **) = nullptr;
	void (*close)(libusb_device_handle *) = nullptr;
	int (*control_transfer)(libusb_device_handle *, uint8_t, uint8_t, uint16_t, uint16_t, unsigned char *, uint16_t,
				unsigned int) = nullptr;
	int (*get_active_config_descriptor)(libusb_device *, libusb_config_descriptor **) = nullptr;
	void (*free_config_descriptor)(libusb_config_descriptor *) = nullptr;
	int (*claim_interface)(libusb_device_handle *, int) = nullptr;
	int (*release_interface)(libusb_device_handle *, int) = nullptr;
	int (*bulk_transfer)(libusb_device_handle *, unsigned char, unsigned char *, int, int *, unsigned int) = nullptr;
	int (*kernel_driver_active)(libusb_device_handle *, int) = nullptr;
	int (*detach_kernel_driver)(libusb_device_handle *, int) = nullptr;

	~UsbApi() { unload(); }

	bool load()
	{
		if (library)
			return true;

#ifdef _WIN32
		const std::string directory = module_directory();
		if (!directory.empty()) {
			const std::string path = directory + "\\libusb-1.0.dll";
			library = LoadLibraryA(path.c_str());
		}
		if (!library)
			library = LoadLibraryA("libusb-1.0.dll");
#else
		library = dlopen("libusb-1.0.so.0", RTLD_NOW | RTLD_LOCAL);
		if (!library)
			library = dlopen("libusb-1.0.dylib", RTLD_NOW | RTLD_LOCAL);
#endif
		if (!library)
			return false;

		const bool symbols_loaded = loadSymbol(init, "libusb_init") && loadSymbol(exit, "libusb_exit") &&
					    loadSymbol(get_device_list, "libusb_get_device_list") &&
					    loadSymbol(free_device_list, "libusb_free_device_list") &&
					    loadSymbol(get_device_descriptor, "libusb_get_device_descriptor") &&
					    loadSymbol(open, "libusb_open") && loadSymbol(close, "libusb_close") &&
					    loadSymbol(control_transfer, "libusb_control_transfer") &&
					    loadSymbol(get_active_config_descriptor, "libusb_get_active_config_descriptor") &&
					    loadSymbol(free_config_descriptor, "libusb_free_config_descriptor") &&
					    loadSymbol(claim_interface, "libusb_claim_interface") &&
					    loadSymbol(release_interface, "libusb_release_interface") &&
					    loadSymbol(bulk_transfer, "libusb_bulk_transfer") &&
					    loadSymbol(kernel_driver_active, "libusb_kernel_driver_active") &&
					    loadSymbol(detach_kernel_driver, "libusb_detach_kernel_driver");
		if (!symbols_loaded)
			unload();
		return symbols_loaded;
	}

	void unload()
	{
		if (!library)
			return;
#ifdef _WIN32
		FreeLibrary(library);
#else
		dlclose(library);
#endif
		library = nullptr;
	}

	template<typename T> bool loadSymbol(T &target, const char *name)
	{
#ifdef _WIN32
		target = reinterpret_cast<T>(GetProcAddress(library, name));
#else
		target = reinterpret_cast<T>(dlsym(library, name));
#endif
		return target != nullptr;
	}

#ifdef _WIN32
	static std::string module_directory()
	{
		HMODULE module = nullptr;
		if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
						GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCSTR>(&module_directory), &module)) {
			return {};
		}

		char path[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameA(module, path, static_cast<DWORD>(sizeof(path)));
		if (length == 0 || length >= sizeof(path))
			return {};

		std::string value(path, length);
		const size_t separator = value.find_last_of("\\/");
		return separator == std::string::npos ? std::string{} : value.substr(0, separator);
	}
#endif
};

struct AccessoryConnection {
	libusb_device_handle *handle = nullptr;
	int interface_number = -1;
	unsigned char endpoint_in = 0;

	void close(UsbApi &api)
	{
		if (handle && interface_number >= 0)
			api.release_interface(handle, interface_number);
		if (handle)
			api.close(handle);
		handle = nullptr;
		interface_number = -1;
		endpoint_in = 0;
	}
};

bool is_aoa_product(uint16_t vendor, uint16_t product)
{
	if (vendor != GOOGLE_VENDOR_ID)
		return false;

	switch (product) {
	case AOA_PID_ACCESSORY:
	case AOA_PID_ACCESSORY_ADB:
	case AOA_PID_AUDIO:
	case AOA_PID_AUDIO_ADB:
	case AOA_PID_ACCESSORY_AUDIO:
	case AOA_PID_ACCESSORY_AUDIO_ADB:
		return true;
	default:
		return false;
	}
}

uint16_t read_u16_le(const unsigned char *data)
{
	return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1] << 8);
}

uint32_t read_u32_be(const unsigned char *data)
{
	return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
	       (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

uint64_t read_u64_be(const unsigned char *data)
{
	uint64_t value = 0;
	for (size_t index = 0; index < 8; ++index)
		value = (value << 8) | data[index];
	return value;
}

bool send_aoa_string(UsbApi &api, libusb_device_handle *handle, uint16_t index, const char *value)
{
	std::vector<unsigned char> bytes(std::strlen(value) + 1);
	std::memcpy(bytes.data(), value, bytes.size());
	const int transferred = api.control_transfer(handle, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR |
							LIBUSB_RECIPIENT_DEVICE,
						AOA_SEND_IDENT, 0, index, bytes.data(),
						static_cast<uint16_t>(bytes.size()), CONTROL_TIMEOUT_MS);
	return transferred >= 0;
}

bool request_accessory_mode(UsbApi &api, libusb_device *device)
{
	libusb_device_descriptor descriptor = {};
	if (api.get_device_descriptor(device, &descriptor) != 0)
		return false;
	if (is_aoa_product(descriptor.idVendor, descriptor.idProduct))
		return false;

	libusb_device_handle *handle = nullptr;
	if (api.open(device, &handle) != 0 || !handle)
		return false;

	unsigned char version_bytes[2] = {};
	const int protocol_result = api.control_transfer(handle, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR |
							LIBUSB_RECIPIENT_DEVICE,
						AOA_GET_PROTOCOL, 0, 0, version_bytes, sizeof(version_bytes),
						CONTROL_TIMEOUT_MS);
	if (protocol_result < 2 || read_u16_le(version_bytes) == 0) {
		api.close(handle);
		return false;
	}

	const bool configured =
		send_aoa_string(api, handle, 0, "LeMegaGeek") && send_aoa_string(api, handle, 1, "CaCam USB") &&
		send_aoa_string(api, handle, 2, "CaCam direct USB video source") &&
		send_aoa_string(api, handle, 3, "1.0") &&
		send_aoa_string(api, handle, 4, "https://github.com/LeMegaGeek/CaCam") &&
		send_aoa_string(api, handle, 5, "CaCam");

	if (configured) {
		api.control_transfer(handle, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
				     AOA_START, 0, 0, nullptr, 0, CONTROL_TIMEOUT_MS);
	}

	api.close(handle);
	return configured;
}

bool select_accessory_endpoint(UsbApi &api, libusb_device *device, libusb_device_handle *handle,
			       AccessoryConnection &connection)
{
	libusb_config_descriptor *config = nullptr;
	if (api.get_active_config_descriptor(device, &config) != 0 || !config)
		return false;

	bool found = false;
	for (uint8_t interface_index = 0; interface_index < config->bNumInterfaces && !found; ++interface_index) {
		const libusb_interface &interface = config->interface[interface_index];
		for (int alt_index = 0; alt_index < interface.num_altsetting && !found; ++alt_index) {
			const libusb_interface_descriptor &descriptor = interface.altsetting[alt_index];
			unsigned char endpoint_in = 0;
			for (uint8_t endpoint_index = 0; endpoint_index < descriptor.bNumEndpoints; ++endpoint_index) {
				const libusb_endpoint_descriptor &endpoint = descriptor.endpoint[endpoint_index];
				const bool is_bulk = (endpoint.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
						     LIBUSB_TRANSFER_TYPE_BULK;
				const bool is_in = (endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN) != 0;
				if (is_bulk && is_in)
					endpoint_in = endpoint.bEndpointAddress;
			}
			if (endpoint_in == 0)
				continue;

			const int interface_number = descriptor.bInterfaceNumber;
			if (api.kernel_driver_active(handle, interface_number) == 1)
				api.detach_kernel_driver(handle, interface_number);
			if (api.claim_interface(handle, interface_number) == 0) {
				connection.handle = handle;
				connection.interface_number = interface_number;
				connection.endpoint_in = endpoint_in;
				found = true;
			}
		}
	}

	api.free_config_descriptor(config);
	return found;
}

bool open_accessory(UsbApi &api, libusb_context *context, AccessoryConnection &connection)
{
	libusb_device **devices = nullptr;
	const libusb_ssize_t count = api.get_device_list(context, &devices);
	if (count < 0)
		return false;

	for (libusb_ssize_t index = 0; index < count; ++index) {
		libusb_device *device = devices[index];
		libusb_device_descriptor descriptor = {};
		if (api.get_device_descriptor(device, &descriptor) != 0 ||
		    !is_aoa_product(descriptor.idVendor, descriptor.idProduct)) {
			continue;
		}

		libusb_device_handle *handle = nullptr;
		if (api.open(device, &handle) != 0 || !handle)
			continue;

		if (select_accessory_endpoint(api, device, handle, connection)) {
			api.free_device_list(devices, 1);
			return true;
		}

		api.close(handle);
	}

	api.free_device_list(devices, 1);
	return false;
}

bool request_first_android_accessory(UsbApi &api, libusb_context *context)
{
	libusb_device **devices = nullptr;
	const libusb_ssize_t count = api.get_device_list(context, &devices);
	if (count < 0)
		return false;

	bool requested = false;
	for (libusb_ssize_t index = 0; index < count && !requested; ++index)
		requested = request_accessory_mode(api, devices[index]);

	api.free_device_list(devices, 1);
	return requested;
}

class BulkReader {
public:
	BulkReader(UsbApi &api_, AccessoryConnection &connection_, const std::atomic<bool> &stop_requested_)
		: api(api_), connection(connection_), stop_requested(stop_requested_)
	{
		buffer.reserve(BULK_READ_BUFFER_SIZE);
	}

	int read_exact(unsigned char *output, size_t size)
	{
		size_t copied = 0;
		while (copied < size && !stop_requested.load()) {
			const size_t available = buffer.size() - buffer_offset;
			if (available == 0) {
				buffer.clear();
				buffer_offset = 0;
				const int fill_result = fill();
				if (fill_result == LIBUSB_ERROR_TIMEOUT)
					continue;
				if (fill_result < 0)
					return fill_result;
				continue;
			}

			const size_t count = std::min(available, size - copied);
			std::memcpy(output + copied, buffer.data() + buffer_offset, count);
			buffer_offset += count;
			copied += count;

			if (buffer_offset == buffer.size()) {
				buffer.clear();
				buffer_offset = 0;
			}
		}
		return stop_requested.load() ? -1 : 0;
	}

private:
	UsbApi &api;
	AccessoryConnection &connection;
	const std::atomic<bool> &stop_requested;
	std::vector<unsigned char> buffer;
	size_t buffer_offset = 0;

	int fill()
	{
		std::vector<unsigned char> chunk(BULK_READ_BUFFER_SIZE);
		int transferred = 0;
		const int result = api.bulk_transfer(connection.handle, connection.endpoint_in, chunk.data(),
						     static_cast<int>(chunk.size()), &transferred, BULK_TIMEOUT_MS);
		if (result < 0)
			return result;
		if (transferred > 0)
			buffer.insert(buffer.end(), chunk.begin(), chunk.begin() + transferred);
		return result;
	}
};

int read_header(BulkReader &reader, unsigned char *header, size_t size)
{
	if (size < 4)
		return -1;

	const unsigned char magic[] = {'C', 'C', 'A', 'M'};
	size_t matched = 0;
	while (matched < sizeof(magic)) {
		unsigned char byte = 0;
		const int result = reader.read_exact(&byte, 1);
		if (result < 0)
			return result;

		if (byte == magic[matched]) {
			header[matched++] = byte;
		} else if (byte == magic[0]) {
			header[0] = byte;
			matched = 1;
		} else {
			matched = 0;
		}
	}

	return reader.read_exact(header + sizeof(magic), size - sizeof(magic));
}

class CacamUsbSource {
public:
	CacamUsbSource(obs_data_t *settings, obs_source_t *source_) : source(source_)
	{
		update(settings);
		if (auto_connect)
			start();
	}

	~CacamUsbSource() { stop(); }

	void update(obs_data_t *settings)
	{
		const bool should_auto_connect = obs_data_get_bool(settings, "auto_connect");
		auto_connect = should_auto_connect;
		if (auto_connect)
			start();
		else
			stop();
	}

	void activate()
	{
		if (auto_connect)
			start();
	}

	void deactivate() { stop(); }

	uint32_t get_width() const { return width.load(); }
	uint32_t get_height() const { return height.load(); }

private:
	obs_source_t *source = nullptr;
	std::thread worker;
	std::mutex worker_mutex;
	std::atomic<bool> stop_requested{false};
	std::atomic<uint32_t> width{1280};
	std::atomic<uint32_t> height{720};
	std::array<std::vector<unsigned char>, 3> video_buffers;
	size_t video_buffer_index = 0;
	bool auto_connect = true;

	void start()
	{
		std::lock_guard<std::mutex> guard(worker_mutex);
		if (worker.joinable())
			return;
		stop_requested.store(false);
		worker = std::thread(&CacamUsbSource::worker_loop, this);
	}

	void stop()
	{
		std::lock_guard<std::mutex> guard(worker_mutex);
		stop_requested.store(true);
		if (worker.joinable())
			worker.join();
	}

	void worker_loop()
	{
		UsbApi api;
		if (!api.load()) {
			obs_log(LOG_ERROR, "[CaCam USB] libusb runtime not found");
			return;
		}

		libusb_context *context = nullptr;
		if (api.init(&context) != 0 || !context) {
			obs_log(LOG_ERROR, "[CaCam USB] libusb initialization failed");
			return;
		}

		while (!stop_requested.load()) {
			AccessoryConnection connection;
			if (!open_accessory(api, context, connection)) {
				if (request_first_android_accessory(api, context))
					obs_log(LOG_INFO, "[CaCam USB] Requested Android Open Accessory mode");
				sleep_retry();
				continue;
			}

			obs_log(LOG_INFO, "[CaCam USB] Connected");
			read_stream(api, connection);
			connection.close(api);
			obs_log(LOG_INFO, "[CaCam USB] Disconnected");
			sleep_retry();
		}

		api.exit(context);
	}

	void sleep_retry()
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(RETRY_DELAY_MS);
		while (!stop_requested.load() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	void read_stream(UsbApi &api, AccessoryConnection &connection)
	{
		std::vector<unsigned char> header(CACAM_HEADER_SIZE);
		BulkReader reader(api, connection, stop_requested);
		while (!stop_requested.load()) {
			const int header_result = read_header(reader, header.data(), header.size());
			if (header_result < 0)
				break;

			const uint32_t magic = read_u32_be(header.data());
			const uint8_t version = header[4];
			const uint8_t type = header[5];
			const uint32_t payload_size = read_u32_be(header.data() + 8);
			const uint64_t timestamp_us = read_u64_be(header.data() + 12);

			if (magic != CACAM_MAGIC || version != CACAM_PROTOCOL_VERSION || payload_size > MAX_PAYLOAD_SIZE) {
				obs_log(LOG_WARNING, "[CaCam USB] Invalid frame header");
				continue;
			}

			std::vector<unsigned char> payload(payload_size);
			if (payload_size > 0 && reader.read_exact(payload.data(), payload.size()) < 0) {
				break;
			}

			if (type == CACAM_TYPE_HELLO) {
				const std::string text(payload.begin(), payload.end());
				obs_log(LOG_INFO, "[CaCam USB] %s", text.c_str());
			} else if (type == CACAM_TYPE_NV21) {
				output_nv21(payload, timestamp_us);
			}
		}
	}

	void output_nv21(const std::vector<unsigned char> &payload, uint64_t message_timestamp_us)
	{
		if (payload.size() < CACAM_FRAME_METADATA_SIZE)
			return;

		const uint32_t frame_width = read_u32_be(payload.data());
		const uint32_t frame_height = read_u32_be(payload.data() + 4);
		const uint64_t frame_timestamp_us = read_u64_be(payload.data() + 8);
		if (frame_width == 0 || frame_height == 0 || frame_width > 4096 || frame_height > 4096)
			return;

		const size_t expected_size = static_cast<size_t>(frame_width) * static_cast<size_t>(frame_height) * 3 / 2;
		if (payload.size() - CACAM_FRAME_METADATA_SIZE < expected_size)
			return;

		auto *frame_data = const_cast<unsigned char *>(payload.data() + CACAM_FRAME_METADATA_SIZE);
		const cv::Mat nv21(static_cast<int>(frame_height + frame_height / 2), static_cast<int>(frame_width), CV_8UC1,
				   frame_data);
		auto &video_buffer = video_buffers[video_buffer_index];
		video_buffer_index = (video_buffer_index + 1) % video_buffers.size();
		video_buffer.resize(static_cast<size_t>(frame_width) * static_cast<size_t>(frame_height) * 4);
		cv::Mat bgra(static_cast<int>(frame_height), static_cast<int>(frame_width), CV_8UC4, video_buffer.data());
		cv::cvtColor(nv21, bgra, cv::COLOR_YUV2BGRA_NV21);

		width.store(static_cast<uint32_t>(bgra.cols));
		height.store(static_cast<uint32_t>(bgra.rows));

		obs_source_frame frame = {};
		frame.format = VIDEO_FORMAT_BGRA;
		frame.width = static_cast<uint32_t>(bgra.cols);
		frame.height = static_cast<uint32_t>(bgra.rows);
		frame.data[0] = bgra.data;
		frame.linesize[0] = static_cast<uint32_t>(bgra.step[0]);
		UNUSED_PARAMETER(message_timestamp_us);
		UNUSED_PARAMETER(frame_timestamp_us);
		frame.timestamp = os_gettime_ns();
		obs_source_output_video(source, &frame);
	}
};

} // namespace

extern "C" const char *cacam_usb_source_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("CaCamUsbSource");
}

extern "C" void *cacam_usb_source_create(obs_data_t *settings, obs_source_t *source)
{
	return new CacamUsbSource(settings, source);
}

extern "C" void cacam_usb_source_destroy(void *data)
{
	delete static_cast<CacamUsbSource *>(data);
}

extern "C" uint32_t cacam_usb_source_get_width(void *data)
{
	const auto *source = static_cast<CacamUsbSource *>(data);
	return source ? source->get_width() : 1280;
}

extern "C" uint32_t cacam_usb_source_get_height(void *data)
{
	const auto *source = static_cast<CacamUsbSource *>(data);
	return source ? source->get_height() : 720;
}

extern "C" void cacam_usb_source_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "auto_connect", true);
}

extern "C" obs_properties_t *cacam_usb_source_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_bool(properties, "auto_connect", obs_module_text("CaCamUsbAutoConnect"));
	return properties;
}

extern "C" void cacam_usb_source_update(void *data, obs_data_t *settings)
{
	auto *source = static_cast<CacamUsbSource *>(data);
	if (source)
		source->update(settings);
}

extern "C" void cacam_usb_source_activate(void *data)
{
	auto *source = static_cast<CacamUsbSource *>(data);
	if (source)
		source->activate();
}

extern "C" void cacam_usb_source_deactivate(void *data)
{
	auto *source = static_cast<CacamUsbSource *>(data);
	if (source)
		source->deactivate();
}
