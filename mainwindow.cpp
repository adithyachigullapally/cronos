#include "mainwindow.h"
#include "widgets.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStyledItemDelegate>
#include <QSystemTrayIcon>
#include <QTime>

#include <algorithm>
#include <cmath>
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Values stored in the action combo boxes. Anything positive is a virtual-key code.
enum { NoAction = 0, KeyShortcut = -1, RunCommand = -2, OpenApp = -3, CloseFront = -4 };
enum { Left, Right };

static const char *const patternNames[] = {QT_TR_NOOP("Single tap"), QT_TR_NOOP("Double tap"),
                                           QT_TR_NOOP("Triple tap")};
static const char *const sideNames[] = {QT_TR_NOOP("Left side"), QT_TR_NOOP("Right side")};

// Programs listed here are started by Windows when the user signs in.
static const char *const runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const char *const runName = "Cronos";

// A second launch posts this to every window; the copy already running shows itself.
static const UINT showMessage = RegisterWindowMessageW(L"cronos-show");

bool MainWindow::alreadyRunning()
{
    CreateMutexW(nullptr, FALSE, L"cronos-running"); // held until this process ends
    if (GetLastError() != ERROR_ALREADY_EXISTS)
        return false;
    AllowSetForegroundWindow(ASFW_ANY); // or Windows would only flash that copy's taskbar button
    PostMessageW(HWND_BROADCAST, showMessage, 0, 0);
    return true;
}

// Dropdown whose list has rounded rows and, on Windows 11, rounded corners.
class Combo : public QComboBox
{
public:
    Combo() { setItemDelegate(new QStyledItemDelegate(this)); } // the default one ignores the style sheet's row rules
    void showPopup() override
    {
        const int roundSmall = 3; // DWMWCP_ROUNDSMALL; older Windows ignores the request
        DwmSetWindowAttribute(HWND(view()->window()->winId()), 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &roundSmall,
                              sizeof(roundSmall));
        QComboBox::showPopup();
    }
};

// ponytail: virtual-key codes only. Games that read scan codes won't see these;
// fill in ki.wScan via MapVirtualKey if that ever matters.
static void sendKeys(const QList<WORD> &keys) // pressed in order, released in reverse
{
    QList<INPUT> inputs;
    for (WORD vk : keys) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        inputs.append(in);
    }
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = *it;
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.append(in);
    }
    if (!inputs.isEmpty())
        SendInput(UINT(inputs.size()), inputs.data(), sizeof(INPUT));
}

static QList<WORD> toVirtualKeys(const QKeySequence &sequence)
{
    if (sequence.isEmpty())
        return {};
    const QKeyCombination combo = sequence[0];
    const int key = combo.key();
    WORD vk = 0;
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) {
        vk = WORD(key);
    } else if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        vk = WORD(VK_F1 + key - Qt::Key_F1);
    } else {
        switch (key) {
        case Qt::Key_Space: vk = VK_SPACE; break;
        case Qt::Key_Tab: vk = VK_TAB; break;
        case Qt::Key_Return:
        case Qt::Key_Enter: vk = VK_RETURN; break;
        case Qt::Key_Escape: vk = VK_ESCAPE; break;
        case Qt::Key_Backspace: vk = VK_BACK; break;
        case Qt::Key_Delete: vk = VK_DELETE; break;
        case Qt::Key_Insert: vk = VK_INSERT; break;
        case Qt::Key_Home: vk = VK_HOME; break;
        case Qt::Key_End: vk = VK_END; break;
        case Qt::Key_PageUp: vk = VK_PRIOR; break;
        case Qt::Key_PageDown: vk = VK_NEXT; break;
        case Qt::Key_Left: vk = VK_LEFT; break;
        case Qt::Key_Right: vk = VK_RIGHT; break;
        case Qt::Key_Up: vk = VK_UP; break;
        case Qt::Key_Down: vk = VK_DOWN; break;
        case Qt::Key_Print: vk = VK_SNAPSHOT; break;
        default:
            if (key < 0x100) { // punctuation: ask the keyboard layout
                const SHORT scan = VkKeyScanW(WCHAR(key));
                if (scan != -1)
                    vk = LOBYTE(scan);
            }
        }
    }
    if (!vk)
        return {};
    QList<WORD> keys;
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    if (mods & Qt::ControlModifier) keys << VK_CONTROL;
    if (mods & Qt::ShiftModifier) keys << VK_SHIFT;
    if (mods & Qt::AltModifier) keys << VK_MENU;
    if (mods & Qt::MetaModifier) keys << VK_LWIN;
    keys << vk;
    return keys;
}

struct App {
    QString name, target; // target: something ShellExecute can open
    QIcon icon;
};

