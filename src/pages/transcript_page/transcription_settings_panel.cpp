#include "transcription_settings_panel.h"
#include "app_settings.h"
#include "summary_limits.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QFileDialog>

TranscriptionSettingsPanel::TranscriptionSettingsPanel(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  mEnabled = new QCheckBox(tr("Enable call transcription"), this);
  mEnabled->setObjectName("transcriptionEnabled");
  layout->addWidget(mEnabled);
  mAutoStart = new QCheckBox(tr("Start transcription automatically when joining a call"), this);
  mAutoStart->setObjectName("transcriptionAutoStart");
  layout->addWidget(mAutoStart);
  mPrompterAutoStart = new QCheckBox(tr("Start the prompter automatically"), this);
  mPrompterAutoStart->setObjectName("prompterAutoStart");
  layout->addWidget(mPrompterAutoStart);
  connect(mEnabled, &QCheckBox::toggled, this, [this](bool enabled) {
    if (!enabled) { mAutoStart->setChecked(false); mPrompterAutoStart->setChecked(false); }
  });
  connect(mAutoStart, &QCheckBox::toggled, this, [this](bool enabled) {
    if (enabled) mEnabled->setChecked(true);
    else mPrompterAutoStart->setChecked(false);
    emit autoStartToggled(enabled);
  });
  connect(mPrompterAutoStart, &QCheckBox::toggled, this, [this](bool enabled) {
    if (enabled) mAutoStart->setChecked(true);
    emit prompterAutoStartToggled(enabled);
  });
  auto *runtime = new QGroupBox(tr("Local prompter (experimental)"), this);
  auto *runtimeForm = new QFormLayout(runtime);
  auto *row = new QWidget(runtime);
  auto *rowLayout = new QHBoxLayout(row);
  rowLayout->setContentsMargins(0, 0, 0, 0);
  mPrompterModel = new QLineEdit(row); mPrompterModel->setObjectName("prompterModel");
  auto *browse = new QPushButton(tr("Browse…"), row);
  rowLayout->addWidget(mPrompterModel, 1); rowLayout->addWidget(browse);
  runtimeForm->addRow(tr("Gemma model (.litertlm)"), row);
  connect(mPrompterModel, &QLineEdit::editingFinished, this, [this] { emit prompterModelChanged(mPrompterModel->text()); });
  connect(browse, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getOpenFileName(this, tr("Select a local file"), mPrompterModel->text());
    if (!path.isEmpty()) { mPrompterModel->setText(path); emit prompterModelChanged(path); }
  });
  mPrompterAcceleration = new QComboBox(runtime); mPrompterAcceleration->setObjectName("prompterAcceleration");
  mPrompterAcceleration->addItem(tr("Automatic (GPU, otherwise CPU)"), QStringLiteral("auto"));
  mPrompterAcceleration->addItem(tr("GPU only"), QStringLiteral("gpu"));
  mPrompterAcceleration->addItem(tr("CPU only"), QStringLiteral("cpu"));
  mPrompterAcceleration->setToolTip(tr("The GPU is several times faster and uses less memory, but on an integrated GPU it can make the desktop stutter. The CPU needs about 10 GB of RAM. Applies the next time the model starts."));
  runtimeForm->addRow(tr("Acceleration"), mPrompterAcceleration);
  connect(mPrompterAcceleration, &QComboBox::currentIndexChanged, this, [this] {
    emit prompterAccelerationChanged(mPrompterAcceleration->currentData().toString());
  });
  mSummaryLeaf = new QSpinBox(runtime); mSummaryLeaf->setObjectName("summaryLeafLimit");
  mSummaryLeaf->setRange(100, pcm::prompter::SummaryLimits::kLeafMax);
  mSummaryLeaf->setSingleStep(100);
  mSummaryLeaf->setToolTip(tr("Maximum length of one chunk note in the call notes. A longer note is rejected and the chunk is retried."));
  runtimeForm->addRow(tr("Note length, characters"), mSummaryLeaf);
  mSummaryOverview = new QSpinBox(runtime); mSummaryOverview->setObjectName("summaryOverviewLimit");
  mSummaryOverview->setRange(500, pcm::prompter::SummaryLimits::kOverviewMax);
  mSummaryOverview->setSingleStep(100);
  mSummaryOverview->setToolTip(tr("Maximum length of the overall call summary."));
  runtimeForm->addRow(tr("Summary length, characters"), mSummaryOverview);
  mSummaryMode = new QComboBox(runtime); mSummaryMode->setObjectName("summaryMode");
  mSummaryMode->addItem(tr("Model overview"), QStringLiteral("model"));
  mSummaryMode->addItem(tr("Join part notes (fast)"), QStringLiteral("parts"));
  mSummaryMode->setToolTip(tr("Joining skips the final model call: faster and never loses parts, but reads as sequential notes instead of one coherent story."));
  runtimeForm->addRow(tr("Final overview"), mSummaryMode);
  connect(mSummaryMode, &QComboBox::currentIndexChanged, this, [this] {
    emit summaryModeChanged(mSummaryMode->currentData().toString() == QLatin1String("parts"));
  });
  const auto emitSummaryLimits = [this] { emit summaryLimitsChanged(mSummaryLeaf->value(), mSummaryOverview->value()); };
  connect(mSummaryLeaf, &QSpinBox::valueChanged, this, emitSummaryLimits);
  connect(mSummaryOverview, &QSpinBox::valueChanged, this, emitSummaryLimits);
  setSummaryLimits(pcm::prompter::SummaryLimits{}.leaf, pcm::prompter::SummaryLimits{}.overview);
  auto *runtimeNote = new QLabel(tr("Choose a downloaded Gemma model. The model runs on this computer, and nothing is downloaded automatically. Prompter suggestions require your review."), runtime);
  runtimeNote->setWordWrap(true);
  runtimeForm->addRow(runtimeNote);
  layout->addWidget(runtime);
  auto *rag = new QGroupBox(tr("Wiki hints (RAG, experimental)"), this);
  auto *ragForm = new QFormLayout(rag);
  auto *ragEnabled = new QCheckBox(tr("Search the wiki during calls"), rag);
  ragEnabled->setObjectName("ragEnabled");
  ragEnabled->setChecked(pcm::app_settings::ragEnabled());
  connect(ragEnabled, &QCheckBox::toggled, this, [](bool on) { pcm::app_settings::setRagEnabled(on); });
  ragForm->addRow(ragEnabled);
  auto *ragAll = new QCheckBox(tr("Search every phrase, even without question words"), rag);
  ragAll->setObjectName("ragSearchAllPhrases");
  ragAll->setChecked(pcm::app_settings::ragSearchAllPhrases());
  connect(ragAll, &QCheckBox::toggled, this, [](bool on) { pcm::app_settings::setRagSearchAllPhrases(on); });
  ragForm->addRow(ragAll);
  auto *ragHead = new QCheckBox(tr("Use the trained router head (experimental)"), rag);
  ragHead->setObjectName("ragUseTrainedRouter");
  ragHead->setChecked(pcm::app_settings::ragUseTrainedRouter());
  connect(ragHead, &QCheckBox::toggled, this, [](bool on) { pcm::app_settings::setRagUseTrainedRouter(on); });
  ragForm->addRow(ragHead);
  auto *ragRewrite = new QComboBox(rag);
  ragRewrite->setObjectName("ragRewriteMode");
  ragRewrite->addItem(tr("Rules"), QStringLiteral("rules"));
  ragRewrite->addItem(tr("Local model"), QStringLiteral("local"));
  ragRewrite->addItem(tr("Cloud model"), QStringLiteral("cloud"));
  ragRewrite->setCurrentIndex(qMax(0, ragRewrite->findData(pcm::app_settings::ragRewriteMode())));
  connect(ragRewrite, &QComboBox::currentIndexChanged, this,
          [ragRewrite](int) { pcm::app_settings::setRagRewriteMode(ragRewrite->currentData().toString()); });
  ragForm->addRow(tr("Query rewrite"), ragRewrite);
  auto *ragUrl = new QLineEdit(pcm::app_settings::ragOutlineUrl(), rag);
  ragUrl->setObjectName("ragOutlineUrl");
  connect(ragUrl, &QLineEdit::editingFinished, this, [ragUrl] { pcm::app_settings::setRagOutlineUrl(ragUrl->text()); });
  ragForm->addRow(tr("Outline URL"), ragUrl);
  auto *ragToken = new QLineEdit(pcm::app_settings::ragOutlineToken(), rag);
  ragToken->setObjectName("ragOutlineToken");
  ragToken->setEchoMode(QLineEdit::Password);
  connect(ragToken, &QLineEdit::editingFinished, this, [ragToken] { pcm::app_settings::setRagOutlineToken(ragToken->text()); });
  ragForm->addRow(tr("Outline token (read-only)"), ragToken);
  auto *ragAllow = new QLineEdit(pcm::app_settings::ragAllowlist().join(','), rag);
  ragAllow->setObjectName("ragAllowlist");
  connect(ragAllow, &QLineEdit::editingFinished, this,
          [ragAllow] { pcm::app_settings::setRagAllowlist(ragAllow->text().split(',', Qt::SkipEmptyParts)); });
  ragForm->addRow(tr("Collection ids (empty = all)"), ragAllow);
  auto *ragModelRow = new QWidget(rag);
  auto *ragModelLayout = new QHBoxLayout(ragModelRow);
  ragModelLayout->setContentsMargins(0, 0, 0, 0);
  auto *ragModel = new QLineEdit(pcm::app_settings::ragEmbedModel(), ragModelRow);
  ragModel->setObjectName("ragEmbedModel");
  auto *ragBrowse = new QPushButton(tr("Browse…"), ragModelRow);
  ragModelLayout->addWidget(ragModel);
  ragModelLayout->addWidget(ragBrowse);
  connect(ragModel, &QLineEdit::editingFinished, this, [ragModel] { pcm::app_settings::setRagEmbedModel(ragModel->text()); });
  connect(ragBrowse, &QPushButton::clicked, this, [this, ragModel] {
    const auto path = QFileDialog::getOpenFileName(this, tr("Embedding model"), {}, tr("LiteRT models (*.litertlm)"));
    if (!path.isEmpty()) { ragModel->setText(path); pcm::app_settings::setRagEmbedModel(path); }
  });
  ragForm->addRow(tr("Embedding model (.litertlm)"), ragModelRow);
  auto *ragNote = new QLabel(tr("Hints are computed on this computer from the indexed wiki. The first indexing runs in the background and can take about an hour on CPU."), rag);
  ragNote->setWordWrap(true);
  ragForm->addRow(ragNote);
  layout->addWidget(rag);
  mModel = new QLabel(this);
  mModel->setObjectName("transcriptionModelInfo");
  mModel->setTextFormat(Qt::PlainText);
  layout->addWidget(mModel);
  auto *note = new QLabel(tr("Transcription runs on this computer. Audio is never stored. Text is saved in the local database next to your other data and included in backups."), this);
  note->setWordWrap(true);
  layout->addWidget(note);
  auto *box = new QGroupBox(tr("Speech recognition"), this);
  auto *form = new QFormLayout(box);
  using T = pcm::transcription::TranscriptionTuning;
  auto seconds = [box](const char *name, float lo, float hi, float step, const QString &tip) {
    auto *spin = new QDoubleSpinBox(box);
    spin->setObjectName(name);
    spin->setRange(lo, hi);
    spin->setSingleStep(step);
    spin->setDecimals(2);
    spin->setSuffix(QObject::tr(" s"));
    spin->setToolTip(tip);
    return spin;
  };
  mThreshold = seconds("vadThreshold", T::kMinThreshold, T::kMaxThreshold, 0.05F,
                       tr("How confident the detector must be that a sound is speech. Raise it if background noise becomes text, lower it if quiet speech is missed."));
  mThreshold->setSuffix({});
  mMinSilence = seconds("vadMinSilence", T::kMinSilence, T::kMaxSilence, 0.1F,
                        tr("A pause this long ends a phrase. Shorter pauses give shorter, quicker phrases."));
  mMinSpeech = seconds("vadMinSpeech", T::kMinSpeech, T::kMaxSpeech, 0.05F,
                       tr("Sounds shorter than this are ignored."));
  mMaxPhrase = seconds("vadMaxPhrase", T::kMinPhrase, T::kMaxPhrase, 1.0F,
                       tr("Continuous speech longer than this is cut into several phrases."));
  mThreads = new QSpinBox(box);
  mThreads->setObjectName("recognizerThreads");
  mThreads->setRange(T::kMinThreads, T::kMaxThreads);
  mThreads->setToolTip(tr("CPU threads for the speech model. More threads are faster but load the computer during a call."));
  form->addRow(tr("Speech sensitivity"), mThreshold);
  form->addRow(tr("Pause that ends a phrase"), mMinSilence);
  form->addRow(tr("Shortest speech"), mMinSpeech);
  form->addRow(tr("Longest phrase"), mMaxPhrase);
  form->addRow(tr("Recognizer threads"), mThreads);
  auto *reset = new QPushButton(tr("Restore defaults"), box);
  reset->setObjectName("resetTranscriptionTuning");
  form->addRow(reset);
  auto *note2 = new QLabel(tr("Changes apply to the next transcription."), box);
  note2->setWordWrap(true);
  form->addRow(note2);
  layout->addWidget(box);
  auto emitTuning = [this] { emit tuningChanged(tuning()); };
  for (auto *spin : {mThreshold, mMinSilence, mMinSpeech, mMaxPhrase})
    connect(spin, &QDoubleSpinBox::valueChanged, this, emitTuning);
  connect(mThreads, &QSpinBox::valueChanged, this, emitTuning);
  connect(reset, &QPushButton::clicked, this, [this] { setTuning({}); emit tuningChanged(tuning()); });
  setTuning({});
  mCount = new QLabel(this);
  layout->addWidget(mCount);
  mDelete = new QPushButton(tr("Delete all transcripts…"), this);
  mDelete->setObjectName("deleteAllTranscripts");
  layout->addWidget(mDelete);
  layout->addStretch();
  connect(mEnabled, &QCheckBox::toggled, this, &TranscriptionSettingsPanel::enabledToggled);
  connect(mDelete, &QPushButton::clicked, this, &TranscriptionSettingsPanel::deleteAllRequested);
  setTranscriptCount(0);
}
void TranscriptionSettingsPanel::setTuning(const pcm::transcription::TranscriptionTuning &tuning) {
  const auto t = tuning.clamped();
  for (auto *widget : {static_cast<QWidget *>(mThreshold), static_cast<QWidget *>(mMinSilence),
                       static_cast<QWidget *>(mMinSpeech), static_cast<QWidget *>(mMaxPhrase),
                       static_cast<QWidget *>(mThreads)})
    widget->blockSignals(true);
  mThreshold->setValue(t.threshold);
  mMinSilence->setValue(t.minSilenceSec);
  mMinSpeech->setValue(t.minSpeechSec);
  mMaxPhrase->setValue(t.maxPhraseSec);
  mThreads->setValue(t.numThreads);
  for (auto *widget : {static_cast<QWidget *>(mThreshold), static_cast<QWidget *>(mMinSilence),
                       static_cast<QWidget *>(mMinSpeech), static_cast<QWidget *>(mMaxPhrase),
                       static_cast<QWidget *>(mThreads)})
    widget->blockSignals(false);
}

