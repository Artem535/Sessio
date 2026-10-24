#include "prompter_panel.h"
#include "phrase_card_paint.h"
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QMessageBox>

namespace {
class ReviewCard final : public QGroupBox {
public:
  using QGroupBox::QGroupBox;
protected:
  void paintEvent(QPaintEvent *) override {
    paintPhraseCard(this, false);
    QPainter painter(this);
    painter.setPen(palette().color(QPalette::WindowText));
    auto heading = font(); heading.setWeight(QFont::DemiBold); painter.setFont(heading);
    painter.drawText(rect().adjusted(14, 10, -14, -10), Qt::AlignLeft | Qt::AlignTop, title());
  }
};
QLabel *plainLabel(const QString &text, QWidget *parent) {
  auto *label = new QLabel(text, parent); label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true); label->setTextInteractionFlags(Qt::TextSelectableByMouse); return label;
}
}
PrompterPanel::PrompterPanel(CallPrompterController *controller, std::shared_ptr<pcm::database::Database> db, QWidget *parent)
    : QWidget(parent), mController(controller), mDb(std::move(db)) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 16, 16, 12); layout->setSpacing(12);
  mStatus = plainLabel({}, this); mStatus->setObjectName("prompterStatus"); layout->addWidget(mStatus);
  auto *bar = new QHBoxLayout;
  mStart = new QPushButton(tr("Start prompter"), this); mStart->setObjectName("prompterStart");
  mPause = new QPushButton(tr("Pause"), this); mPause->setObjectName("prompterPause");
  bar->addWidget(mStart); bar->addWidget(mPause); layout->addLayout(bar);
  connect(mStart, &QPushButton::clicked, this, &PrompterPanel::startRequested);
  connect(mPause, &QPushButton::clicked, this, &PrompterPanel::pauseRequested);
  mHint = new ReviewCard(tr("You could clarify"), this);
  auto *hintLayout = new QVBoxLayout(mHint);
  hintLayout->setContentsMargins(14, fontMetrics().height() + 20, 14, 12);
  mHintText = plainLabel({}, mHint); hintLayout->addWidget(mHintText);
  auto *hide = new QPushButton(tr("Hide"), mHint); hintLayout->addWidget(hide);
  connect(hide, &QPushButton::clicked, this, [this] { mDismissedHint = mHintText->text(); mHint->hide(); });
  mHint->hide();
  auto *scroll = mScroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
  auto *content = new QWidget(scroll); auto *body = new QVBoxLayout(content);
  body->setContentsMargins(0, 0, 4, 0); body->setSpacing(12);
  auto heading = [content](const QString &text) {
    auto *result = plainLabel(text, content); auto font = result->font();
    font.setWeight(QFont::DemiBold); result->setFont(font); return result;
  };
  body->addWidget(heading(tr("For review")));
  mCards = new QVBoxLayout; mCards->setSpacing(12); body->addLayout(mCards);
  mEmpty = plainLabel(tr("New agreements will appear here after completed phrases."), content); body->addWidget(mEmpty);
  body->addWidget(mHint);
  auto *divider = new QFrame(content); divider->setFrameShape(QFrame::HLine); body->addWidget(divider);
  auto *acceptedHeading = heading(tr("Accepted agreements")); body->addWidget(acceptedHeading);
  mRecords = plainLabel({}, content); body->addWidget(mRecords); body->addStretch();
  mAcceptedSection = {divider, acceptedHeading, mRecords};
  scroll->setWidget(content); layout->addWidget(scroll, 1);
  auto *notice = plainLabel(tr("Private suggestions. Check the source before saving."), this); layout->addWidget(notice);
  connect(controller, &CallPrompterController::cardsChanged, this, &PrompterPanel::refresh);
  connect(controller, &CallPrompterController::statusChanged, this, [this](const QString &status) {
    mStatus->setText(status); mPause->setEnabled(mController && mController->running());
    mStart->setEnabled(mController && !mController->running());
  });
  refresh();
}
void PrompterPanel::setEmbedded(bool embedded) {
  if (!embedded || mScroll->isHidden()) return;
  auto *layout = static_cast<QVBoxLayout *>(this->layout());
  layout->setContentsMargins(0, 0, 0, 0);
  auto *content = mScroll->takeWidget();
  layout->insertWidget(layout->indexOf(mScroll), content);
  content->layout()->setContentsMargins(0, 0, 0, 0);
  content->setAutoFillBackground(false);  // QScrollArea turned it on
  mScroll->hide();
  for (auto *widget : mAcceptedSection) widget->hide();
}
void PrompterPanel::refresh() {
  if (!mController) return;
  mStatus->setText(mController->status()); mPause->setEnabled(mController->running());
  mStart->setEnabled(!mController->running());
  QString hint;
  if (!mController->suggestions().isEmpty()) hint = mController->suggestions().first().toObject()["text"].toString();
  mHintText->setText(hint); mHint->setVisible(!hint.isEmpty() && hint != mDismissedHint);
  int pending = 0;
  QSet<QString> present;
  for (const auto &value : mController->cards()) {
    const auto card = value.toObject(); const auto id = card["id"].toString(); present.insert(id);
    const auto decision = card["decision"].toString();
    if (decision == "pending") ++pending;
    QWidget *widget = mCardWidgets.value(id);
    if (!widget) {
      auto *box = new ReviewCard(this); box->setObjectName("prompterCard"); widget = box;
      auto *layout = new QVBoxLayout(box);
      layout->setContentsMargins(14, fontMetrics().height() + 20, 14, 12); layout->setSpacing(10);
      auto *text = plainLabel({}, box); text->setObjectName("action"); layout->addWidget(text);
      auto *warning = plainLabel(tr("The source was changed or deleted. Review the transcript."), box);
      warning->setObjectName("sourceWarning"); layout->addWidget(warning);
      auto *target = plainLabel({}, box); target->setObjectName("target"); layout->addWidget(target);
      auto *sourceLayout = new QVBoxLayout; layout->addLayout(sourceLayout);
      for (const auto &evidence : card["evidence_ids"].toArray()) {
        const auto evidenceId = evidence.toString();
        QJsonObject source = mController->source(evidenceId);
        for (const auto &snapshot : card["evidence_snapshot"].toArray())
          if (snapshot.toObject()["id"].toString() == evidenceId) source = snapshot.toObject();
        const auto ms = source["start_ms"].toInteger();
        auto *quote = plainLabel(source["speaker"].toString() + ": «" + source["text"].toString() + "»", box);
        layout->addWidget(quote);
        auto *jump = new QPushButton(tr("Source · %1:%2").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QLatin1Char('0')), box);
        jump->setFlat(true); jump->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        jump->setToolTip(source["text"].toString());
        layout->addWidget(jump);
        connect(jump, &QPushButton::clicked, this, [this, evidenceId] { emit sourceRequested(evidenceId.toLongLong()); });
      }
      auto *actions = new QHBoxLayout;
      auto *accept = new QPushButton(tr("Accept"), box); accept->setObjectName("accept"); actions->addWidget(accept);
      auto *edit = new QPushButton(tr("Edit"), box); edit->setObjectName("edit"); actions->addWidget(edit);
      auto *reject = new QPushButton(tr("Dismiss"), box); reject->setObjectName("reject"); actions->addWidget(reject);
      layout->addLayout(actions);
      auto *save = new QPushButton(tr("To notes"), box); save->setObjectName("save"); layout->addWidget(save);
      auto *resolve = new QPushButton(tr("Choose the agreement"), box); resolve->setObjectName("resolve"); layout->addWidget(resolve);
      connect(resolve, &QPushButton::clicked, this, [this, id] {
        if (!mController) return;
        QStringList texts, ids;
        for (const auto &value : mController->cards()) {
          const auto c = value.toObject();
          if (c["id"].toString() == id && c["kind"].toString() == "accept") { texts << tr("New agreement"); ids << QString(); }
          if (c["id"].toString() != id && c["decision"].toString() == "pending" && c["kind"].toString() == "accept" &&
              c["target_id"].toString().isEmpty() && !c["unresolved"].toBool()) { texts << c["action"].toString(); ids << c["id"].toString(); }
        }
        for (const auto &value : mController->records()) {
          const auto r = value.toObject();
          if (r["active"].toBool(true)) { texts << r["text"].toString(); ids << r["id"].toString(); }
        }
        if (texts.isEmpty()) return;
        for (int i = 0; i < texts.size(); ++i) texts[i] = QString::number(i + 1) + QStringLiteral(" · ") + texts[i];
        bool ok = false; QPointer<PrompterPanel> guard(this);
        const auto text = QInputDialog::getItem(this, tr("Review the target"), tr("Agreement to change or cancel"), texts, 0, false, &ok);
        if (guard && ok && mController) mController->resolveCard(id, ids[texts.indexOf(text)]);
      });
      connect(accept, &QPushButton::clicked, this, [this, id] { if (mController) mController->acceptCard(id); });
      connect(reject, &QPushButton::clicked, this, [this, id] { if (mController) mController->rejectCard(id); });
      connect(edit, &QPushButton::clicked, this, [this, id] {
        if (!mController) return;
        QString text;
        for (const auto &c : mController->cards()) if (c.toObject()["id"].toString() == id) text = c.toObject()["action"].toString();
        QPointer<PrompterPanel> guard(this); bool ok = false;
        const auto edited = QInputDialog::getMultiLineText(this, tr("Review agreement"), tr("Agreement"), text, &ok);
        if (guard && ok && mController) mController->acceptCard(id, edited);
      });
      connect(save, &QPushButton::clicked, this, [this, id] { saveNote(id); });
      mCardWidgets.insert(id, box); mCards->addWidget(box);
    }
    static_cast<QGroupBox *>(widget)->setTitle(card["unresolved"].toBool() ? tr("Needs clarification") :
        card["kind"].toString() == "cancel" ? tr("Proposed cancellation") : !card["target_id"].toString().isEmpty() ? tr("Proposed change") : tr("Agreement"));
    widget->findChild<QLabel *>("action")->setText(card["action"].toString());
    QString previous = card["previous_action"].toString();
    if (previous.isEmpty()) for (const auto &r : mController->records()) if (r.toObject()["id"] == card["target_id"]) previous = r.toObject()["text"].toString();
    if (previous.isEmpty()) for (const auto &c : mController->cards()) if (c.toObject()["id"] == card["target_id"]) previous = c.toObject()["action"].toString();
    auto *target = widget->findChild<QLabel *>("target");
    target->setText(tr("Previous agreement: %1").arg(previous)); target->setVisible(!previous.isEmpty());
    widget->findChild<QPushButton *>("resolve")->setVisible(decision == "pending" && card["unresolved"].toBool());
    const bool sourceValid = mController->evidenceAvailable(id);
    widget->findChild<QLabel *>("sourceWarning")->setVisible(!sourceValid);
    const bool editable = decision == "pending" && !card["unresolved"].toBool() && sourceValid;
    widget->findChild<QPushButton *>("accept")->setEnabled(editable);
    widget->findChild<QPushButton *>("edit")->setEnabled(editable);
    widget->findChild<QPushButton *>("reject")->setEnabled(decision == "pending");
    auto *save = widget->findChild<QPushButton *>("save");
    save->setEnabled((editable || decision == "accepted") && sourceValid && card["note_id"].toInteger() <= 0);
    save->setText(card["note_id"].toInteger() > 0 ? tr("Saved to notes") : tr("To notes"));
    widget->setVisible(decision != "rejected" && decision != "superseded");
  }
  for (auto it = mCardWidgets.begin(); it != mCardWidgets.end();) {
    if (!present.contains(it.key())) { delete it.value(); it = mCardWidgets.erase(it); } else ++it;
  }
  QStringList records;
  for (const auto &value : mController->records()) {
    const auto record = value.toObject();
    if (record["active"].toBool(true)) records << QStringLiteral("• ") + record["text"].toString();
  }
  mRecords->setText(records.join("\n\n"));
  if (mScroll->isHidden()) mRecords->hide(); mEmpty->setVisible(pending == 0);
  emit pendingCountChanged(pending);
}
void PrompterPanel::saveNote(const QString &cardId) {
  if (!mController) return;
  QString decision;
  for (const auto &value : mController->cards()) if (value.toObject()["id"].toString() == cardId) decision = value.toObject()["decision"].toString();
  if (decision == "pending" && !mController->acceptCard(cardId)) return;
  if (mCallNoteSaver) {
    if (mSavedCallNotes.contains(cardId)) return;
    QString text;
    for (const auto &value : mController->cards())
      if (value.toObject()["id"].toString() == cardId) text = value.toObject()["action"].toString();
    if (!text.isEmpty() && mCallNoteSaver(cardId, text)) {
      mSavedCallNotes.insert(cardId); emit noteSaved();
      auto *widget = mCardWidgets.value(cardId);
      if (widget) { auto *save = widget->findChild<QPushButton *>("save"); save->setEnabled(false); save->setText(tr("Saved to notes")); }
    }
    return;
  }
  const auto clients = mController->clientIds();
  if (clients.empty()) {
    QMessageBox::information(this, tr("Agreement kept as a draft"),
        tr("Attach a client to this transcript to save the agreement in client notes."));
    return;
  }
  qint64 selected = clients.front();
  if (clients.size() > 1) {
    QStringList names;
    for (const auto id : clients) {
      const auto client = mDb->get_client(id);
      names << tr("%1 · #%2").arg(client ? QString::fromStdString(client->name.value_or("")) : QString()).arg(id);
    }
    bool ok = false; QPointer<PrompterPanel> guard(this);
    const auto name = QInputDialog::getItem(this, tr("Save to client notes"), tr("Client"), names, 0, false, &ok);
    if (!guard || !ok) return;
    selected = clients[names.indexOf(name)];
  }
  if (mController->saveNote(cardId, selected)) emit noteSaved();
}
