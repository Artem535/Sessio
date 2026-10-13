#include "transcription_settings_panel.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
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
  auto runtimeChanged = [this] {
    emit prompterRuntimeChanged(mPython->text(), mPrompterModel->text(), mPrompterTemplate->text());
  };
  auto field = [this, runtime, runtimeForm, runtimeChanged](const QString &title, const char *name) {
    auto *row = new QWidget(runtime);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    auto *edit = new QLineEdit(row); edit->setObjectName(name);
    auto *browse = new QPushButton(tr("Browse…"), row);
    rowLayout->addWidget(edit, 1); rowLayout->addWidget(browse);
    runtimeForm->addRow(title, row);
    connect(edit, &QLineEdit::editingFinished, this, runtimeChanged);
    connect(browse, &QPushButton::clicked, this, [this, edit, runtimeChanged] {
      const auto path = QFileDialog::getOpenFileName(this, tr("Select a local file"), edit->text());
      if (!path.isEmpty()) { edit->setText(path); runtimeChanged(); }
    });
    return edit;
  };
  mPython = field(tr("Python with LiteRT-LM installed"), "prompterPython");
  mPrompterModel = field(tr("Gemma model (.litertlm)"), "prompterModel");
  mPrompterTemplate = field(tr("Gemma chat template (.jinja)"), "prompterTemplate");
  auto *runtimeNote = new QLabel(tr("Choose an installed local runtime and model. Nothing is downloaded automatically. Prompter suggestions require your review."), runtime);
  runtimeNote->setWordWrap(true);
  runtimeForm->addRow(runtimeNote);
  layout->addWidget(runtime);
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
void TranscriptionSettingsPanel::setPrompterRuntime(const QString &python, const QString &model, const QString &promptTemplate) {
  mPython->setText(python); mPrompterModel->setText(model); mPrompterTemplate->setText(promptTemplate);
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
