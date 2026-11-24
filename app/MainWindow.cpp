// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSysInfo>
#include <QVBoxLayout>

#include <string>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <dxgi.h>
#endif

namespace {

// OBS mode: uxplay hands its decoded frames to the "Kagami Cast" OBS source
// (obs-plugin/) through GStreamer's d3d11ipcsink on this named pipe. The plugin
// shows them without a copy only when they come from the graphics adapter OBS
// renders on; it writes that adapter's LUID to obs-adapter.txt in our data folder.
const QString kObsPipe = QStringLiteral("\\\\.\\pipe\\kagami-cast");

// The name reaches uxplay.exe through the ANSI command line, which is not UTF-8 on
// Windows: a Thai name makes uxplay exit ("invalid (non-UTF-8/ascii) server name")
// on Thai-locale Windows and turns into "????" elsewhere. Allow printable ASCII only.
const QRegularExpression kNameRe(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9 ._()'-]{0,39}$"));

const qint64 kMaxLogBytes = 1024 * 1024;   // rotate session.log above 1 MB

const char *kToneColor[] = {"#888888", "#e0a44a", "#4fae5a", "#e05260"};

} // namespace

MainWindow::MainWindow(const QString &dataDir, QWidget *parent)
    : QMainWindow(parent), m_dataDir(dataDir) {
    setWindowTitle(QStringLiteral(APP_DISPLAY_NAME));
    resize(460, 690);

    m_settings = new QSettings(m_dataDir + QStringLiteral("/settings.ini"), QSettings::IniFormat, this);
    openLogFile();

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::started, this, &MainWindow::onProcessStarted);
    connect(m_proc, &QProcess::errorOccurred, this, &MainWindow::onProcessError);
    connect(m_proc, &QProcess::finished, this, &MainWindow::onProcessFinished);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &MainWindow::onProcessOutput);
    connect(m_proc, &QProcess::stateChanged, this, &MainWindow::onProcessStateChanged);

#ifdef Q_OS_WIN
    // Put uxplay in a job object that is killed when our last handle closes, so the
    // engine cannot outlive this window (crash, Task Manager) and keep its ports.
    if (HANDLE job = CreateJobObjectW(nullptr, nullptr)) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info)))
            m_job = job;
        else
            CloseHandle(job);
    }
#endif

    buildUi();
    loadSettings();
    setStatus(QStringLiteral("หยุดอยู่"), Tone::Idle);
}

MainWindow::~MainWindow() {
#ifdef Q_OS_WIN
    if (m_job)
        CloseHandle(static_cast<HANDLE>(m_job));
#endif
}

// ---------------------------------------------------------------- UI

