// Colours and the custom-painted pieces of the main window. Header-only and
// without Q_OBJECT: these use plain callbacks where they would need a signal.
#ifndef WIDGETS_H
#define WIDGETS_H

#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QDir>
#include <QGraphicsOpacityEffect>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleHints>
#include <QTimer>
#include <QVariantAnimation>

#include <cmath>
#include <functional>

struct Theme {
    bool dark;
    QColor window, card, border, text, sub, accent, field, button;
    QColor body1, body2, outline, well, key, bezel, screen1, screen2; // the laptop drawing
};

// ponytail: read once at startup; restart after switching Windows between light and dark.
inline const Theme &theme()
{
    static const Theme t = QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
        ? Theme{true, "#1e1e20", "#29292c", "#3c3c40", "#f5f5f7", "#98989f", "#0a84ff", "#1c1c1e", "#3a3a3e",
                "#77787d", "#5b5c61", QColor(255, 255, 255, 40), "#151517", "#2c2c30", "#050506", "#22304d", "#0d1322"}
        : Theme{false, "#f5f5f7", "#ffffff", "#dcdce1", "#1d1d1f", "#6e6e73", "#007aff", "#ffffff", "#ffffff",
                "#ededf0", "#d4d4d9", QColor(0, 0, 0, 45), "#1c1c1e", "#35353a", "#1d1d1f", "#2b3d63", "#111a2e"};
    return t;
}

inline QColor mix(const QColor &a, const QColor &b, qreal k) // a at 0, b at 1
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * k, a.greenF() + (b.greenF() - a.greenF()) * k,
                            a.blueF() + (b.blueF() - a.blueF()) * k, a.alphaF() + (b.alphaF() - a.alphaF()) * k);
}

inline QColor faded(QColor c, qreal alpha)
{
    c.setAlphaF(alpha);
    return c;
}

// The same rectangle with its edges moved onto the centres of screen pixels.
// At 125% scaling a one-pixel outline otherwise straddles two pixels and comes
// out grey and soft. Draw it with a zero-width pen (one screen pixel).
inline QRectF crisp(const QWidget *w, const QRectF &r)
{
    const qreal k = w->devicePixelRatioF();
    const QPointF o = w->mapTo(w->window(), QPoint(0, 0));
    auto low = [k](qreal v, qreal origin) { return (std::floor((v + origin) * k) + 0.5) / k - origin; };
    auto high = [k](qreal v, qreal origin) { return (std::ceil((v + origin) * k) - 0.5) / k - origin; };
    return QRectF(QPointF(low(r.left(), o.x()), low(r.top(), o.y())),
                  QPointF(high(r.right(), o.x()), high(r.bottom(), o.y())));
}

// The rounded panel everything sits on: a contact shadow, the fill, a hairline
// edge that is lighter at the top in dark mode, as if lit from above.
inline void paintPanel(QPainter &p, const QWidget *w, const QRectF &r, qreal radius, const QColor &fill, const QColor &edge)
{
    const Theme &t = theme();
    const QRectF box = crisp(w, r);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, t.dark ? 34 : 12));
    p.drawRoundedRect(box.translated(0, 1), radius, radius);
    QLinearGradient lit(box.topLeft(), box.topLeft() + QPointF(0, 2 * radius));
    lit.setColorAt(0, t.dark ? mix(edge, Qt::white, 0.14) : edge);
    lit.setColorAt(1, edge);
    p.setPen(QPen(lit, 0));
    p.setBrush(fill);
    p.drawRoundedRect(box, radius, radius);
}