// Everything under "All apps" in the Start menu, Store apps included: that
// list is the shell folder AppsFolder. Read once, on first use.
static const QList<App> &installedApps()
{
    static QList<App> apps;
    ComPtr<IShellItem> folder, item;
    ComPtr<IEnumShellItems> items;
    if (!apps.isEmpty() || FAILED(SHCreateItemFromParsingName(L"shell:AppsFolder", nullptr, IID_PPV_ARGS(&folder)))
        || FAILED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))))
        return apps;
    while (items->Next(1, item.ReleaseAndGetAddressOf(), nullptr) == S_OK) {
        LPWSTR name = nullptr, id = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name))
            && SUCCEEDED(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &id))) {
            App app{QString::fromWCharArray(name), QStringLiteral("shell:AppsFolder\\") + QString::fromWCharArray(id), {}};
            ComPtr<IShellItemImageFactory> image;
            HBITMAP bitmap = nullptr;
            if (SUCCEEDED(item.As(&image)) && SUCCEEDED(image->GetImage(SIZE{32, 32}, SIIGBF_ICONONLY, &bitmap))) {
                // Read the pixels directly: QImage::fromHBITMAP drops the alpha channel these carry.
                DIBSECTION dib{};
                if (GetObject(bitmap, sizeof(dib), &dib) == sizeof(dib) && dib.dsBm.bmBitsPixel == 32) {
                    const QImage pixels(static_cast<const uchar *>(dib.dsBm.bmBits), dib.dsBm.bmWidth, dib.dsBm.bmHeight,
                                        dib.dsBm.bmWidthBytes, QImage::Format_ARGB32_Premultiplied);
                    app.icon = QPixmap::fromImage(dib.dsBmih.biHeight > 0 ? pixels.flipped(Qt::Vertical) : pixels);
                }
                DeleteObject(bitmap);
            }
            apps.append(app);
        }
        CoTaskMemFree(name);
        CoTaskMemFree(id);
    }
    std::sort(apps.begin(), apps.end(),
              [](const App &a, const App &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    return apps;
}

// Searchable list of installed apps, or any file via "Other file". False if cancelled.
static bool chooseApp(QWidget *parent, QString *name, QString *target)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(MainWindow::tr("Choose an app"));
    dialog.resize(380, 480);
    auto *search = new QLineEdit;
    search->setPlaceholderText(MainWindow::tr("Search"));
    search->setClearButtonEnabled(true);
    auto *list = new QListWidget;
    list->setIconSize({24, 24});
    list->setUniformItemSizes(true);
    QApplication::setOverrideCursor(Qt::WaitCursor); // reading a couple of hundred icons takes a moment
    for (const App &app : installedApps())
        (new QListWidgetItem(app.icon, app.name, list))->setData(Qt::UserRole, app.target);
    QApplication::restoreOverrideCursor();
    list->setCurrentRow(0);
    auto *other = new QPushButton(MainWindow::tr("Other file..."));
    other->setAutoDefault(false);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(MainWindow::tr("Choose"));
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(other);
    bottom->addStretch();
    bottom->addWidget(buttons);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(search);
    layout->addWidget(list, 1);
    layout->addLayout(bottom);

    QObject::connect(search, &QLineEdit::textChanged, list, [list](const QString &text) {
        QListWidgetItem *first = nullptr;
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *item = list->item(i);
            item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
            if (!first && !item->isHidden())
                first = item;
        }
        list->setCurrentItem(first); // so Enter picks the top match
    });
    QString file;
    QObject::connect(other, &QPushButton::clicked, &dialog, [&] {
        file = QFileDialog::getOpenFileName(&dialog, MainWindow::tr("Choose a program or file"), QString(),
                                            MainWindow::tr("Programs (*.exe *.lnk *.bat *.cmd);;All files (*)"));
        if (!file.isEmpty())
            dialog.accept();
    });
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return false;
    if (!file.isEmpty()) {
        *name = QFileInfo(file).completeBaseName();
        *target = QDir::toNativeSeparators(file);
    } else if (QListWidgetItem *item = list->currentItem()) {
        *name = item->text();
        *target = item->data(Qt::UserRole).toString();
    } else {
        return false; // the search matched nothing
    }
    return true;
}

static QIcon appIcon() // window and tray: tap ripples on a blue tile
{
    QPixmap pixmap(256, 256); // drawn on a 64-unit grid; big enough for cronos.ico, which ui_test saves from this
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(4, 4);
    QLinearGradient blue(0, 0, 0, 64);
    blue.setColorAt(0, "#40a6ff");
    blue.setColorAt(1, "#0a6cf0");
    p.setPen(Qt::NoPen);
    p.setBrush(blue);
    p.drawRoundedRect(QRectF(2, 2, 60, 60), 15, 15);
    p.setBrush(Qt::white);
    p.drawEllipse(QPointF(32, 32), 7, 7);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 170), 3));
    p.drawEllipse(QPointF(32, 32), 14, 14);
    p.setPen(QPen(QColor(255, 255, 255, 85), 3));
    p.drawEllipse(QPointF(32, 32), 21.5, 21.5);
    return QIcon(pixmap);
}