void MainWindow::buildUi() {
    auto *central = new QWidget(this);
    central->setStyleSheet(QStringLiteral("background:#1e1f26;"));
    auto *col = new QVBoxLayout(central);
    col->setContentsMargins(20, 18, 20, 18);
    col->setSpacing(12);

    auto *title = new QLabel(QStringLiteral(APP_DISPLAY_NAME));
    title->setStyleSheet(QStringLiteral("color:#f4f4f6; font-size:20px; font-weight:600;"));
    col->addWidget(title);

    // status row
    auto *statusRow = new QHBoxLayout;
    m_statusDot = new QLabel;
    m_statusDot->setFixedSize(12, 12);
    m_statusText = new QLabel;
    m_statusText->setStyleSheet(QStringLiteral("color:#c9c9d1; font-size:13px;"));
    m_statusText->setWordWrap(true);
    statusRow->addWidget(m_statusDot);
    statusRow->addWidget(m_statusText, 1);
    col->addLayout(statusRow);

    // PIN shown large while an iPhone is pairing (only with "require PIN")
    m_pinLabel = new QLabel;
    m_pinLabel->setAlignment(Qt::AlignCenter);
    m_pinLabel->setStyleSheet(QStringLiteral(
        "color:#f4f4f6; background:#2a2b34; border:1px solid #3d7eff; border-radius:8px;"
        "font-size:30px; font-weight:700; padding:10px;"));
    m_pinLabel->hide();
    col->addWidget(m_pinLabel);

    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QStringLiteral("color:#33343d;"));
    col->addWidget(line);

    const QString fieldLabelCss = QStringLiteral("color:#9a9aa6; font-size:12px;");
    const QString hintCss = QStringLiteral("color:#6b6b76; font-size:10px;");
    const QString inputCss = QStringLiteral(
        "background:#2a2b34; color:#f0f0f4; border:1px solid #3a3b45; border-radius:6px; padding:8px;");

    // device name
    auto *nameLbl = new QLabel(QStringLiteral("ชื่อเครื่อง (ที่ iPhone เห็น)"));
    nameLbl->setStyleSheet(fieldLabelCss);
    col->addWidget(nameLbl);
    m_nameEdit = new QLineEdit;
    m_nameEdit->setMaxLength(40);
    m_nameEdit->setValidator(new QRegularExpressionValidator(kNameRe, m_nameEdit));
    m_nameEdit->setStyleSheet(QStringLiteral("QLineEdit{%1}").arg(inputCss));
    connect(m_nameEdit, &QLineEdit::textChanged, this, &MainWindow::updateStartEnabled);
    col->addWidget(m_nameEdit);
    auto *nameHint = new QLabel(QStringLiteral(
        "ใช้ได้เฉพาะ A–Z, a–z, 0–9, เว้นวรรค และ - _ . ( ) ' ไม่เกิน 40 ตัว (ภาษาไทยใช้ไม่ได้)"));
    nameHint->setStyleSheet(hintCss);
    nameHint->setWordWrap(true);
    col->addWidget(nameHint);

    // preset (itemData = extra uxplay arguments, space separated)
    auto *presetLbl = new QLabel(QStringLiteral("โหมดภาพ/เสียง"));
    presetLbl->setStyleSheet(fieldLabelCss);
    col->addWidget(presetLbl);
    m_presetCombo = new QComboBox;
    // หมายเหตุ: ปล่อยให้ decodebin (ค่า default) เลือก hardware decoder เองเพื่อ
    // เลี่ยงปัญหา caps negotiation; เราคุม fps / sink / audio ซึ่งปลอดภัยและได้ผลจริง
    m_presetCombo->addItem(QStringLiteral("สมดุล (แนะนำ) — 1080p60, เร่ง GPU"),
        QStringLiteral("-s 1920x1080@60 -fps 60 -vs d3d11videosink"));
    m_presetCombo->addItem(QStringLiteral("หน่วงต่ำสุด — สำหรับเกม/ตอบสนองไว"),
        QStringLiteral("-s 1920x1080@60 -fps 60 -vs d3d11videosink -vsync no -al 0.1"));
    m_presetCombo->addItem(QStringLiteral("คมชัดสุด / 4K — 2160p60 (H.265)"),
        QStringLiteral("-h265 -s 3840x2160@60 -fps 60 -vs d3d11videosink"));
    m_presetCombo->addItem(QStringLiteral("เข้ากันได้สูงสุด — auto (ถ้ามีปัญหา)"),
        QStringLiteral("-s 1920x1080@60 -fps 60"));
    m_presetCombo->setStyleSheet(QStringLiteral("QComboBox{%1}").arg(inputCss));
    col->addWidget(m_presetCombo);
    auto *presetHint = new QLabel(QStringLiteral(
        "ทุกโหมดใช้ 60 FPS (ค่าเดิม uxplay = 30) · 3 โหมดแรกเร่งด้วย GPU (Direct3D 11) · "
        "ถ้าภาพไม่ขึ้น ให้ลองโหมด \"เข้ากันได้สูงสุด\""));
    presetHint->setStyleSheet(hintCss);
    presetHint->setWordWrap(true);
    col->addWidget(presetHint);

    const QString checkCss = QStringLiteral("color:#c9c9d1; font-size:13px;");
    m_obsCheck = new QCheckBox(QStringLiteral("ส่งภาพเข้า OBS (ไม่เปิดหน้าต่างภาพ)"));
    m_obsCheck->setStyleSheet(checkCss);
    col->addWidget(m_obsCheck);
    auto *obsHint = new QLabel(QStringLiteral(
        "ต้องติดตั้งปลั๊กอิน Kagami Cast for OBS แล้วเพิ่ม Source \"Kagami Cast (iPhone / iPad)\" ใน OBS · "
        "เสียงยังออกลำโพงตามปกติ — ถ้าจะอัดเสียงด้วย ใช้ Application Audio Capture เลือก uxplay.exe"));
    obsHint->setStyleSheet(hintCss);
    obsHint->setWordWrap(true);
    col->addWidget(obsHint);

    // access control
    m_pinCheck = new QCheckBox(QStringLiteral("ต้องใส่ PIN เมื่อ iPhone เครื่องใหม่เชื่อมต่อครั้งแรก"));
    m_pinCheck->setStyleSheet(checkCss);
    col->addWidget(m_pinCheck);
    auto *pinHint = new QLabel(QStringLiteral(
        "แนะนำเมื่อใช้ Wi-Fi ร่วมกับคนอื่น (ออฟฟิศ ร้านกาแฟ) เพื่อกันคนอื่นส่งภาพขึ้นจอ — "
        "PIN จะขึ้นในหน้าต่างนี้ เครื่องที่จับคู่แล้วไม่ต้องใส่ซ้ำ"));
    pinHint->setStyleSheet(hintCss);
    pinHint->setWordWrap(true);
    col->addWidget(pinHint);

    m_onTop = new QCheckBox(QStringLiteral("ปักหมุดหน้าต่างนี้ไว้บนสุด"));
    m_onTop->setStyleSheet(checkCss);
    connect(m_onTop, &QCheckBox::toggled, this, &MainWindow::onToggleOnTop);
    col->addWidget(m_onTop);

    // buttons
    auto *btnRow = new QHBoxLayout;
    m_startBtn = new QPushButton(QStringLiteral("เริ่มรับสัญญาณ"));
    m_startBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#3d7eff; color:white; border:none; border-radius:8px;"
        "padding:11px; font-size:14px; font-weight:600;}"
        "QPushButton:hover{background:#4f8bff;}"
        "QPushButton:disabled{background:#33343d; color:#777;}"));
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStart);
    m_stopBtn = new QPushButton(QStringLiteral("หยุด"));
    m_stopBtn->setEnabled(false);
    m_stopBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:#2a2b34; color:#f0f0f4; border:1px solid #3a3b45;"
        "border-radius:8px; padding:11px; font-size:14px;}"
        "QPushButton:hover{background:#343541;}"
        "QPushButton:disabled{color:#666; border-color:#2a2b30;}"));
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);
    btnRow->addWidget(m_startBtn, 2);
    btnRow->addWidget(m_stopBtn, 1);
    col->addLayout(btnRow);

    // log
    auto *logLbl = new QLabel(QStringLiteral("บันทึกการเชื่อมต่อ"));
    logLbl->setStyleSheet(fieldLabelCss);
    logLbl->setToolTip(QStringLiteral("ไฟล์ log: ") +
                       QDir::toNativeSeparators(m_dataDir + QStringLiteral("/session.log")));
    col->addWidget(logLbl);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(1000);
    m_log->setStyleSheet(QStringLiteral(
        "QPlainTextEdit{background:#16171c; color:#8fae8f; border:1px solid #2a2b30;"
        "border-radius:6px; font-family:Consolas,monospace; font-size:11px; padding:6px;}"));
    col->addWidget(m_log, 1);

    setCentralWidget(central);
}