inline void applyTheme(QApplication &app)
{
    const Theme &t = theme();
    app.setStyle(QStringLiteral("Fusion"));
    QFont font(QStringList{QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}); // the first is Windows 11's
    font.setPixelSize(13);
    app.setFont(font);
    QPalette p;
    p.setColor(QPalette::Window, t.window);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.field);
    p.setColor(QPalette::AlternateBase, t.card);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Button, t.button);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::Highlight, t.accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, t.sub);
    p.setColor(QPalette::ToolTipBase, t.card);
    p.setColor(QPalette::ToolTipText, t.text);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, t.sub);
    app.setPalette(p);

    // The combo box's blue cap with its two chevrons. A style sheet can only take
    // it from a file; the "@2x" copy keeps it sharp on a scaled display.
    const QString cap = QDir::temp().filePath(QStringLiteral("cronos-cap.png"));
    for (int scale : {1, 2}) {
        QPixmap image(16 * scale, 16 * scale);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(scale, scale);
        QLinearGradient blue(0, 0, 0, 16);
        blue.setColorAt(0, mix(t.accent, Qt::white, 0.18));
        blue.setColorAt(1, t.accent);
        painter.setPen(Qt::NoPen);
        painter.setBrush(blue);
        painter.drawRoundedRect(QRectF(0, 0, 16, 16), 4.5, 4.5);
        painter.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(QPolygonF{{5, 6.6}, {8, 3.8}, {11, 6.6}});
        painter.drawPolyline(QPolygonF{{5, 9.4}, {8, 12.2}, {11, 9.4}});
        painter.end();
        image.save(scale == 1 ? cap : QDir::temp().filePath(QStringLiteral("cronos-cap@2x.png")));
    }

    app.setStyleSheet(QStringLiteral(
        "#title { font-family: 'Segoe UI Variable Display', 'Segoe UI'; font-size: 22px; font-weight: 600; }"
        "#popTitle { font-size: 15px; font-weight: 600; }"
        "#sub { color: %1; font-size: 12px; }"
        "QComboBox, QLineEdit, QPushButton { border: 1px solid %3; border-radius: 7px; min-height: 26px; }"
        "QComboBox { background: %5; padding: 0 4px 0 10px; }"
        "QComboBox:hover, QPushButton:hover { background: %8; }"
        "QComboBox::drop-down { border: none; width: 26px; }"
        "QComboBox::down-arrow { image: url(%7); width: 16px; height: 16px; }"
        "QComboBox QAbstractItemView { background: %2; border: none; padding: 4px; outline: 0; }"
        "QComboBox QAbstractItemView::item { min-height: 22px; padding: 0 8px; border-radius: 5px; }"
        "QComboBox QAbstractItemView::item:selected { background: %4; color: white; }"
        "QLineEdit { background: %6; padding: 0 8px; selection-background-color: %4; }"
        "QPushButton { background: %5; padding: 0 14px; }"
        "QPushButton:pressed { background: %3; }"
        "QPushButton:disabled { color: %1; }"
        "QPushButton:default { background: %4; border-color: %4; color: white; }"
        "QComboBox:focus, QLineEdit:focus, QPushButton:focus { border-color: %4; }"
        "#link, #link:hover { border: none; background: transparent; color: %4; text-align: left; padding: 4px 0; }"
        "QSlider { min-height: 24px; }"
        "QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: %3; }"
        "QSlider::sub-page:horizontal { border-radius: 2px; background: %4; }"
        "QSlider::handle:horizontal { width: 18px; margin: -8px 0; border-radius: 10px; background: white;"
        " border: 1px solid rgba(0, 0, 0, 70); }"
        "QListWidget { background: %6; border: 1px solid %3; border-radius: 9px; padding: 4px; outline: 0; }"
        "QListWidget::item { padding: 4px; border-radius: 6px; }"
        "QListWidget::item:selected { background: %4; color: white; }"
        "QPlainTextEdit { background: %2; border: 1px solid %3; border-radius: 12px; padding: 8px;"
        " font-family: 'Cascadia Mono', Consolas; font-size: 12px; }"
        "QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }"
        "QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }"
        "QScrollBar::handle { background: %9; border-radius: 4px; min-height: 28px; min-width: 28px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
        "QToolTip { background: %2; color: %10; border: 1px solid %3; padding: 4px 6px; }")
            .arg(t.sub.name(), t.card.name(), t.border.name(), t.accent.name(), t.button.name(), t.field.name(), cap,
                 mix(t.button, t.text, 0.07).name(), mix(t.card, t.text, 0.3).name(), t.text.name()));
}