static QString signedNumber(float v) // "+2.4", "-0.3"
{
    return (v > 0 ? QStringLiteral("+") : QString()) + QString::number(v, 'f', 1);
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Cronos"));
    const QIcon icon = appIcon();
    setWindowIcon(icon);

    winId(); // a real window from the start, even if never shown: nativeEvent() needs one to be reached

    // Header: icon, name, engine status, master switch
    auto *logo = new QLabel;
    logo->setPixmap(icon.pixmap(QSize(40, 40), qApp->devicePixelRatio()));
    auto *title = new QLabel(tr("Cronos"));
    title->setObjectName("title");
    auto *statusLabel = new QLabel;
    statusLabel->setObjectName("sub");
    statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // a long mic name must not widen the window
    auto showStatus = [statusLabel](const QString &text) { // with a dot: green listening, grey stopped, orange in trouble
        const char *dot = text.startsWith("Listening") ? "#30d158" : text == "Stopped" ? "#8e8e93" : "#ff9f0a";
        statusLabel->setText(QStringLiteral("<span style='color:%1'>●</span>&nbsp; %2").arg(QLatin1String(dot), text.toHtmlEscaped()));
    };
    showStatus(QStringLiteral("Stopped"));
    auto *names = new QVBoxLayout;
    names->setSpacing(0);
    names->addWidget(title);
    names->addWidget(statusLabel);
    enabled = new Switch;
    enabled->setAccessibleName(tr("Listen for taps"));
    auto *header = new QHBoxLayout;
    header->setSpacing(12);
    header->addWidget(logo);
    header->addLayout(names, 1);
    header->addWidget(new QLabel(tr("Listen for taps")));
    header->addWidget(enabled);

    // The laptop, with one tile per side
    laptop = new LaptopView;
    laptop->labels[Left] = tr("LEFT");
    laptop->labels[Right] = tr("RIGHT");
    laptop->idleText = tr("Tap the palm rest");
    laptop->pausedText = tr("Paused");
    laptop->onClick = [this](int side) { showPopover(side); };
    auto *stage = new QHBoxLayout;
    stage->setSpacing(18);
    for (int side = 0; side < 2; ++side) {
        ZoneTile *tile = tiles[side] = new ZoneTile;
        tile->title = tr(sideNames[side]);
        tile->hint = tr("Change...");
        tile->setAccessibleName(tr("%1 actions").arg(tile->title));
        for (const char *name : patternNames)
            tile->names << tr(name);
        connect(tile, &QAbstractButton::clicked, this, [this, side] { showPopover(activeSide == side ? -1 : side); });
    }
    stage->addWidget(tiles[Left], 1);
    stage->addWidget(laptop);
    stage->addWidget(tiles[Right], 1);

    // Settings card
    // With earphones plugged in, Windows' default is usually the earphone mic,
    // which can't hear the chassis; the built-in one has to be picked here.
    // ponytail: list is read once at startup; restart to see a newly plugged mic.
    mic = new Combo;
    mic->setFixedWidth(330);
    mic->addItem(tr("Windows default"), QString());
    for (const TapEngine::Mic &m : TapEngine::microphones())
        mic->addItem(m.name, m.id);
    sensitivity = new QSlider(Qt::Horizontal);
    sensitivity->setRange(0, 100);
    sensitivity->setFixedWidth(330);
    ignoreInput = new Switch;
    ignoreInput->setAccessibleName(tr("Ignore taps while typing or using the mouse"));
    sideStatus = new QLabel;
    sideStatus->setObjectName("sub");
    calibrateButton = new QPushButton(tr("Calibrate..."));
    // Only a copy with Qt's files beside it can start by itself. One run from Qt
    // Creator can't, so it must not put itself in the list.
    const bool installed = QFileInfo::exists(QCoreApplication::applicationDirPath() + "/platforms");
    auto *startup = new Switch;
    startup->setAccessibleName(tr("Start when I sign in to Windows"));
    startup->setChecked(QSettings(runKey, QSettings::NativeFormat).contains(runName));
    startup->setEnabled(installed);
    connect(startup, &QAbstractButton::toggled, this, [](bool on) {
        QSettings run(runKey, QSettings::NativeFormat);
        if (on)
            run.setValue(runName, QStringLiteral("\"%1\" --hidden").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
        else
            run.remove(runName);
    });
    auto *startupNote = new QLabel(installed ? QString() : tr("Only in the installed app (install.ps1)"));
    startupNote->setObjectName("sub");
    auto *card = new Card;
    auto addRow = [card](const QString &label, QWidget *note, QWidget *control) { // name, grey note, control at the right
        QHBoxLayout *row = card->addRow();
        row->addWidget(new QLabel(label));
        if (note)
            row->addWidget(note, 1);
        else
            row->addStretch(1);
        row->addWidget(control);
    };
    addRow(tr("Microphone"), nullptr, mic);
    addRow(tr("Sensitivity"), nullptr, sensitivity);
    addRow(ignoreInput->accessibleName(), nullptr, ignoreInput);
    addRow(tr("Left and right"), sideStatus, calibrateButton);
    addRow(startup->accessibleName(), startupNote, startup);

    // Per-tap numbers, for tuning. Hidden until asked for.
    activity = new QPushButton(tr("Show activity"));
    activity->setObjectName("link");
    activity->setCheckable(true);
    activity->setCursor(Qt::PointingHandCursor);
    log = new QPlainTextEdit;
    log->setReadOnly(true);
    log->setMaximumBlockCount(500);
    log->setMinimumHeight(96);
    log->hide();

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(22, 14, 22, 20); // the bottom leaves room for an open popover's shadow
    layout->setSpacing(12);
    layout->addLayout(header);
    layout->addLayout(stage);
    layout->addWidget(card);
    layout->addWidget(activity, 0, Qt::AlignLeft);
    layout->addWidget(log, 1);
    layout->addStretch();
    setCentralWidget(central);
    central->setFocus(); // or the first switch starts out with a focus ring nobody asked for

    // One popover per side: what single, double and triple taps there do
    for (int side = 0; side < 2; ++side) {
        Popover *pop = popovers[side] = new Popover(central);
        pop->hide();
        pop->setFixedWidth(510);
        auto *grid = new QGridLayout(pop);
        grid->setContentsMargins(Popover::Pad + 20, Popover::Pad + Popover::Arrow + 16, Popover::Pad + 20, Popover::Pad + 18);
        grid->setHorizontalSpacing(14);
        grid->setVerticalSpacing(9);
        auto *head = new QLabel(tr(sideNames[side]));
        head->setObjectName("popTitle");
        auto *hint = new QLabel(tr("Choose what taps on this side of the laptop do."));
        hint->setObjectName("sub");
        grid->addWidget(head, 0, 0, 1, 2);
        grid->addWidget(hint, 1, 0, 1, 2);
        grid->setRowMinimumHeight(2, 4);
        for (int i = 0; i < 3; ++i) {
            ActionRow &row = rows[side][i];
            row.kind = new Combo;
            row.kind->setFixedWidth(196);
            row.kind->addItem(tr("Do nothing"), NoAction);
            row.kind->addItem(tr("Play / Pause"), VK_MEDIA_PLAY_PAUSE);
            row.kind->addItem(tr("Next track"), VK_MEDIA_NEXT_TRACK);
            row.kind->addItem(tr("Previous track"), VK_MEDIA_PREV_TRACK);
            row.kind->addItem(tr("Mute"), VK_VOLUME_MUTE);
            row.kind->addItem(tr("Volume up"), VK_VOLUME_UP);
            row.kind->addItem(tr("Volume down"), VK_VOLUME_DOWN);
            row.kind->addItem(tr("Open an app"), OpenApp);
            row.kind->addItem(tr("Close the app in front"), CloseFront);
            row.kind->addItem(tr("Press a shortcut"), KeyShortcut);
            row.kind->addItem(tr("Run a command"), RunCommand);
            row.keys = new QKeySequenceEdit;
            row.keys->setMaximumSequenceLength(1);
            row.command = new QLineEdit;
            row.command->setPlaceholderText(tr("Program or script, with arguments"));
            row.app = new QPushButton;
            connect(row.app, &QPushButton::clicked, this, [this, button = row.app] {
                QString name, target;
                if (chooseApp(this, &name, &target)) {
                    button->setText(name);
                    button->setProperty("target", target);
                    settingsChanged();
                }
            });
            auto *line = new QHBoxLayout;
            line->setSpacing(8);
            line->addWidget(row.kind);
            line->addWidget(row.keys, 1);
            line->addWidget(row.command, 1);
            line->addWidget(row.app, 1);
            line->addStretch();
            grid->addWidget(new QLabel(tr(patternNames[i])), i + 3, 0);
            grid->addLayout(line, i + 3, 1);
        }
        grid->setColumnStretch(1, 1);
    }
    qApp->installEventFilter(this);

    // Tray: the app keeps listening with the window closed.
    qApp->setQuitOnLastWindowClosed(false);
    tray = new QSystemTrayIcon(icon, this);
    tray->setToolTip(tr("Cronos"));
    auto *menu = new QMenu(this);
    QAction *toggle = menu->addAction(tr("Listen for taps"));
    toggle->setCheckable(true);
    connect(toggle, &QAction::toggled, enabled, &QAbstractButton::setChecked);
    connect(enabled, &QAbstractButton::toggled, toggle, &QAction::setChecked);
    menu->addAction(tr("Show window"), this, &MainWindow::reveal);
    menu->addAction(tr("Quit"), qApp, &QCoreApplication::quit);
    tray->setContextMenu(menu);
    connect(tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason why) {
        if (why == QSystemTrayIcon::Trigger)
            reveal();
    });
    tray->show();

    connect(&engine, &TapEngine::status, statusLabel, showStatus);
    connect(&engine, &TapEngine::level, laptop, [this](float peak) { // -60..0 dB across the meter
        laptop->setLevel(peak > 0 ? qBound(0.f, 1 + std::log10(peak) / 3, 1.f) : 0);
    });
    connect(&engine, &TapEngine::tap, this, [this](float peak, float ms, float hz, float lag, float thump, bool counted) {
        say((counted ? tr("tap")
             : thump < engine.minThump ? tr("ignored - no thump, so not a tap on the laptop")
             : hz < engine.minRingHz   ? tr("ignored - the laptop did not ring, so not a tap on it")
                                       : tr("ignored - not from a palm rest"))
            + tr("   peak %1   %2 ms   ~%3 Hz   lag %4   thump %5 dB")
                  .arg(peak, 0, 'f', 3).arg(int(ms)).arg(int(hz)).arg(signedNumber(lag), signedNumber(thump)));
    });
    connect(&engine, &TapEngine::pattern, this, &MainWindow::onPattern);
    connect(calibrateButton, &QPushButton::clicked, this, &MainWindow::calibrate);

    load(); // before the connections below, so a half-loaded state is never saved
    connect(enabled, &QAbstractButton::toggled, this, &MainWindow::settingsChanged);
    connect(mic, &QComboBox::currentIndexChanged, this, &MainWindow::settingsChanged);
    connect(ignoreInput, &QAbstractButton::toggled, this, &MainWindow::settingsChanged);
    connect(sensitivity, &QSlider::valueChanged, this, &MainWindow::settingsChanged);
    connect(activity, &QAbstractButton::toggled, this, &MainWindow::settingsChanged);
    for (const auto &side : rows) {
        for (const ActionRow &row : side) {
            connect(row.kind, &QComboBox::currentIndexChanged, this, &MainWindow::settingsChanged);
            connect(row.keys, &QKeySequenceEdit::keySequenceChanged, this, &MainWindow::settingsChanged);
            connect(row.command, &QLineEdit::textChanged, this, &MainWindow::settingsChanged);
        }
    }
    settingsChanged();
    resize(790, minimumSizeHint().height());
}