void MainWindow::loadSettings() {
    QString name = m_settings->value(QStringLiteral("name")).toString();
    if (!kNameRe.match(name).hasMatch())
        name = defaultServerName();
    m_nameEdit->setText(name);

    const int preset = m_settings->value(QStringLiteral("preset"), 0).toInt();
    if (preset >= 0 && preset < m_presetCombo->count())
        m_presetCombo->setCurrentIndex(preset);

    m_pinCheck->setChecked(m_settings->value(QStringLiteral("requirePin"), false).toBool());
    m_obsCheck->setChecked(m_settings->value(QStringLiteral("obsMode"), false).toBool());
    m_onTop->setChecked(m_settings->value(QStringLiteral("alwaysOnTop"), false).toBool());
    updateStartEnabled();
}

void MainWindow::saveSettings() {
    if (m_nameEdit->hasAcceptableInput())
        m_settings->setValue(QStringLiteral("name"), serverName());
    m_settings->setValue(QStringLiteral("preset"), m_presetCombo->currentIndex());
    m_settings->setValue(QStringLiteral("requirePin"), m_pinCheck->isChecked());
    m_settings->setValue(QStringLiteral("obsMode"), m_obsCheck->isChecked());
    m_settings->setValue(QStringLiteral("alwaysOnTop"), m_onTop->isChecked());
    m_settings->sync();
}

