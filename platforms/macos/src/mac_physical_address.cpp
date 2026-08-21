#include "cec_injector/mate/macos/mac_physical_address.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

#include "cec_injector/mate/edid/physical_address.h"

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

bool to_bool(CFTypeRef value) {
  return value && CFGetTypeID(value) == CFBooleanGetTypeID() &&
         CFBooleanGetValue(static_cast<CFBooleanRef>(value));
}

std::vector<std::uint8_t> to_bytes(CFTypeRef value) {
  if (!value || CFGetTypeID(value) != CFDataGetTypeID()) return {};
  const auto data = static_cast<CFDataRef>(value);
  const auto* bytes = CFDataGetBytePtr(data);
  return std::vector<std::uint8_t>(bytes, bytes + CFDataGetLength(data));
}

std::vector<MacDisplayCandidate> enumerate_candidates_impl() {
  CFMutableDictionaryRef matching =
      IOServiceMatching("IOPortTransportStateDisplayPort");
  if (!matching) {
    throw std::runtime_error("DisplayPort matching dictionary missing");
  }

  io_iterator_t iterator = IO_OBJECT_NULL;
  const auto result =
      IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
  if (result != KERN_SUCCESS) {
    throw std::runtime_error("DisplayPort service enumeration failed");
  }

  IoObject iterator_object(iterator);
  std::vector<MacDisplayCandidate> candidates;
  while (const io_object_t service = IOIteratorNext(iterator_object.get())) {
    IoObject service_object(service);
    MacDisplayCandidate candidate;
    if (IORegistryEntryGetRegistryEntryID(service_object.get(),
                                          &candidate.registry_entry_id) !=
        KERN_SUCCESS) {
      continue;
    }
    candidate.service_class = "IOPortTransportStateDisplayPort";
    candidate.product_name =
        to_string(copy_property(service_object.get(), CFSTR("ProductName")).get());
    candidate.transport_description =
        to_string(copy_property(service_object.get(), CFSTR("TransportDescription"))
                      .get());
    candidate.active = to_bool(copy_property(service_object.get(), CFSTR("Active")).get());
    candidate.edid = to_bytes(copy_property(service_object.get(), CFSTR("EDID")).get());
    if (candidate.edid->empty()) candidate.edid.reset();
    candidate.plausible_external = candidate.active;
    candidates.push_back(std::move(candidate));
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const MacDisplayCandidate& left, const MacDisplayCandidate& right) {
              return left.registry_entry_id < right.registry_entry_id;
            });
  return candidates;
}

}  // namespace

MacPhysicalAddressObservation observe_physical_address(
    const std::vector<MacDisplayCandidate>& candidates) {
  MacPhysicalAddressObservation observation;
  std::vector<std::uint16_t> addresses;
  bool unresolved_plausible = false;

  for (const auto& candidate : candidates) {
    if (!candidate.plausible_external) continue;
    ++observation.plausible_external_candidates;
    if (!candidate.edid) {
      unresolved_plausible = true;
      continue;
    }
    ++observation.candidates_with_edid;
    const auto parsed = edid::parse_edid_physical_address(*candidate.edid);
    if (parsed.status == edid::EdidPhysicalAddressStatus::PhysicalAddress) {
      addresses.push_back(*parsed.address);
      continue;
    }
    unresolved_plausible = true;
  }

  std::sort(addresses.begin(), addresses.end());
  addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
  observation.unique_addresses = addresses;

  if (addresses.size() > 1 ||
      (addresses.size() == 1 && unresolved_plausible)) {
    observation.resolution = {PhysicalAddressStatus::Ambiguous, std::nullopt};
  } else if (addresses.empty()) {
    observation.resolution = {PhysicalAddressStatus::Unavailable, std::nullopt};
  } else {
    observation.resolution = {PhysicalAddressStatus::Available, addresses.front()};
  }
  return observation;
}

MacPhysicalAddressResolver::MacPhysicalAddressResolver()
    : enumerator_([] { return enumerate_candidates_impl(); }) {}

MacPhysicalAddressResolver::MacPhysicalAddressResolver(Enumerator enumerator)
    : enumerator_(std::move(enumerator)) {}

std::vector<MacDisplayCandidate> MacPhysicalAddressResolver::enumerate_candidates() const {
  return enumerator_ ? enumerator_() : std::vector<MacDisplayCandidate>{};
}

MacPhysicalAddressObservation MacPhysicalAddressResolver::observe() const {
  return observe_physical_address(enumerate_candidates());
}

PhysicalAddressResolution MacPhysicalAddressResolver::resolve() const {
  return observe().resolution;
}

}  // namespace cec_injector::mate::macos_platform