void MainWindow::reveal()
{
    showNormal();
    raise();
    activateWindow();
}

bool MainWindow::nativeEvent(const QByteArray &type, void *message, qintptr *result)
{
    if (static_cast<MSG *>(message)->message == showMessage)
        reveal();
    return QMainWindow::nativeEvent(type, message, result);
}

// Settings live in %APPDATA%\cronos\cronos.ini
static const char *const org = "cronos";

void MainWindow::load()
{
    const QSettings s(QSettings::IniFormat, QSettings::UserScope, org, org);
    enabled->setChecked(s.value("enabled", true).toBool());
    mic->setCurrentIndex(qMax(0, mic->findData(s.value("microphone").toString()))); // unplugged -> default
    // 35 is onset ratio ~9: on the recordings in tuning/ every tap is heard with the right
    // count, while 70 (ratio 4) also picks up finger-contact noise as extra taps.
    sensitivity->setValue(s.value("sensitivity2", 35).toInt());
    ignoreInput->setChecked(s.value("ignoreInput", true).toBool());
    activity->setChecked(s.value("showLog", false).toBool());
    sideCenter = s.value("side/center", 0.f).toFloat();
    leftFirst = s.value("side/leftFirst", true).toBool();
    calibrated = s.value("side/calibrated", false).toBool();
    sideLags[Left] = s.value("side/left", 0.f).toFloat();
    sideLags[Right] = s.value("side/right", 0.f).toFloat();
    minThump = s.value("side/thump", -200.f).toFloat();
    minRing = s.value("side/ring", 0.f).toFloat();
    // Single tap does nothing by default: one stray knock is too easy to make.
    const int defaults[] = {NoAction, VK_MEDIA_PLAY_PAUSE, VK_MEDIA_NEXT_TRACK};
    for (int side = 0; side < 2; ++side) {
        for (int i = 0; i < 3; ++i) {
            // Both sides start from what the pattern did before the sides were told apart ("tapN/...").
            const QString old = QStringLiteral("tap%1/").arg(i + 1);
            const QString prefix = (side == Left ? QStringLiteral("left/") : QStringLiteral("right/")) + old;
            const ActionRow &row = rows[side][i];
            const int kind = s.value(prefix + "action", s.value(old + "action", defaults[i])).toInt();
            row.kind->setCurrentIndex(qMax(0, row.kind->findData(kind)));
            row.keys->setKeySequence(QKeySequence(s.value(prefix + "keys", s.value(old + "keys")).toString()));
            row.command->setText(s.value(prefix + "command", s.value(old + "command")).toString());
            row.app->setText(s.value(prefix + "appName", tr("Choose app...")).toString());
            row.app->setProperty("target", s.value(prefix + "app"));
        }
    }
}