void MainWindow::updateStartEnabled() {
    const bool idle = m_proc->state() == QProcess::NotRunning;
    m_startBtn->setEnabled(idle && m_nameEdit->hasAcceptableInput());
}

void MainWindow::onToggleOnTop(bool on) {
    Qt::WindowFlags f = windowFlags();
    if (on) f |= Qt::WindowStaysOnTopHint;
    else    f &= ~Qt::WindowStaysOnTopHint;
    setWindowFlags(f);
    show();   // re-apply flags
}

void MainWindow::setStatus(const QString &text, Tone tone) {
    m_statusText->setText(text);
    m_statusDot->setStyleSheet(QStringLiteral("background:%1; border-radius:6px;")
                                   .arg(QString::fromLatin1(kToneColor[static_cast<int>(tone)])));
}

void MainWindow::showPin(const QString &pin) {
    if (pin.isEmpty()) {
        m_pinLabel->hide();
        return;
    }
    QStringList digits;
    for (const QChar c : pin)
        digits << QString(c);
    m_pinLabel->setText(QStringLiteral("PIN  ") + digits.join(QLatin1Char(' ')));
    m_pinLabel->show();
}

QString MainWindow::readyText() const {
    return QStringLiteral("พร้อมรับ — บน iPhone เปิด Screen Mirroring แล้วเลือก \"%1\"\n"
                          "ถ้า iPhone ไม่เห็นชื่อนี้: ให้อยู่ Wi-Fi วงเดียวกัน และตั้ง Wi-Fi ของเครื่องนี้เป็น Private")
        .arg(serverName());
}

// ---------------------------------------------------------------- engine setup

QString MainWindow::serverName() const {
    return m_nameEdit->text().trimmed();
}

QString MainWindow::defaultServerName() {
    QString host = QSysInfo::machineHostName();
    host.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 ._()'-]")), QStringLiteral("-"));
    host = host.left(40);
    if (kNameRe.match(host).hasMatch())
        return host;
    return QStringLiteral("Windows-PC");
}

QString MainWindow::uxplayExe() const {
    // UXPLAY_EXE overrides for development (see tools/dev-run-gui.bat); a packaged copy
    // always has uxplay.exe next to this executable.
    const QString fromEnv = qEnvironmentVariable("UXPLAY_EXE");
    if (!fromEnv.isEmpty())
        return fromEnv;
    return QCoreApplication::applicationDirPath() + QStringLiteral("/uxplay.exe");
}

// uxplay receives argv in the ANSI code page, so a path it must open has to be
// plain ASCII. Returns the path (or its 8.3 short form) if it is, otherwise "".
QString MainWindow::asciiSafePath(const QString &path) {
    const auto isAscii = [](const QString &s) {
        for (const QChar c : s) {
            if (c.unicode() < 0x20 || c.unicode() > 0x7E)
                return false;
        }
        return true;
    };
    const QString native = QDir::toNativeSeparators(path);
    if (isAscii(native))
        return native;
#ifdef Q_OS_WIN
    const std::wstring wide = native.toStdWString();
    wchar_t buf[MAX_PATH];
    const DWORD n = GetShortPathNameW(wide.c_str(), buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        const QString shortPath = QString::fromWCharArray(buf, static_cast<int>(n));
        if (isAscii(shortPath))
            return shortPath;
    }
#endif
    return QString();
}