pcm::transcription::TranscriptionTuning TranscriptionSettingsPanel::tuning() const {
  pcm::transcription::TranscriptionTuning t;
  t.threshold = static_cast<float>(mThreshold->value());
  t.minSilenceSec = static_cast<float>(mMinSilence->value());
  t.minSpeechSec = static_cast<float>(mMinSpeech->value());
  t.maxPhraseSec = static_cast<float>(mMaxPhrase->value());
  t.numThreads = mThreads->value();
  return t.clamped();
}

void TranscriptionSettingsPanel::setEnabledState(bool enabled) {
  const QSignalBlocker block(mEnabled);
  mEnabled->setChecked(enabled);
}
void TranscriptionSettingsPanel::setAutoStartState(bool transcription, bool prompter) {
  const QSignalBlocker a(mAutoStart), b(mPrompterAutoStart);
  mAutoStart->setChecked(transcription);
  mPrompterAutoStart->setChecked(prompter && transcription);
}
void TranscriptionSettingsPanel::setPrompterModel(const QString &model) { mPrompterModel->setText(model); }
void TranscriptionSettingsPanel::setSummaryLimits(int leaf, int overview) {
  const QSignalBlocker a(mSummaryLeaf), b(mSummaryOverview);
  mSummaryLeaf->setValue(qBound(100, leaf, pcm::prompter::SummaryLimits::kLeafMax));
  mSummaryOverview->setValue(qBound(500, overview, pcm::prompter::SummaryLimits::kOverviewMax));
}
void TranscriptionSettingsPanel::setSummaryMode(bool partsOnly) {
  const QSignalBlocker blocker(mSummaryMode);
  mSummaryMode->setCurrentIndex(partsOnly ? 1 : 0);
}
void TranscriptionSettingsPanel::setPrompterAcceleration(const QString &mode) {
  const QSignalBlocker blocker(mPrompterAcceleration);
  mPrompterAcceleration->setCurrentIndex(std::max(0, mPrompterAcceleration->findData(mode)));
}
void TranscriptionSettingsPanel::setModelInfo(const QString &id, bool installed) {
  mModel->setText(tr("Model: %1 — %2").arg(id, installed ? tr("installed") : tr("not installed")));
}
void TranscriptionSettingsPanel::setTranscriptCount(int count) {
  mTranscriptCount = count;
  mCount->setText(tr("%n transcripts stored", nullptr, count));
  mDelete->setEnabled(mDeleteAllowed && count > 0);
}
void TranscriptionSettingsPanel::setDeleteAllAllowed(bool allowed, const QString &reason) {
  mDeleteAllowed = allowed;
  mDelete->setToolTip(reason);
  mDelete->setEnabled(allowed && mTranscriptCount > 0);
}
