#include "cec_injector/mate/linux/linux_physical_address.h"

#include <fcntl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <charconv>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace cec_injector::mate::linux_platform {
namespace {

struct Fd {
  int value{-1};
  ~Fd() { if (value >= 0) close(value); }
};
struct ResourcesDeleter {
  void operator()(drmModeRes* value) const { drmModeFreeResources(value); }
};
struct ConnectorDeleter {
  void operator()(drmModeConnector* value) const { drmModeFreeConnector(value); }
};
struct PropertyDeleter {
  void operator()(drmModePropertyRes* value) const { drmModeFreeProperty(value); }
};
struct BlobDeleter {
  void operator()(drmModePropertyBlobRes* value) const {
    drmModeFreePropertyBlob(value);
  }
};

std::optional<std::uint32_t> ReadConnectorId(
    const std::filesystem::path& connector) {
  std::ifstream input(connector / "connector_id");
  std::string text;
  input >> text;
  if (!input) return std::nullopt;
  std::uint32_t value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::filesystem::path> CardDevice(
    const std::filesystem::path& connector) {
  std::error_code error;
  const auto canonical = std::filesystem::canonical(connector, error);
  if (error) return std::nullopt;
  const auto card = canonical.parent_path().filename().string();
  if (!card.starts_with("card") || card.size() == 4) return std::nullopt;
  for (const char character : std::string_view(card).substr(4)) {
    if (character < '0' || character > '9') return std::nullopt;
  }
  return std::filesystem::path("/dev/dri") / card;
}

std::optional<std::vector<std::uint8_t>> ReadDrmEdid(
    const std::filesystem::path& connector_path) {
  const auto connector_id = ReadConnectorId(connector_path);
  const auto card_device = CardDevice(connector_path);
  if (!connector_id || !card_device) return std::nullopt;

  Fd fd{open(card_device->c_str(), O_RDONLY | O_CLOEXEC)};
  if (fd.value < 0) return std::nullopt;
  std::unique_ptr<drmModeRes, ResourcesDeleter> resources(
      drmModeGetResources(fd.value));
  if (!resources) return std::nullopt;

  bool listed = false;
  for (int index = 0; index < resources->count_connectors; ++index) {
    if (resources->connectors[index] == *connector_id) listed = true;
  }
  if (!listed) return std::nullopt;

  std::unique_ptr<drmModeConnector, ConnectorDeleter> connector(
      drmModeGetConnector(fd.value, *connector_id));
  if (!connector || connector->connector_id != *connector_id ||
      connector->connection != DRM_MODE_CONNECTED) {
    return std::nullopt;
  }

  std::optional<std::uint32_t> blob_id;
  for (int index = 0; index < connector->count_props; ++index) {
    std::unique_ptr<drmModePropertyRes, PropertyDeleter> property(
        drmModeGetProperty(fd.value, connector->props[index]));
    if (!property || std::string_view(property->name) != "EDID" ||
        (property->flags & DRM_MODE_PROP_BLOB) == 0) {
      continue;
    }
    if (connector->prop_values[index] == 0 || blob_id) return std::nullopt;
    blob_id = static_cast<std::uint32_t>(connector->prop_values[index]);
  }
  if (!blob_id) return std::nullopt;

  std::unique_ptr<drmModePropertyBlobRes, BlobDeleter> blob(
      drmModeGetPropertyBlob(fd.value, *blob_id));
  if (!blob || blob->length == 0 || !blob->data) return std::nullopt;
  const auto* begin = static_cast<const std::uint8_t*>(blob->data);
  return std::vector<std::uint8_t>(begin, begin + blob->length);
}

}  // namespace

DrmEdidReader create_libdrm_edid_reader() { return ReadDrmEdid; }

}  // namespace cec_injector::mate::linux_platform