QStringList MainWindow::engineArguments() const {
    QStringList args;
    // -nh: advertise exactly the typed name (uxplay appends "@<computer>" otherwise)
    args << QStringLiteral("-n") << serverName() << QStringLiteral("-nh");
    QStringList preset = m_presetCombo->currentData().toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (m_obsCheck->isChecked()) {
        // The preset's window sink is replaced by the OBS sink below.
        const qsizetype vs = preset.indexOf(QStringLiteral("-vs"));
        if (vs >= 0)
            preset.remove(vs, qMin<qsizetype>(2, preset.size() - vs));
    }
    args += preset;
    if (m_obsCheck->isChecked()) {
        // uxplay builds "<decoder> ! <-vc> ! videoscale ! <-vs> name=... sync=...".
        // The decoders hand over D3D11 textures and videoscale passes those through,
        // so the picture stays on the GPU; d3d11convert makes it the BGRA the plugin
        // draws directly. gst_parse_launch unescapes backslashes, hence the doubling.
        const int adapter = obsAdapter(nullptr);
        const QString onAdapter = adapter >= 0 ? QStringLiteral(" adapter=%1").arg(adapter) : QString();
        QString pipe = kObsPipe;
        pipe.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        args << QStringLiteral("-vc")
             << QStringLiteral("d3d11convert%1 ! video/x-raw(memory:D3D11Memory),format=BGRA").arg(onAdapter)
             << QStringLiteral("-vs")
             << QStringLiteral("d3d11ipcsink pipe-name=%1%2").arg(pipe, onAdapter);
    }
    if (m_pinCheck->isChecked()) {
        // -pin: a device must enter the onscreen PIN the first time it connects.
        // -key: random server key kept in our data folder. Without it uxplay derives
        //       the key from the MAC address, which anyone on the network can compute.
        // -reg: register of devices that entered the PIN. Without it uxplay trusts any
        //       client that claims to be "returning" and skips the PIN entirely.
        args << QStringLiteral("-pin");
        const QString dir = asciiSafePath(m_dataDir);
        if (!dir.isEmpty()) {
            args << QStringLiteral("-key") << dir + QStringLiteral("\\uxplay.pem")
                 << QStringLiteral("-reg") << dir + QStringLiteral("\\uxplay.register");
        }
    }
    return args;
}

QProcessEnvironment MainWindow::engineEnvironment() const {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString pluginDir = appDir + QStringLiteral("/lib/gstreamer-1.0");
    if (QFileInfo(pluginDir).isDir()) {
        // Packaged layout: use only the bundled runtime, never a GStreamer that
        // happens to be installed elsewhere on the machine.
        env.insert(QStringLiteral("PATH"),
                   QDir::toNativeSeparators(appDir) + QLatin1Char(';') + env.value(QStringLiteral("PATH")));
        env.insert(QStringLiteral("GST_PLUGIN_SYSTEM_PATH_1_0"), QDir::toNativeSeparators(pluginDir));
        env.insert(QStringLiteral("GST_PLUGIN_PATH"), QDir::toNativeSeparators(pluginDir));
        const QString scanner = appDir + QStringLiteral("/libexec/gstreamer-1.0/gst-plugin-scanner.exe");
        if (QFileInfo::exists(scanner))
            env.insert(QStringLiteral("GST_PLUGIN_SCANNER"), QDir::toNativeSeparators(scanner));
        // private plugin cache, so another GStreamer version cannot clash with it
        env.insert(QStringLiteral("GST_REGISTRY"),
                   QDir::toNativeSeparators(m_dataDir + QStringLiteral("/gst-registry.bin")));
    }
    // Development layout (no bundled plugins): the caller's PATH must already
    // contain the MSYS2 UCRT64 runtime — see tools/dev-run-gui.bat.
    return env;
}

// Index, in DXGI enumeration order (what d3d11ipcsink's "adapter" means), of the
// adapter the OBS plugin last reported. Matched by LUID: a GPU can appear twice
// with the same name.
int MainWindow::obsAdapter(QString *description) const {
    QFile f(m_dataDir + QStringLiteral("/obs-adapter.txt"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;
    qint64 luid = 0;
    bool ok = false;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(QLatin1String("adapter_luid=")))
            luid = line.mid(13).toLongLong(&ok);
    }
    if (!ok)
        return -1;
    int found = -1;
#ifdef Q_OS_WIN
    IDXGIFactory1 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory))))
        return -1;
    IDXGIAdapter1 *adapter = nullptr;
    for (UINT i = 0; found < 0 && factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            ((qint64(desc.AdapterLuid.HighPart) << 32) | desc.AdapterLuid.LowPart) == luid) {
            found = int(i);
            if (description)
                *description = QString::fromWCharArray(desc.Description);
        }
        adapter->Release();
    }
    factory->Release();
