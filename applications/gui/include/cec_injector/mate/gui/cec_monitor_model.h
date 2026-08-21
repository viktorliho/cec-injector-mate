#pragma once

#include <QAbstractTableModel>

#include <vector>

#include "cec_injector/mate/frontend/frontend_backend.h"

namespace cec_injector::mate::gui {

class CecMonitorModel final : public QAbstractTableModel {
  Q_OBJECT

 public:
  explicit CecMonitorModel(QObject* parent = nullptr);

  [[nodiscard]] int rowCount(
      const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] int columnCount(
      const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index,
                              int role = Qt::DisplayRole) const override;
  [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                    int role) const override;

  void setEvents(std::vector<frontend::FrontendCecEvent> events);
  void appendEvent(frontend::FrontendCecEvent event);

 private:
  std::vector<frontend::FrontendCecEvent> events_;
};

}  // namespace cec_injector::mate::gui
