#include "agreements_widget.h"
#include <QJsonObject>
#include <QLabel>
#include <QVBoxLayout>

AgreementsWidget::AgreementsWidget(QWidget *parent) : NotesWidgetFrame(tr("Agreements"), parent) {
  mRows = new QVBoxLayout; mRows->setSpacing(4); body()->addLayout(mRows);
}

bool AgreementsWidget::hasContent() const { return mCount > 0; }

void AgreementsWidget::setRecords(const QJsonArray &records) {
  while (auto *item = mRows->takeAt(0)) { delete item->widget(); delete item; }
  mCount = 0;
  for (const auto &value : records) {
    const auto record = value.toObject();
    const auto text = record["text"].toString().trimmed();
    if (!record["active"].toBool(true) || text.isEmpty()) continue;
    auto *row = new QLabel(text, this); row->setObjectName("agreementRow");
    row->setTextFormat(Qt::PlainText); row->setWordWrap(true);
    row->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mRows->addWidget(row); ++mCount;
  }
  setStatus(mCount > 0 ? QString::number(mCount) : QString{});
}