#else
    Q_UNUSED(description);
#endif
    return found;
}

// ---------------------------------------------------------------- start / stop

void MainWindow::onStart() {
    if (m_proc->state() != QProcess::NotRunning)
        return;
    if (!m_nameEdit->hasAcceptableInput()) {
        setStatus(QStringLiteral("ชื่อเครื่องไม่ถูกต้อง"), Tone::Bad);
        return;
    }
    const QString exe = uxplayExe();
    if (!QFileInfo::exists(exe)) {
        appendLog(QStringLiteral("!! ไม่พบไฟล์ ") + QDir::toNativeSeparators(exe));
        setStatus(QStringLiteral("ไม่พบ uxplay.exe — ไฟล์โปรแกรมไม่ครบ ลองติดตั้งใหม่"), Tone::Bad);
        return;
    }
    saveSettings();

    m_stopRequested = false;
    m_ready = false;
    m_client.clear();
    m_lastError.clear();
    m_lastErrorLine.clear();
    m_sockets.clear();
    showPin(QString());

    if (m_pinCheck->isChecked() && asciiSafePath(m_dataDir).isEmpty()) {
        appendLog(QStringLiteral(
            "(คำเตือน: path ของโฟลเดอร์ข้อมูลมีอักษรที่ไม่ใช่ภาษาอังกฤษ จึงเก็บ key และรายชื่อเครื่อง"
            "ที่จับคู่แล้วไม่ได้ — PIN ยังใช้ได้ แต่ป้องกันได้น้อยลง)"));
    }
    if (m_obsCheck->isChecked()) {
        QString gpu;
        if (obsAdapter(&gpu) >= 0)
            appendLog(QStringLiteral("โหมด OBS: ส่งภาพจากการ์ดจอเดียวกับ OBS (%1) — ภาพจะขึ้นที่ Source "
                                     "\"Kagami Cast\" ใน OBS").arg(gpu));
        else
            appendLog(QStringLiteral("โหมด OBS: ยังไม่รู้ว่า OBS ใช้การ์ดจอใบไหน จึงใช้การ์ดหลักไปก่อน — "
                                     "เปิด OBS ที่มี Source \"Kagami Cast\" แล้วกดหยุด/เริ่มใหม่ ถ้า OBS "
                                     "แจ้งว่ารับภาพผ่าน CPU"));
    }
    const QStringList args = engineArguments();
    appendLog(QStringLiteral("$ uxplay %1").arg(args.join(QLatin1Char(' '))));
    // Set before start(): on Windows a launch failure emits errorOccurred from
    // inside start(), and its message must not be overwritten afterwards.
    setStatus(QStringLiteral("กำลังเริ่ม… (ครั้งแรกอาจใช้เวลาสักครู่)"), Tone::Busy);
    m_proc->setProcessEnvironment(engineEnvironment());
    m_proc->start(exe, args);   // see onProcessStarted / onProcessError / onProcessFinished
}

void MainWindow::onStop() {
    if (m_proc->state() == QProcess::NotRunning)
        return;
    appendLog(QStringLiteral("-- กำลังหยุด --"));
    m_stopRequested = true;
    // uxplay on Windows only reacts to console Ctrl events, not to the WM_CLOSE
    // that QProcess::terminate() sends, so terminate() would just stall the UI
    // until a timeout. kill() is immediate; Bonjour withdraws the advertisement
    // when the process's connection to the mDNS service closes.
    m_proc->kill();
}

void MainWindow::onProcessStarted() {
    attachToJob();
}

void MainWindow::attachToJob() {
#ifdef Q_OS_WIN
    if (!m_job)
        return;
    HANDLE proc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                              static_cast<DWORD>(m_proc->processId()));
    if (!proc)
        return;
    if (!AssignProcessToJobObject(static_cast<HANDLE>(m_job), proc))
        appendLog(QStringLiteral("(คำเตือน: ผูก uxplay กับหน้าต่างนี้ไม่สำเร็จ — ถ้าโปรแกรมปิดผิดปกติ uxplay อาจค้างอยู่)"));
    CloseHandle(proc);
#endif
}