void MainWindow::settingsChanged()
{
    // Slider 0..100 -> onset ratio 20..2 (log scale).
    engine.onsetRatio = 2.f * std::pow(10.f, (100 - sensitivity->value()) / 100.f);
    engine.ignoreWhileInput = ignoreInput->isChecked();
    // Only sounds from where the calibration taps came from, with a thump like
    // theirs, count. Not while calibrating: a wrong calibration could then never
    // be replaced.
    engine.sideLagLeft = calibrating ? 0 : sideLags[Left];
    engine.sideLagRight = calibrating ? 0 : sideLags[Right];
    engine.minThump = calibrating ? -200.f : minThump;
    engine.minRingHz = calibrating ? 0.f : minRing;
    const QString micId = mic->currentData().toString();
    if (!enabled->isChecked() || micId != engine.deviceId)
        engine.stop(); // releases the microphone
    engine.deviceId = micId;
    if (enabled->isChecked() && !engine.isRunning())
        engine.start();

    laptop->listening = enabled->isChecked();
    laptop->update();
    calibrateButton->setEnabled(enabled->isChecked());
    sideStatus->setText(sideLags[Left] != sideLags[Right] ? tr("Calibrated: sounds from elsewhere are ignored")
                        : calibrated ? tr("Calibrated on this laptop")
                                     : tr("Not calibrated yet, sides are a first guess"));
    if (log->isHidden() == activity->isChecked()) {
        log->setVisible(activity->isChecked());
        activity->setText(activity->isChecked() ? tr("Hide activity") : tr("Show activity"));
        QTimer::singleShot(0, this, [this] { resize(width(), minimumSizeHint().height()); });
    }

    QSettings s(QSettings::IniFormat, QSettings::UserScope, org, org);
    s.setValue("enabled", enabled->isChecked());
    s.setValue("microphone", micId);
    s.setValue("sensitivity2", sensitivity->value()); // "2": the scale changed with the 4 kHz filter, old saved values no longer mean the same
    s.setValue("ignoreInput", ignoreInput->isChecked());
    s.setValue("showLog", activity->isChecked());
    s.setValue("side/center", double(sideCenter));
    s.setValue("side/leftFirst", leftFirst);
    s.setValue("side/calibrated", calibrated);
    s.setValue("side/left", double(sideLags[Left]));
    s.setValue("side/right", double(sideLags[Right]));
    s.setValue("side/thump", double(minThump));
    s.setValue("side/ring", double(minRing));
    for (int side = 0; side < 2; ++side) {
        QStringList summary;
        for (int i = 0; i < 3; ++i) {
            const ActionRow &row = rows[side][i];
            const int kind = row.kind->currentData().toInt();
            const QString target = row.app->property("target").toString();
            row.keys->setVisible(kind == KeyShortcut);
            row.command->setVisible(kind == RunCommand);
            row.app->setVisible(kind == OpenApp);
            if (kind == KeyShortcut)
                summary << (row.keys->keySequence().isEmpty() ? tr("No shortcut set")
                                                              : row.keys->keySequence().toString(QKeySequence::NativeText));
            else if (kind == RunCommand)
                summary << (row.command->text().isEmpty() ? tr("No command set") : row.command->text());
            else if (kind == OpenApp)
                summary << (target.isEmpty() ? tr("No app chosen") : tr("Open %1").arg(row.app->text()));
            else
                summary << row.kind->currentText();
            const QString prefix = QStringLiteral("%1/tap%2/").arg(side == Left ? "left" : "right").arg(i + 1);
            s.setValue(prefix + "action", kind);
            s.setValue(prefix + "keys", row.keys->keySequence().toString());
            s.setValue(prefix + "command", row.command->text());
            s.setValue(prefix + "app", target);
            if (!target.isEmpty())
                s.setValue(prefix + "appName", row.app->text());
        }
        tiles[side]->values = summary;
        tiles[side]->update();
    }
}