// A number that glides to wherever it is sent, repainting its widget on the way.
// It jumps instead while the widget is not on screen.
class Glide : public QVariantAnimation
{
public:
    Glide(QWidget *widget, int ms = 160)
        : owner(widget)
    {
        setDuration(ms);
        setEasingCurve(QEasingCurve::OutCubic);
        connect(this, &QVariantAnimation::valueChanged, widget, qOverload<>(&QWidget::update));
    }
    void to(qreal target)
    {
        if (target == end)
            return;
        const qreal from = now();
        stop();
        end = target;
        if (!owner->isVisible()) {
            owner->update();
            return;
        }
        setStartValue(from);
        setEndValue(target);
        start();
    }
    qreal now() const { return state() == QAbstractAnimation::Running ? currentValue().toReal() : end; }

private:
    QWidget *owner;
    qreal end = 0;
};

// On/off pill, in place of a checkbox.
class Switch : public QAbstractButton
{
public:
    Switch()
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus); // the focus ring is for keyboard users; a click shouldn't leave one behind
        connect(this, &QAbstractButton::toggled, this, [this](bool on) { knob.to(on); });
    }
    QSize sizeHint() const override { return {46, 30}; } // bigger than the pill: easier to hit

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = theme();
        const qreal k = knob.now();
        const QRectF pill(4, 4, width() - 8, height() - 8);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setOpacity(isEnabled() ? 1 : 0.4);
        p.setPen(Qt::NoPen);
        p.setBrush(mix(t.dark ? QColor("#4a4a4f") : QColor("#d1d1d6"), t.accent, k));
        p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        const qreal d = pill.height() - 4, stretch = isDown() ? 4 : 0; // the knob widens while held
        const QRectF dot(pill.left() + 2 + (pill.width() - d - 4 - stretch) * k, pill.top() + 2, d + stretch, d);
        p.setBrush(QColor(0, 0, 0, 45));
        p.drawRoundedRect(dot.translated(0, 1), d / 2, d / 2);
        p.setBrush(Qt::white);
        p.drawRoundedRect(dot, d / 2, d / 2);
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(faded(t.accent, 0.45), 2));
            p.drawRoundedRect(pill.adjusted(-2, -2, 2, 2), pill.height() / 2 + 2, pill.height() / 2 + 2);
        }
    }

private:
    Glide knob{this, 180};
};

// A group of settings: one row per addRow(), with a hairline between rows.
class Card : public QWidget
{
public:
    Card()
    {
        rows = new QVBoxLayout(this);
        rows->setContentsMargins(16, 3, 16, 5);
        rows->setSpacing(0);
    }
    QHBoxLayout *addRow()
    {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 5, 0, 5);
        row->setSpacing(12);
        rows->addLayout(row);
        return row;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = theme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        paintPanel(p, this, QRectF(rect()).adjusted(1, 1, -1, -2), 12, t.card, t.border);
        p.setPen(QPen(t.border, 0));
        for (int i = 1; i < rows->count(); ++i) {
            const QRectF line = crisp(this, QRectF(16, rows->itemAt(i)->geometry().top(), width() - 32, 0));
            p.drawLine(line.topLeft(), line.topRight());
        }
    }

private:
    QVBoxLayout *rows;
};

// One side's summary, left or right of the laptop. Click (or Space) to edit it.
class ZoneTile : public QAbstractButton
{
public:
    QString title, hint;       // hint: what a click does, shown top right
    QStringList names, values; // each tap pattern and what it does