void MainWindow::onProcessError(QProcess::ProcessError error) {
    // Crashes and kills are reported through onProcessFinished; only a failed
    // launch (which emits no finished signal) is handled here.
    if (error != QProcess::FailedToStart)
        return;
    appendLog(QStringLiteral("!! เปิด uxplay ไม่สำเร็จ: ") + m_proc->errorString());
    setStatus(QStringLiteral("เปิด uxplay ไม่สำเร็จ — ดูรายละเอียดใน log ด้านล่าง"), Tone::Bad);
}

void MainWindow::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    // a last line without a trailing newline is still in the buffer
    const QByteArray rest = m_proc->readAll();
    if (!rest.isEmpty()) {
        for (const QByteArray &part : rest.split('\n'))
            handleLine(QString::fromUtf8(part));
    }

    showPin(QString());
    m_ready = false;
    m_client.clear();
    m_sockets.clear();

    if (m_stopRequested) {
        setStatus(QStringLiteral("หยุดอยู่"), Tone::Idle);
        return;
    }
    // uxplay ends every fatal path with exit(0) (cleanup() in uxplay.cpp), so the
    // exit code says nothing; the error lines it printed are the real reason.
    QString why = m_lastError;
    if (why.isEmpty() && !m_lastErrorLine.isEmpty())
        why = QStringLiteral("uxplay หยุดทำงาน: ") + m_lastErrorLine;
    if (why.isEmpty()) {
        why = (exitStatus == QProcess::CrashExit)
            ? QStringLiteral("uxplay หยุดทำงานผิดปกติ")
            : QStringLiteral("uxplay ปิดตัวเอง (รหัส %1)").arg(exitCode);
    }
    appendLog(QStringLiteral("!! ") + why);
    setStatus(why + QStringLiteral(" — ดูรายละเอียดใน log ด้านล่าง"), Tone::Bad);
}

