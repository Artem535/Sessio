#include "client_info.h"
#include "../../widgets/constants.hpp"
#include "ui/pages/ui_clientinfo.h"

Q_LOGGING_CATEGORY(logClientInfo, "pcm.ClientInfo")

namespace {

class ClientFilterModel final : public QSortFilterProxyModel {
public:
  using QSortFilterProxyModel::QSortFilterProxyModel;

  void setShowInactiveClients(const bool showInactive) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    mShowInactiveClients = showInactive;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    mShowInactiveClients = showInactive;
    invalidateFilter();
#endif
  }

  void setQuery(const QString &query) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    mQuery = query.trimmed().toCaseFolded();
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    mQuery = query.trimmed().toCaseFolded();
    invalidateFilter();
#endif
  }

protected:
  bool filterAcceptsRow(int source_row,
                        const QModelIndex &source_parent) const override {
    const auto index = sourceModel()->index(source_row, 0, source_parent);
    const auto clientVar = sourceModel()->data(index, QClientModel::ClientRoles::Full_object);
    const auto client = clientVar.value<DuckClient>();

    if (!mShowInactiveClients && !client.client_active) {
      return false;
    }

    const auto needle = mQuery;
    if (needle.isEmpty()) {
      return true;
    }

    const QString haystack =
        QStringList{
            QString::fromStdString(client.name.value_or("")),
            QString::fromStdString(client.last_name.value_or("")),
            QString::fromStdString(client.email.value_or("")),
            QString::fromStdString(client.phone_number.value_or("")),
            QString::fromStdString(client.city.value_or(""))
        }.join(' ').toCaseFolded();

    return haystack.contains(needle);
  }

private:
  QString mQuery;
  bool mShowInactiveClients = false;
};

} // namespace

ClientInfo::ClientInfo(std::shared_ptr<QClientModel> model, QWidget *parent)
    : QWidget(parent), mUi(std::make_unique<Ui::ClientInfo>()),
      mClientModel(std::move(model)) {
  mUi->setupUi(this);

  mLoadingLabel = new QLabel(tr(": CLIENTS_LOADING"), this);
  mLoadingLabel->setAlignment(Qt::AlignCenter);
  mUi->verticalLayout->insertWidget(0, mLoadingLabel);

  mFilterModel = new ClientFilterModel(this);
  mFilterModel->setSourceModel(mClientModel.get());
  mFilterModel->setDynamicSortFilter(true);

  mUi->listView->setModel(mFilterModel);
  mUi->listView->setViewMode(QListView::ListMode);
  mUi->listView->setFrameShape(QFrame::NoFrame);
  mUi->listView->setSpacing(10);
  mUi->listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  mUi->listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  mUi->listView->setStyleSheet(QStringLiteral(
      "QListView#listView {"
      " background-color: rgba(%1, %2, %3, %4);"
      " border: 1px solid rgba(%5, %6, %7, %8);"
      " border-radius: 16px;"
      " padding: %9px;"
      " outline: none;"
      "}"
      "QListView#listView::item {"
      " background: transparent;"
      " border: none;"
      "}").arg(
      QString::number(pcm::widgets::constants::kSurfaceBackgroundColor.red()),
      QString::number(pcm::widgets::constants::kSurfaceBackgroundColor.green()),
      QString::number(pcm::widgets::constants::kSurfaceBackgroundColor.blue()),
      QString::number(pcm::widgets::constants::kSurfaceBackgroundColor.alpha()),
      QString::number(pcm::widgets::constants::kSurfaceBorderColor.red()),
      QString::number(pcm::widgets::constants::kSurfaceBorderColor.green()),
      QString::number(pcm::widgets::constants::kSurfaceBorderColor.blue()),
      QString::number(pcm::widgets::constants::kSurfaceBorderColor.alpha()),
      QString::number(pcm::widgets::constants::kPanelPadding)));

  const auto delegate = new QClientDelegate(mUi->listView);
  connectSignals(delegate);

  mUi->listView->setItemDelegate(delegate);

  connect(mClientModel.get(), &QClientModel::loadingStateChanged, this,
          [this](const bool isLoading) {
            if (isLoading) {
              mLoadingLabel->setText(tr(": CLIENTS_LOADING"));
            }
            mLoadingLabel->setVisible(isLoading);
            mUi->listView->setEnabled(!isLoading);
          });

  connect(mClientModel.get(), &QClientModel::loadFailed, this,
          [this](const QString &message) {
            mLoadingLabel->setText(tr(": CLIENTS_LOADING_ERROR %1")
                                       .arg(message));
            mLoadingLabel->setVisible(true);
          });

  mLoadingLabel->setVisible(mClientModel->isLoading());
  mUi->listView->setEnabled(!mClientModel->isLoading());
}

ClientInfo::~ClientInfo() = default;

void ClientInfo::setSearchQuery(const QString &query) const {
  if (!mFilterModel) {
    return;
  }

  static_cast<ClientFilterModel *>(mFilterModel)->setQuery(query);
}

void ClientInfo::setShowInactiveClients(const bool showInactive) const {
  if (!mFilterModel) {
    return;
  }

  static_cast<ClientFilterModel *>(mFilterModel)->setShowInactiveClients(showInactive);
}

void ClientInfo::connectSignals(const QClientDelegate *delegate) {
  connect(
      delegate, &QClientDelegate::displayButtonClicked, [&](const auto index) {
        qCDebug(logClientInfo) << "Display button clicked for index: " << index;

        const QVariant clientVar =
            index.data(QClientModel::ClientRoles::Full_object);
        const auto client = clientVar.value<DuckClient>();

        emit displayButtonClicked(client);
      });

  connect(
      delegate, &QClientDelegate::notesButtonClicked, [&](const auto index) {
        qCDebug(logClientInfo) << "Notes button clicked for index: " << index;

        const QVariant clientVar =
            index.data(QClientModel::ClientRoles::Full_object);
        const auto client = clientVar.value<DuckClient>();

        emit notesButtonClicked(client);
      });

  connect(
      delegate, &QClientDelegate::removeButtonClicked, [&](const auto index) {
        qCDebug(logClientInfo) << "Remove button clicked for index: " << index;

        const QVariant clientVar =
            index.data(QClientModel::ClientRoles::Full_object);
        const auto client = clientVar.value<DuckClient>();
        if (client.id <= 0) {
          return;
        }

        const auto firstName = QString::fromStdString(client.name.value_or(""));
        const auto lastName = QString::fromStdString(client.last_name.value_or(""));
        const auto clientName = QString("%1 %2").arg(firstName, lastName).trimmed();
        const auto promptName = clientName.isEmpty() ? tr("this client") : clientName;

        const auto reply = QMessageBox::question(
            this, tr("Delete client"),
            tr("Delete %1? This action cannot be undone.").arg(promptName) + "\n" +
                tr("The transcript will be kept."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) {
          return;
        }

        emit removeButtonClicked(client.id);
      });
}