    ZoneTile()
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setAttribute(Qt::WA_Hover);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); // as tall as the laptop beside it
    }
    QSize sizeHint() const override { return {200, 222}; }
    void setActive(bool on) // its popover is open
    {
        active = on;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = theme();
        hover.to(underMouse());
        ring.to(active || hasFocus());
        const qreal h = hover.now(), a = ring.now();
        const QRectF box = QRectF(rect()).adjusted(2, 2, -2, -3);
        const qreal left = 18, w = width() - 2 * left;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor fill = mix(t.card, t.dark ? QColor(Qt::white) : t.accent, (isDown() ? 0.02 : 0.05) * h);
        paintPanel(p, this, box, 14, fill, mix(t.border, t.accent, qMax(a, 0.6 * h)));
        if (a > 0) { // a second, thicker line: the selected look
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(faded(t.accent, a), 1.5));
            p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 13.5, 13.5);
        }

        QFont f = font();
        f.setPixelSize(15);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(t.text);
        p.drawText(QRectF(left, 14, w, 24), Qt::AlignLeft | Qt::AlignVCenter, title);
        f.setPixelSize(12);
        f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(mix(t.sub, t.accent, qMax(a, h)));
        p.drawText(QRectF(left, 14, w, 24), Qt::AlignRight | Qt::AlignVCenter, hint);

        const qreal top = 50, step = (height() - top - 9) / 3.0;
        for (int i = 0; i < names.size() && i < values.size(); ++i) {
            const qreal y = top + i * step, middle = y + step / 2;
            p.setPen(QPen(t.border, 0)); // hairline above each row
            const QRectF line = crisp(this, QRectF(i ? left + 40 : left, y, 0, 0));
            p.drawLine(line.topLeft(), QPointF(width() - left, line.top()));
            const QRectF badge(left, middle - 15, 30, 30); // as many dots as taps
            p.setPen(Qt::NoPen);
            p.setBrush(faded(t.accent, t.dark ? 0.2 : 0.12));
            p.drawRoundedRect(badge, 9, 9);
            p.setBrush(t.dark ? mix(t.accent, Qt::white, 0.25) : t.accent);
            for (int dot = 0; dot <= i; ++dot)
                p.drawEllipse(QPointF(badge.center().x() + (dot - i / 2.0) * 7, badge.center().y()), 2.4, 2.4);
            f.setPixelSize(11);
            p.setFont(f);
            p.setPen(t.sub);
            p.drawText(QRectF(left + 40, middle - 18, w - 40, 15), Qt::AlignLeft | Qt::AlignVCenter, names[i]);
            f.setPixelSize(14);
            p.setFont(f);
            p.setPen(t.text);
            p.drawText(QRectF(left + 40, middle - 3, w - 40, 21), Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetricsF(f).elidedText(values[i], Qt::ElideRight, w - 40));
        }
    }

private:
    bool active = false;
    Glide hover{this}, ring{this};
};

// The laptop drawing: lid on top, deck below, split into a left and a right
// half. The lid's display shows the mic level and the last tap pattern.
class LaptopView : public QWidget
{
public:
    std::function<void(int)> onClick; // 0 = left half of the deck, 1 = right half
    bool listening = true;
    QString labels[2], idleText, pausedText;