void MainWindow::onPattern(int count, float lag)
{
    if (calibrating)
        return;
    if (count == 0) {
        say(tr("ignored - keyboard or mouse was in use"));
        return;
    }
    count = qMin(count, 3); // four or more taps count as a triple
    // A one-channel mic can't tell the sides apart: everything counts as left.
    const int side = engine.stereo && (lag > sideCenter) != leftFirst ? Right : Left;
    const ActionRow &row = rows[side][count - 1];
    const int kind = row.kind->currentData().toInt();
    QString what = row.kind->currentText();
    if (kind == KeyShortcut) {
        const QList<WORD> keys = toVirtualKeys(row.keys->keySequence());
        what = keys.isEmpty() ? tr("no usable shortcut set")
                              : row.keys->keySequence().toString(QKeySequence::NativeText);
        sendKeys(keys);
    } else if (kind == RunCommand) {
        QStringList parts = QProcess::splitCommand(row.command->text());
        if (parts.isEmpty()) {
            what = tr("no command set");
        } else {
            const QString program = parts.takeFirst();
            what = (QProcess::startDetached(program, parts) ? tr("ran %1") : tr("could not run %1"))
                       .arg(program);
        }
    } else if (kind == OpenApp) {
        const QString target = row.app->property("target").toString();
        if (target.isEmpty()) {
            what = tr("no app chosen");
        } else {
            // ShellExecute, as if double-clicked: works for programs, shortcuts, documents and Store apps alike.
            const QString folder = target.startsWith("shell:") ? QString() : QFileInfo(target).absolutePath();
            const auto result = INT_PTR(ShellExecuteW(nullptr, nullptr, reinterpret_cast<LPCWSTR>(target.utf16()), nullptr,
                                                      folder.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(folder.utf16()),
                                                      SW_SHOWNORMAL));
            what = (result > 32 ? tr("opened %1") : tr("could not open %1")).arg(row.app->text());
        }
    } else if (kind == CloseFront) {
        // Same as clicking the window's X. Not the desktop or taskbar: closing those brings up "Shut Down Windows".
        const HWND front = GetForegroundWindow();
        wchar_t cls[32] = L"";
        if (front)
            GetClassNameW(front, cls, 32);
        const QString name = QString::fromWCharArray(cls);
        if (!front || front == GetShellWindow() || name == "WorkerW" || name == "Shell_TrayWnd")
            what = tr("no app in front");
        else
            PostMessageW(front, WM_SYSCOMMAND, SC_CLOSE, 0);
    } else if (kind != NoAction) {
        sendKeys({WORD(kind)});
    }
    const QString name = tr(patternNames[count - 1]);
    laptop->flash(side, name, what);
    say(QStringLiteral("%1, %2 -> %3   (lag %4)").arg(tr(sideNames[side]), name, what, signedNumber(lag)));
}