void MainWindow::onProcessStateChanged(QProcess::ProcessState state) {
    const bool idle = (state == QProcess::NotRunning);
    m_stopBtn->setEnabled(!idle);
    m_nameEdit->setEnabled(idle);
    m_presetCombo->setEnabled(idle);
    m_pinCheck->setEnabled(idle);
    m_obsCheck->setEnabled(idle);
    updateStartEnabled();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveSettings();
    if (m_proc->state() != QProcess::NotRunning) {
        m_stopRequested = true;
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
    event->accept();
}

// ---------------------------------------------------------------- engine output

void MainWindow::onProcessOutput() {
    // Consume complete lines only; a partial line stays buffered until its
    // newline arrives, so a line split across two pipe reads is not cut in half.
    while (m_proc->canReadLine())
        handleLine(QString::fromUtf8(m_proc->readLine()));
}

void MainWindow::handleLine(const QString &raw) {
    QString line = raw;
    while (!line.isEmpty() && line.back().isSpace())
        line.chop(1);   // keep leading spaces: uxplay draws the PIN as ASCII art
    if (line.trimmed().isEmpty())
        return;

    // Informational lines that only alarm users ("Unhandled ..." although nothing
    // is wrong): kept in session.log for diagnosis, not shown in the window.
    // uxplay logs this one each time iOS switches its media audio mode.
    static const QString kHiddenOnScreen[] = {
        QStringLiteral("Unhandled RTSP request \"audioMode:"),
    };
    for (const QString &noise : kHiddenOnScreen) {
        if (line.contains(noise)) {
            writeLogFile(line);
            return;
        }
    }
    appendLog(line);
    parseLine(line.trimmed());
}

// The session state is derived from uxplay's own log lines (unmodified UxPlay
// prints all of them), so the GUI does not depend on patches to the engine.
void MainWindow::parseLine(const QString &line) {
    static const QRegularExpression reAccepted(QStringLiteral("Accepted IPv[46] client on socket (\\d+)"));
    static const QRegularExpression reClosed(QStringLiteral("Connection closed on socket (\\d+)"));
    static const QRegularExpression reRequest(QStringLiteral("connection request from (.+?) \\("));
    static const QRegularExpression rePin(QStringLiteral("CLIENT MUST NOW ENTER PIN = \"(\\d+)\""));

    // --- fatal start-up problems: remembered, shown when uxplay exits
    if (line.contains(QLatin1String("Could not initialize dnssd library"))) {
        m_lastError = QStringLiteral("ยังไม่ได้ติดตั้ง Apple Bonjour (รัน INSTALL-Bonjour.bat)");
        return;
    }
    if (line.contains(QLatin1String("No DNS-SD Server found"))) {
        m_lastError = QStringLiteral("Bonjour Service ไม่ทำงาน — ลองรีสตาร์ทเครื่อง");
        return;
    }
    if (line.contains(QLatin1String("kDNSServiceErr_NameConflict"))) {
        m_lastError = QStringLiteral("ชื่อนี้ถูกใช้อยู่ หรือมี uxplay อีกตัวเปิดอยู่ — ลองเปลี่ยนชื่อเครื่อง");
        return;
    }
    if (line.startsWith(QLatin1String("invalid")) || line.startsWith(QLatin1String("unknown option"))) {
        m_lastError = QStringLiteral("ค่าที่ส่งให้ uxplay ไม่ถูกต้อง");
        return;
    }
    // any other error-level line (uxplay prefixes them with "*** ERROR: ") is kept
    // as the fallback reason in case uxplay exits; parsing continues below
    static const QString kErrorPrefix = QStringLiteral("*** ERROR: ");
    if (line.startsWith(kErrorPrefix))
        m_lastErrorLine = line.mid(kErrorPrefix.size()).trimmed();

    // --- normal session flow
    if (line.contains(QLatin1String("Initialized server socket"))) {
        m_ready = true;
        setStatus(readyText(), Tone::Busy);
        return;
    }
    QRegularExpressionMatch m = reAccepted.match(line);
    if (m.hasMatch()) {
        m_sockets.insert(m.captured(1).toInt());
        return;
    }
    m = reClosed.match(line);
    if (m.hasMatch()) {
        m_sockets.remove(m.captured(1).toInt());
        if (m_sockets.isEmpty()) {
            m_client.clear();
            showPin(QString());
            if (m_ready)
                setStatus(readyText(), Tone::Busy);
        }
        return;
    }
    m = rePin.match(line);
    if (m.hasMatch()) {
        showPin(m.captured(1));
        setStatus(QStringLiteral("ใส่ PIN ด้านล่างนี้บน iPhone"), Tone::Busy);
        return;
    }
    m = reRequest.match(line);
    if (m.hasMatch()) {
        m_client = m.captured(1);
        setStatus(QStringLiteral("เชื่อมต่อแล้ว: ") + m_client, Tone::Good);
        return;
    }
    if (line.contains(QLatin1String("Begin streaming to GStreamer video pipeline"))) {
        showPin(QString());
        setStatus(QStringLiteral("กำลังแสดงภาพจาก ") +
                      (m_client.isEmpty() ? QStringLiteral("iPhone") : m_client),
                  Tone::Good);
        return;
    }
    if (line.contains(QLatin1String("raop_rtp_mirror->running is no longer true"))) {
        if (!m_sockets.isEmpty() && !m_client.isEmpty())
            setStatus(QStringLiteral("เชื่อมต่อแล้ว: ") + m_client, Tone::Good);
        else if (m_ready)
            setStatus(readyText(), Tone::Busy);
        return;
    }
}

// ---------------------------------------------------------------- log

void MainWindow::openLogFile() {
    const QString path = m_dataDir + QStringLiteral("/session.log");
    const QFileInfo info(path);
    if (info.exists() && info.size() > kMaxLogBytes) {
        const QString old = m_dataDir + QStringLiteral("/session.old.log");
        QFile::remove(old);
        QFile::rename(path, old);
    }
    m_logFile.setFileName(path);
    if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_logFile.write(QStringLiteral("\n=== " APP_DISPLAY_NAME " %1 started %2 ===\n")
                            .arg(QStringLiteral(APP_VERSION),
                                 QDateTime::currentDateTime().toString(Qt::ISODate))
                            .toUtf8());
        m_logFile.flush();
    }
}

void MainWindow::appendLog(const QString &line) {
    m_log->appendPlainText(line);
    writeLogFile(line);
}

void MainWindow::writeLogFile(const QString &line) {
    if (m_logFile.isOpen()) {
        m_logFile.write(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")).toUtf8());
        m_logFile.write(line.toUtf8());
        m_logFile.write("\n");
        m_logFile.flush();
    }
}
