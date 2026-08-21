#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QVariantMap>

#include <mutex>
#include <set>
#include <map>

#include "cec_injector/mate/frontend/frontend_backend.h"

class QDBusServiceWatcher;

namespace cec_injector::mate::gui {

class DaemonMateFrontendBackend final : public QObject,
                                        public frontend::MateFrontendBackend {
  Q_OBJECT
 public:
  explicit DaemonMateFrontendBackend(
      QDBusConnection connection = QDBusConnection::systemBus(),
      QObject* parent = nullptr);
  ~DaemonMateFrontendBackend() override;

  void set_observer(frontend::MateFrontendObserver* observer) override;
  [[nodiscard]] frontend::FrontendStatus status() const override;
  [[nodiscard]] frontend::FrontendCapabilities capabilities() const override;
  [[nodiscard]] frontend::FrontendConfigurationSnapshot configuration() const override;
  [[nodiscard]] frontend::FrontendConfigurationRequestId request_configuration(
      std::uint64_t base_revision,
      const frontend::FrontendConfiguration& configuration) override;
  [[nodiscard]] frontend::FrontendActionRequestId request_action(
      frontend::FrontendAction action) override;

 private slots:
  void serviceAppeared();
  void serviceVanished();
  void statusSignal(const QVariantMap& value);
  void configurationSignal(const QVariantMap& value);
  void actionSignal(const QVariantMap& value);
  void cecSignal(const QVariantMap& value);

 private:
  void refresh();
  QDBusConnection connection_;
  QDBusServiceWatcher* watcher_{};
  mutable std::mutex mutex_;
  frontend::MateFrontendObserver* observer_{};
  frontend::FrontendStatus status_;
  frontend::FrontendCapabilities capabilities_;
  frontend::FrontendConfigurationSnapshot configuration_;
  std::set<std::uint64_t> pending_actions_;
  std::map<std::uint64_t, frontend::FrontendActionResult> early_actions_;
  std::uint64_t next_configuration_request_{1};
};

}  // namespace cec_injector::mate::gui
