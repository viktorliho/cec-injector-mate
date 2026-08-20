#include "cec_injector/mate/macos/mac_device_discovery.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace cec_injector::mate::macos_platform {
namespace {

class IoObject {
 public:
  explicit IoObject(io_object_t value = IO_OBJECT_NULL) : value_(value) {}
  ~IoObject() {
    if (value_ != IO_OBJECT_NULL) IOObjectRelease(value_);
  }

  IoObject(const IoObject&) = delete;
  IoObject& operator=(const IoObject&) = delete;

  IoObject(IoObject&& other) noexcept : value_(other.value_) {
    other.value_ = IO_OBJECT_NULL;
  }
  IoObject& operator=(IoObject&& other) noexcept {
    if (this == &other) return *this;
    if (value_ != IO_OBJECT_NULL) IOObjectRelease(value_);
    value_ = other.value_;
    other.value_ = IO_OBJECT_NULL;
    return *this;
  }

  [[nodiscard]] io_object_t get() const noexcept { return value_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return value_ != IO_OBJECT_NULL;
  }

 private:
  io_object_t value_;
};

using CfTypePtr = std::unique_ptr<std::remove_pointer_t<CFTypeRef>,
                                  decltype(&CFRelease)>;

CfTypePtr copy_property(io_registry_entry_t entry, CFStringRef key) {
  return CfTypePtr(IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault,
                                                   0),
                   &CFRelease);
}

CfTypePtr search_property(io_registry_entry_t entry, CFStringRef key) {
  return CfTypePtr(
      IORegistryEntrySearchCFProperty(entry, kIOServicePlane, key,
                                      kCFAllocatorDefault,
                                      kIORegistryIterateParents |
                                          kIORegistryIterateRecursively),
      &CFRelease);
}

std::string to_string(CFTypeRef value) {
  if (!value || CFGetTypeID(value) != CFStringGetTypeID()) return {};
  const auto string_value = static_cast<CFStringRef>(value);
  const auto length = CFStringGetLength(string_value);
  const auto size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8);
  std::string result(static_cast<std::size_t>(size) + 1, '\0');
  if (!CFStringGetCString(string_value, result.data(),
                          static_cast<CFIndex>(result.size()),
                          kCFStringEncodingUTF8)) {
    return {};
  }
  result.resize(std::char_traits<char>::length(result.c_str()));
  return result;
}

std::uint16_t to_uint16(CFTypeRef value) {
  if (!value || CFGetTypeID(value) != CFNumberGetTypeID()) return 0;
  std::int32_t result = 0;
  if (!CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt32Type,
                        &result)) {
    return 0;
  }
  if (result < 0 || result > 0xffff) return 0;
  return static_cast<std::uint16_t>(result);
}

bool has_parent_class(io_registry_entry_t entry, const char* class_name) {
  IoObject current(entry);
  if (!current) return false;
  io_registry_entry_t parent = IO_OBJECT_NULL;
  while (IORegistryEntryGetParentEntry(current.get(), kIOServicePlane, &parent) ==
         KERN_SUCCESS) {
    IoObject parent_object(parent);
    if (IOObjectConformsTo(parent_object.get(), class_name)) return true;
    current = std::move(parent_object);
  }
  return false;
}

std::vector<MacDeviceCandidate> enumerate_candidates_impl() {
  CFMutableDictionaryRef matching = IOServiceMatching(kIOSerialBSDServiceValue);
  if (!matching) throw std::runtime_error("IOSerialBSD matching dictionary missing");
  CFDictionarySetValue(matching, CFSTR(kIOSerialBSDTypeKey),
                       CFSTR(kIOSerialBSDAllTypes));

  io_iterator_t iterator = IO_OBJECT_NULL;
  const kern_return_t result =
      IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
  if (result != KERN_SUCCESS) {
    throw std::runtime_error("IOSerialBSD enumeration failed");
  }

  IoObject iterator_object(iterator);
  std::vector<MacDeviceCandidate> candidates;
  while (const io_object_t service = IOIteratorNext(iterator_object.get())) {
    IoObject service_object(service);
    std::uint64_t registry_entry_id = 0;
    if (IORegistryEntryGetRegistryEntryID(service_object.get(),
                                          &registry_entry_id) != KERN_SUCCESS) {
      continue;
    }

    MacDeviceCandidate candidate;
    candidate.registry_entry_id = registry_entry_id;
    candidate.tty_device =
        to_string(copy_property(service_object.get(), CFSTR(kIOTTYDeviceKey)).get());
    candidate.callout_device = to_string(
        copy_property(service_object.get(), CFSTR(kIOCalloutDeviceKey)).get());
    candidate.dialin_device = to_string(
        copy_property(service_object.get(), CFSTR(kIODialinDeviceKey)).get());
    candidate.serial_client_type = to_string(
        copy_property(service_object.get(), CFSTR("IOSerialBSDClientType")).get());
    candidate.vendor_id =
        to_uint16(search_property(service_object.get(), CFSTR("idVendor")).get());
    candidate.product_id =
        to_uint16(search_property(service_object.get(), CFSTR("idProduct")).get());
    candidate.serial = to_string(
        search_property(service_object.get(), CFSTR("USB Serial Number")).get());
    if (candidate.serial.empty()) {
      candidate.serial = to_string(search_property(service_object.get(),
                                                   CFSTR("kUSBSerialNumberString"))
                                       .get());
    }
    candidate.has_usb_device_ancestry =
        has_parent_class(service_object.get(), "IOUSBHostDevice");
    candidates.push_back(std::move(candidate));
  }
  return candidates;
}

}  // namespace

MacDeviceDiscovery::MacDeviceDiscovery()
    : enumerator_([] { return enumerate_candidates_impl(); }) {}

}  // namespace cec_injector::mate::macos_platform
