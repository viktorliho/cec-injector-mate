#include "cec_injector/mate/gui/cec_monitor_model.h"

#include <QDateTime>
#include <QStringList>

namespace cec_injector::mate::gui {
namespace {

QString HexByte(std::uint8_t value) {
  return QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

}  // namespace

CecMonitorModel::CecMonitorModel(QObject* parent) : QAbstractTableModel(parent) {}

int CecMonitorModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(events_.size());
}

int CecMonitorModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : 7;
}

QVariant CecMonitorModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || role != Qt::DisplayRole || index.row() < 0 ||
      index.row() >= static_cast<int>(events_.size())) {
    return {};
  }
  const auto& event = events_[static_cast<std::size_t>(index.row())];
  switch (index.column()) {
    case 0:
      return QDateTime::fromMSecsSinceEpoch(
                 std::chrono::duration_cast<std::chrono::milliseconds>(
                     event.timestamp.time_since_epoch())
                     .count())
          .toString(QStringLiteral("HH:mm:ss.zzz"));
    case 1:
      return event.direction == frontend::CecDirection::Received ? "RX" : "TX";
    case 2:
      return HexByte(event.source);
    case 3:
      return HexByte(event.destination);
    case 4:
      return HexByte(event.opcode);
    case 5: {
      QStringList bytes;
      for (const auto byte : event.parameters) bytes.push_back(HexByte(byte));
      return bytes.join(QLatin1Char(' '));
    }
    case 6:
      return event.semantic ? QString::fromStdString(*event.semantic) : QString{};
    default:
      return {};
  }
}

QVariant CecMonitorModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const {
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
  static const QStringList headers{"Time", "Direction", "Source",
                                   "Destination", "Opcode", "Data", "Semantic"};
  return section >= 0 && section < headers.size() ? headers.at(section)
                                                  : QVariant{};
}

void CecMonitorModel::setEvents(
    std::vector<frontend::FrontendCecEvent> events) {
  beginResetModel();
  events_ = std::move(events);
  endResetModel();
}

void CecMonitorModel::appendEvent(frontend::FrontendCecEvent event) {
  if (events_.size() == kMaximumEvents) {
    beginRemoveRows({}, 0, 0);
    events_.erase(events_.begin());
    endRemoveRows();
  }
  const auto row = static_cast<int>(events_.size());
  beginInsertRows({}, row, row);
  events_.push_back(std::move(event));
  endInsertRows();
}

void CecMonitorModel::clear() {
  beginResetModel();
  events_.clear();
  endResetModel();
}

}  // namespace cec_injector::mate::gui