    LaptopView()
    {
        setMouseTracking(true);
        setFixedSize(W + 4, 2 + LidH + Hinge + BaseH + 12);
        ripple.setDuration(900);
        ripple.setStartValue(0.0);
        ripple.setEndValue(1.0);
        ripple.setEasingCurve(QEasingCurve::OutCubic);
        connect(&ripple, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
        idle.setSingleShot(true);
        idle.setInterval(2500);
        connect(&idle, &QTimer::timeout, this, [this] { text.to(0); });
    }
    void setActive(int side) // the half being edited, drawn highlighted; -1 for neither
    {
        active = side;
        update();
    }
    void setLevel(float v) // 0..1
    {
        shown = qMax(v, shown - 0.08f); // jumps up, falls back gradually
        const int n = qRound(shown * Segments);
        if (n != lit) {
            lit = n;
            update(meter().toAlignedRect().adjusted(-1, -1, 1, 1));
        }
    }
    void flash(int side, const QString &head, const QString &what) // a pattern was just recognised on that half
    {
        rippleSide = side;
        headline = head;
        detail = what;
        ripple.stop();
        ripple.start();
        text.to(1);
        idle.start();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = theme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Lid: bezel, camera, display
        const QRectF lid = crisp(this, QRectF((width() - W) / 2.0, 2, W, LidH));
        p.setPen(Qt::NoPen);
        p.setBrush(t.dark ? QColor("#3b3b3f") : QColor("#b4b4ba"));
        p.drawRect(QRectF(lid.left() + 26, lid.bottom() - 2, W - 52, Hinge + 4));
        p.setPen(QPen(t.outline, 0));
        p.setBrush(t.bezel);
        p.drawRoundedRect(lid, 12, 12);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#2e2e33"));
        p.drawEllipse(QPointF(lid.center().x(), lid.top() + 4.5), 1.3, 1.3);
        const QRectF d = lid.adjusted(7, 9, -7, -7);
        QPainterPath glass;
        glass.addRoundedRect(d, 6, 6);
        p.setClipPath(glass);
        QLinearGradient sky(d.topLeft(), d.bottomRight());
        sky.setColorAt(0, t.screen1);
        sky.setColorAt(1, t.screen2);
        p.fillRect(d, sky);
        auto blob = [&](QPointF at, QColor colour) { // a soft patch of colour, as on a desktop picture
            QRadialGradient soft(at, d.width() * 0.62);
            soft.setColorAt(0, colour);
            colour.setAlpha(0);
            soft.setColorAt(1, colour);
            p.fillRect(d, soft);
        };
        blob(d.topLeft(), QColor(94, 92, 230, 125));
        blob(d.bottomRight(), QColor(191, 90, 242, 80));
        p.setClipping(false);

        QFont f = font();
        const qreal shownText = text.now(); // 0: the idle line, 1: the last pattern
        if (shownText < 1) {
            f.setPixelSize(13);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, int(160 * (1 - shownText))));
            p.drawText(d.adjusted(0, 0, 0, -8), Qt::AlignCenter, listening ? idleText : pausedText);
        }
        if (shownText > 0) {
            const qreal rise = 5 * (1 - shownText);
            f.setPixelSize(19);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, int(255 * shownText)));
            p.drawText(d.adjusted(8, rise, -8, rise - d.height() / 2 - 5), Qt::AlignHCenter | Qt::AlignBottom, headline);
            f.setPixelSize(12);
            f.setWeight(QFont::Normal);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, int(190 * shownText)));
            p.drawText(d.adjusted(8, rise + d.height() / 2 - 1, -8, 0), Qt::AlignHCenter | Qt::AlignTop,
                       QFontMetricsF(f).elidedText(detail, Qt::ElideRight, d.width() - 16));
        }
        const QRectF m = meter(); // mic level, as a row of segments
        p.setPen(Qt::NoPen);
        for (int i = 0; i < Segments; ++i) {
            p.setBrush(listening && i < lit ? QColor(i < Segments - 3 ? "#30d158" : "#ffd60a") : QColor(255, 255, 255, 40));
            p.drawRoundedRect(QRectF(m.left() + i * 7, m.top(), 4, m.height()), 2, 2);
        }

        // Deck: shadow on the table, metal shell, keyboard, trackpad
        const QRectF b = crisp(this, base());
        p.setBrush(QColor(0, 0, 0, t.dark ? 22 : 9)); // stacked layers, as under the popover
        for (int i = 1; i <= 6; ++i)
            p.drawRoundedRect(b.adjusted(9 - i, b.height() - 26, i - 9, 1.5 * i), 13, 13);
        QPainterPath shell;
        shell.addRoundedRect(b, 13, 13);
        QLinearGradient metal(b.topLeft(), b.bottomLeft());
        metal.setColorAt(0, t.body1);
        metal.setColorAt(1, t.body2);
        p.setPen(QPen(t.outline, 0));
        p.setBrush(metal);
        p.drawPath(shell);
        p.setClipPath(shell);
        p.setPen(QPen(QColor(255, 255, 255, t.dark ? 46 : 200), 0)); // light catching the far edge
        p.drawLine(QPointF(b.left(), b.top() + 1), QPointF(b.right(), b.top() + 1));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, t.dark ? 55 : 28)); // the notch for opening the lid
        p.drawRoundedRect(QRectF(b.center().x() - 20, b.bottom() - 3, 40, 8), 4, 4);
        p.setClipping(false);

        const QRectF well(b.left() + 16, b.top() + 10, b.width() - 32, 66); // keyboard
        p.setBrush(t.well);
        p.drawRoundedRect(well, 5, 5);
        p.setBrush(t.key);
        const int cols = 14, keyRows = 5;
        const qreal gap = 2.5, kh = (well.height() - 8 - (keyRows - 1) * gap) / keyRows,
                    kw = (well.width() - 8 - (cols - 1) * gap) / cols;
        auto key = [&](int row, int col, int span) {
            p.drawRoundedRect(QRectF(well.left() + 4 + col * (kw + gap), well.top() + 4 + row * (kh + gap),
                                     span * kw + (span - 1) * gap, kh), 2, 2);
        };
        for (int row = 0; row < keyRows; ++row)
            for (int col = 0; col < cols; ++col)
                if (row < keyRows - 1 || col < 4 || col > 8)
                    key(row, col, 1);
        key(keyRows - 1, 4, 5); // space bar

        const QRectF pad(b.center().x() - 50, well.bottom() + 8, 100, b.bottom() - well.bottom() - 17); // trackpad
        p.setPen(QPen(t.outline, 0));
        p.setBrush(QColor(255, 255, 255, t.dark ? 16 : 95));
        p.drawRoundedRect(crisp(this, pad), 6, 6);

        // Each half's highlight, and the ripple where a tap was just heard
        const qreal g = ripple.state() == QAbstractAnimation::Running ? ripple.currentValue().toReal() : 1;
        p.setClipPath(shell);
        for (int s = 0; s < 2; ++s) {
            tint[s].to(s == active ? 0.24 : s == hot ? 0.12 : 0);
            const qreal a = qMax(tint[s].now(), s == rippleSide ? 0.5 * (1 - g) : 0);
            p.fillRect(QRectF(s ? b.center().x() : b.left(), b.top(), b.width() / 2, b.height()), faded(t.accent, a));
        }
        if (g < 1) {
            const QPointF centre(rippleSide ? (pad.right() + b.right()) / 2 : (b.left() + pad.left()) / 2, pad.center().y());
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, int(210 * (1 - g))));
            p.drawEllipse(centre, 5, 5);
            p.setBrush(Qt::NoBrush);
            for (qreal delay : {0.0, 0.2}) { // two rings, the second a little behind
                const qreal k = (g - delay) / (1 - delay);
                if (k <= 0)
                    continue;
                p.setPen(QPen(QColor(255, 255, 255, int(220 * (1 - k))), 2));
                p.drawEllipse(centre, 8 + 84 * k, 8 + 84 * k);
            }
        }
        p.setClipping(false);
        QPen dashes(faded(t.accent, 0.6), 0);
        dashes.setDashPattern({3, 4});
        p.setPen(dashes);
        p.drawLine(QPointF(b.center().x(), b.top() + 6), QPointF(b.center().x(), b.bottom() - 6));

        f = font();
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        p.setFont(f);
        for (int s = 0; s < 2; ++s) { // on the palm rests, either side of the trackpad
            p.setPen(s == active || s == hot ? (t.dark ? QColor(Qt::white) : t.accent) : t.dark ? QColor("#e3e3e8") : t.sub);
            p.drawText(QRectF(s ? pad.right() : b.left(), pad.top(), (b.width() - pad.width()) / 2, pad.height()),
                       Qt::AlignCenter, labels[s]);
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int s = sideAt(e->position());
        if (s != hot) {
            hot = s;
            setCursor(s < 0 ? Qt::ArrowCursor : Qt::PointingHandCursor);
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        hot = -1;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const int s = sideAt(e->position());
        if (s >= 0 && e->button() == Qt::LeftButton && onClick)
            onClick(s);
    }

private:
    static constexpr int W = 270, LidH = 150, Hinge = 4, BaseH = 140, Segments = 15;

    QRectF base() const { return QRectF((width() - W) / 2.0, 2 + LidH + Hinge, W, BaseH); }
    QRectF meter() const { return QRectF((width() - (Segments * 7 - 3)) / 2.0, 2 + LidH - 24, Segments * 7 - 3, 7); }
    int sideAt(QPointF at) const
    {
        const QRectF b = base();
        return !b.contains(at) ? -1 : at.x() < b.center().x() ? 0 : 1;
    }

    int active = -1, hot = -1, rippleSide = -1, lit = 0;
    float shown = 0;
    QString headline, detail;
    QVariantAnimation ripple;
    Glide tint[2]{{this}, {this}}, text{this, 240};
    QTimer idle;
};