void MainWindow::showPopover(int side)
{
    activeSide = side;
    laptop->setActive(side);
    for (int s = 0; s < 2; ++s) {
        tiles[s]->setActive(s == side);
        popovers[s]->setVisible(false);
    }
    if (side < 0)
        return;
    // Under the side's tile, arrow pointing up at it, kept inside the window.
    Popover *pop = popovers[side];
    pop->adjustSize();
    const QWidget *area = centralWidget();
    const QPoint anchor = tiles[side]->mapTo(area, QPoint(tiles[side]->width() / 2, tiles[side]->height()));
    const int x = qBound(4, anchor.x() - pop->width() / 2, area->width() - pop->width() - 4);
    const QPoint at(x, anchor.y() + 2 - Popover::Pad);
    pop->arrowX = anchor.x() - x;
    pop->appear(at);
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
    if (activeSide >= 0 && event->type() == QEvent::MouseButtonPress && object->isWidgetType()
        && !QApplication::activeModalWidget()) { // not while the app chooser is up
        // Clicks in the popover, or in a dropdown it opened, leave it open.
        // Its own tile does too: that click toggles it.
        bool inside = false;
        for (auto *w = static_cast<QWidget *>(object); w; w = w->parentWidget())
            inside |= w == popovers[activeSide] || w == tiles[activeSide];
        if (!inside)
            showPopover(-1);
    } else if (activeSide >= 0 && event->type() == QEvent::KeyPress && !QApplication::activePopupWidget()
               && !QApplication::activeModalWidget() && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape
               && !qobject_cast<QKeySequenceEdit *>(QApplication::focusWidget())) {
        showPopover(-1);
        return true;
    }
    return QMainWindow::eventFilter(object, event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (isVisible() && activeSide >= 0)
        showPopover(-1); // it was placed for the old size
}

void MainWindow::calibrate()
{
    static const int Needed = 5;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Calibrate left and right"));
    dialog.setMinimumWidth(380);
    auto *text = new QLabel;
    text->setWordWrap(true);
    text->setMinimumHeight(120);
    text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 16, 20, 14);
    layout->addWidget(text, 1);
    layout->addWidget(buttons);

    QList<float> lags[2];
    QList<float> thumps, rings; // of all ten taps
    auto prompt = [&] {
        const int side = lags[Left].size() < Needed ? Left : Right;
        text->setText(tr("<h3>Tap the %1 palm rest</h3>One tap at a time, about a second apart, "
                         "with your hands off the keyboard and touchpad.<br><br><b>%2 of %3</b>")
                          .arg(side == Left ? tr("left") : tr("right")).arg(lags[side].size()).arg(Needed));
    };
    if (engine.stereo)
        prompt();
    else
        text->setText(tr("<h3>Not possible with this microphone</h3>It has a single channel, so there is "
                         "nothing to compare. A built-in microphone array is needed to tell left from right."));
    connect(&engine, &TapEngine::tap, &dialog, [&](float, float, float hz, float lag, float thump, bool) {
        if (!engine.stereo || lags[Right].size() == Needed)
            return;
        lags[lags[Left].size() < Needed ? Left : Right] << lag;
        thumps << thump;
        rings << hz;
        if (lags[Right].size() < Needed) {
            prompt();
            return;
        }
        auto median = [](QList<float> v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        const float left = median(lags[Left]), right = median(lags[Right]), center = (left + right) / 2;
        int correct = 0; // how many of the ten taps the new dividing line gets right
        for (float v : lags[Left])
            correct += (v > center) == (left > right);
        for (float v : lags[Right])
            correct += (v > center) != (left > right);
        const QString numbers = tr("Left taps measured %1, right taps %2 (lag in samples).")
                                    .arg(signedNumber(left), signedNumber(right));
        if (std::fabs(left - right) < 0.5f || correct < 2 * Needed - 1) {
            text->setText(tr("<h3>Could not tell the sides apart</h3>%1 Only %2 of %3 taps would land on the "
                             "correct side, so nothing was changed. Try again, tapping nearer the edges.")
                              .arg(numbers).arg(correct).arg(2 * Needed));
        } else {
            sideCenter = center;
            leftFirst = left > right;
            sideLags[Left] = left;
            sideLags[Right] = right;
            // -14 dB is what the recordings in tuning/ support (TapConfig::minThumpDb):
            // 69 taps measured -8.9 or more, slaps on a shoulder -16 to -30. These ten
            // taps may loosen that for a laptop whose thump is weaker, never tighten
            // it: taps spread over 15 dB, more than ten of them show. The third
            // lowest, so that a cough or two in between cannot switch the check off.
            // ponytail: the 6 is the most that still gives -14 from any ten of the
            // recorded taps (third lowest of them all: -7.9). On a laptop where this
            // rule does loosen, nothing has been measured; go by the log there.
            std::sort(thumps.begin(), thumps.end());
            minThump = std::min(-14.f, thumps[2] - 6.f);
            // The same for the ring (TapConfig::minRingHz): 12500 Hz on the recordings
            // (taps 13050 Hz and up, third lowest of them all 13667; speech and cracked
            // knuckles that got through the other checks 3300-12450).
            std::sort(rings.begin(), rings.end());
            minRing = std::min(12500.f, rings[2] - 1000.f);
            calibrated = true;
            settingsChanged();
            say(tr("Sounds with a thump under %1 dB or a ring under %2 Hz are ignored.")
                    .arg(signedNumber(minThump)).arg(int(minRing)));
            text->setText(tr("<h3>Calibrated</h3>%1 %2 of %3 taps land on the correct side.")
                              .arg(numbers).arg(correct).arg(2 * Needed));
        }
        say(numbers);
    });
    calibrating = true;
    settingsChanged(); // hear everything meanwhile
    dialog.exec();
    calibrating = false;
    settingsChanged();
}

void MainWindow::say(const QString &line)
{
    log->appendPlainText(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz  ")) + line);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!tray->isVisible()) { // no tray to live in: closing really quits
        qApp->quit();
        return;
    }
    hide();
    event->ignore();
    static bool told = false;
    if (!told) {
        told = true;
        tray->showMessage(tr("Cronos"), tr("Still listening. Right-click the tray icon to quit."));
    }
}