// Floating panel with an arrow on its top edge, shown over the window's other
// content. A child widget rather than a real popup window, so the combo boxes
// and dialogs opened from inside it behave normally.
class Popover : public QWidget
{
public:
    static constexpr int Pad = 22 /* room for the shadow */, Arrow = 9;
    int arrowX = 0; // where the arrow's tip sits, in this widget's coordinates

    using QWidget::QWidget;

    void appear(QPoint at) // fades in while settling down into place
    {
        move(at);
        raise();
        show();
        if (!window()->isVisible())
            return;
        auto *fade = new QGraphicsOpacityEffect(this);
        fade->setOpacity(0);
        setGraphicsEffect(fade); // also discards one still running, and its animation with it
        auto *motion = new QVariantAnimation(fade);
        motion->setDuration(180);
        motion->setEasingCurve(QEasingCurve::OutCubic);
        motion->setStartValue(0.0);
        motion->setEndValue(1.0);
        connect(motion, &QVariantAnimation::valueChanged, this, [this, fade, at](const QVariant &v) {
            fade->setOpacity(v.toReal());
            move(at - QPoint(0, qRound(8 * (1 - v.toReal()))));
        });
        // Queued: taking the effect off deletes the animation, which must not happen inside its own signal.
        connect(motion, &QVariantAnimation::finished, this, [this, fade] {
            if (graphicsEffect() == fade)
                setGraphicsEffect(nullptr); // back to direct painting: sharper text, cheaper redraws
        }, Qt::QueuedConnection);
        motion->start();
    }

protected:
    // Swallow clicks on the panel's own background. Unhandled, they would travel
    // on to the window behind and count there as a click outside the popover.
    void mousePressEvent(QMouseEvent *e) override { e->accept(); }
    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = theme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF body = crisp(this, QRectF(rect()).adjusted(Pad, Pad + Arrow, -Pad, -Pad));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, t.dark ? 11 : 5)); // stacked translucent layers: a soft shadow, heavier below
        for (int i = Pad - 2; i > 0; i -= 2)
            p.drawRoundedRect(body.adjusted(-i, -i + 6, i, i + 2), 13 + i, 13 + i);
        QPainterPath shape, arrow;
        shape.addRoundedRect(body, 13, 13);
        arrow.moveTo(arrowX - Arrow - 1, body.top() + 1);
        arrow.lineTo(arrowX, body.top() - Arrow);
        arrow.lineTo(arrowX + Arrow + 1, body.top() + 1);
        arrow.closeSubpath();
        p.setPen(QPen(t.dark ? mix(t.border, Qt::white, 0.08) : t.border, 0));
        p.setBrush(t.dark ? mix(t.card, Qt::white, 0.03) : t.card);
        p.drawPath(shape.united(arrow));
    }
};

#endif // WIDGETS_H
