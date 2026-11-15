// Colours and the custom-painted pieces of the main window. Header-only and
// without Q_OBJECT: these use plain callbacks where they would need a signal.
#ifndef WIDGETS_H
#define WIDGETS_H

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleHints>
#include <QTimer>
#include <QVariantAnimation>

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
        ? Theme{true, "#1e1e20", "#2a2a2d", "#3d3d41", "#f5f5f7", "#98989f", "#0a84ff", "#1c1c1e", "#3a3a3e",
                "#6b6c71", "#56575c", QColor(255, 255, 255, 40), "#151517", "#2c2c30", "#050506", "#22304d", "#0d1322"}
        : Theme{false, "#f5f5f7", "#ffffff", "#dcdce1", "#1d1d1f", "#6e6e73", "#007aff", "#ffffff", "#ffffff",
                "#ebebee", "#d4d4d9", QColor(0, 0, 0, 45), "#1c1c1e", "#35353a", "#1d1d1f", "#2b3d63", "#111a2e"};
    return t;
}

inline void applyTheme(QApplication &app)
{
    const Theme &t = theme();
    app.setStyle(QStringLiteral("Fusion"));
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

    // A style sheet can only take the combo box arrow from a file.
    const QString arrow = QDir::temp().filePath(QStringLiteral("tap-tap-arrow.png"));
    QPixmap chevron(10, 6);
    chevron.fill(Qt::transparent);
    {
        QPainter painter(&chevron);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(t.sub, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPolyline(QPolygonF{{1, 1}, {5, 5}, {9, 1}});
    }
    chevron.save(arrow);

    app.setStyleSheet(QStringLiteral(
        "#title { font-size: 20px; font-weight: 600; }"
        "#popTitle { font-size: 15px; font-weight: 600; }"
        "#sub { color: %1; font-size: 12px; }"
        "#card { background: %2; border: 1px solid %3; border-radius: 12px; }"
        "QComboBox, QLineEdit, QPushButton { border: 1px solid %3; border-radius: 6px; min-height: 24px; }"
        "QComboBox { background: %5; padding: 1px 4px 1px 10px; }"
        "QComboBox::drop-down { border: none; width: 26px; }"
        "QComboBox::down-arrow { image: url(%7); }"
        "QComboBox QAbstractItemView { background: %2; border: 1px solid %3; outline: 0;"
        " selection-background-color: %4; selection-color: white; }"
        "QLineEdit { background: %6; padding: 1px 8px; selection-background-color: %4; }"
        "QPushButton { background: %5; padding: 1px 14px; }"
        "QPushButton:pressed { background: %3; }"
        "QPushButton:disabled { color: %1; }"
        "QPushButton:default { background: %4; border-color: %4; color: white; }"
        "QComboBox:focus, QLineEdit:focus, QPushButton:focus { border-color: %4; }"
        "#link { border: none; background: transparent; color: %4; text-align: left; padding: 4px 0; }"
        "QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: %3; }"
        "QSlider::sub-page:horizontal { border-radius: 2px; background: %4; }"
        "QSlider::handle:horizontal { width: 16px; margin: -7px 0; border-radius: 9px; background: white;"
        " border: 1px solid rgba(0, 0, 0, 60); }"
        "QListWidget { background: %6; border: 1px solid %3; border-radius: 8px; padding: 4px; outline: 0; }"
        "QListWidget::item { padding: 4px; border-radius: 6px; }"
        "QListWidget::item:selected { background: %4; color: white; }"
        "QPlainTextEdit { background: %2; border: 1px solid %3; border-radius: 10px; padding: 6px;"
        " font-family: Consolas; font-size: 12px; }")
            .arg(t.sub.name(), t.card.name(), t.border.name(), t.accent.name(), t.button.name(), t.field.name(),
                 arrow));
}

// On/off pill, in place of a checkbox.
class Switch : public QAbstractButton
{
public:
    Switch()
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        slide.setDuration(140);
        slide.setEasingCurve(QEasingCurve::OutCubic);
        connect(&slide, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
        connect(this, &QAbstractButton::toggled, this, [this](bool on) {
            const qreal from = knob();
            slide.stop();
            slide.setStartValue(from);
            slide.setEndValue(on ? 1.0 : 0.0);
            slide.start();
        });
    }
    QSize sizeHint() const override { return {46, 30}; } // bigger than the pill: easier to hit

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = theme();
        const qreal k = knob();
        const QRectF pill(4, 4, width() - 8, height() - 8);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor off = t.dark ? QColor("#4a4a4f") : QColor("#d1d1d6");
        p.setPen(Qt::NoPen);
        p.setBrush(QColor::fromRgbF(off.redF() + (t.accent.redF() - off.redF()) * k,
                                    off.greenF() + (t.accent.greenF() - off.greenF()) * k,
                                    off.blueF() + (t.accent.blueF() - off.blueF()) * k));
        p.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        const qreal d = pill.height() - 4;
        const QRectF dot(pill.left() + 2 + (pill.width() - d - 4) * k, pill.top() + 2, d, d);
        p.setBrush(QColor(0, 0, 0, 40));
        p.drawEllipse(dot.translated(0, 1));
        p.setBrush(Qt::white);
        p.drawEllipse(dot);
        if (hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(t.accent.red(), t.accent.green(), t.accent.blue(), 110), 2));
            p.drawRoundedRect(pill.adjusted(-2, -2, 2, 2), pill.height() / 2 + 2, pill.height() / 2 + 2);
        }
    }

private:
    qreal knob() const
    {
        return slide.state() == QAbstractAnimation::Running ? slide.currentValue().toReal() : isChecked() ? 1.0 : 0.0;
    }
    QVariantAnimation slide;
};

// One side's summary, left or right of the laptop. Click (or Space) to edit it.
class ZoneTile : public QAbstractButton
{
public:
    QString title, footer;
    QStringList names, values; // each tap pattern and what it does
    bool active = false;       // its popover is open

    ZoneTile()
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); // as tall as the laptop beside it
    }
    QSize sizeHint() const override { return {180, 222}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = theme();
        const bool strong = active || hasFocus(), hot = strong || underMouse();
        const qreal w = width() - 32;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor edge = hot ? t.accent : t.border;
        if (!strong && hot)
            edge.setAlpha(140);
        p.setPen(QPen(edge, strong ? 2 : 1));
        p.setBrush(isDown() ? t.window : t.card);
        p.drawRoundedRect(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5), 12, 12);

        QFont f = font();
        f.setPixelSize(11);
        f.setWeight(QFont::DemiBold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
        p.setFont(f);
        p.setPen(hot ? t.accent : t.sub);
        p.drawText(QRectF(16, 14, w, 16), Qt::AlignLeft | Qt::AlignVCenter, title.toUpper());
        const qreal step = (height() - 46 - 44) / 3.0;
        qreal y = 46 + (step - 36) / 2;
        for (int i = 0; i < names.size() && i < values.size(); ++i, y += step) {
            p.setPen(QPen(t.border, 1)); // hairline above each row
            p.drawLine(QPointF(16, 46.5 + i * step), QPointF(width() - 16, 46.5 + i * step));
            f = font();
            f.setPixelSize(11);
            p.setFont(f);
            p.setPen(t.sub);
            p.drawText(QRectF(16, y, w, 14), Qt::AlignLeft | Qt::AlignVCenter, names[i]);
            f.setPixelSize(14);
            p.setFont(f);
            p.setPen(t.text);
            p.drawText(QRectF(16, y + 15, w, 20), Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetricsF(f).elidedText(values[i], Qt::ElideRight, w));
        }
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(t.accent);
        p.drawText(QRectF(16, height() - 36, w, 18), Qt::AlignLeft | Qt::AlignVCenter, footer);
    }
};

// The laptop drawing: lid on top, deck below, split into a left and a right
// half. The lid's display shows the mic level and the last tap pattern.
class LaptopView : public QWidget
{
public:
    std::function<void(int)> onClick; // 0 = left half of the deck, 1 = right half
    int active = -1;                  // half being edited, drawn highlighted
    bool listening = true;
    QString labels[2], idleText, pausedText;

    LaptopView()
    {
        setMouseTracking(true);
        setFixedSize(W + 4, 2 + LidH + 4 + BaseH + 2);
        glow.setDuration(900);
        glow.setStartValue(1.0);
        glow.setEndValue(0.0);
        glow.setEasingCurve(QEasingCurve::OutCubic);
        connect(&glow, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
        idle.setSingleShot(true);
        idle.setInterval(2500);
        connect(&idle, &QTimer::timeout, this, [this] {
            headline.clear();
            update();
        });
    }
    void setLevel(float v) // 0..1
    {
        level = v;
        update();
    }
    void flash(int side, const QString &head, const QString &what) // a pattern was just recognised on that half
    {
        glowSide = side;
        headline = head;
        detail = what;
        glow.stop();
        glow.start();
        idle.start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = theme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF lid((width() - W) / 2.0, 2, W, LidH);
        p.setPen(QPen(t.outline, 1));
        p.setBrush(t.bezel);
        p.drawRoundedRect(lid, 11, 11);
        const QRectF d = lid.adjusted(7, 7, -7, -7);
        QLinearGradient sky(d.topLeft(), d.bottomRight());
        sky.setColorAt(0, t.screen1);
        sky.setColorAt(1, t.screen2);
        p.setPen(Qt::NoPen);
        p.setBrush(sky);
        p.drawRoundedRect(d, 5, 5);
        QFont f = font();
        if (headline.isEmpty()) {
            f.setPixelSize(13);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 150));
            p.drawText(d, Qt::AlignCenter, listening ? idleText : pausedText);
        } else {
            f.setPixelSize(18);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(d.adjusted(8, 0, -8, -d.height() / 2 - 1), Qt::AlignHCenter | Qt::AlignBottom, headline);
            f.setPixelSize(12);
            f.setWeight(QFont::Normal);
            p.setFont(f);
            p.setPen(QColor(255, 255, 255, 190));
            p.drawText(d.adjusted(8, d.height() / 2 + 3, -8, 0), Qt::AlignHCenter | Qt::AlignTop,
                       QFontMetricsF(f).elidedText(detail, Qt::ElideRight, d.width() - 16));
        }
        const QRectF meter(d.center().x() - 50, d.bottom() - 14, 100, 3);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 40));
        p.drawRoundedRect(meter, 1.5, 1.5);
        p.setBrush(QColor("#30d158"));
        p.drawRoundedRect(QRectF(meter.left(), meter.top(), meter.width() * level, meter.height()), 1.5, 1.5);

        const QRectF b = base();
        QPainterPath shell;
        shell.addRoundedRect(b, 13, 13);
        QLinearGradient metal(b.topLeft(), b.bottomLeft());
        metal.setColorAt(0, t.body1);
        metal.setColorAt(1, t.body2);
        p.setPen(QPen(t.outline, 1));
        p.setBrush(metal);
        p.drawPath(shell);

        const QRectF well(b.left() + 16, b.top() + 10, b.width() - 32, 62); // keyboard
        p.setPen(Qt::NoPen);
        p.setBrush(t.well);
        p.drawRoundedRect(well, 5, 5);
        p.setBrush(t.key);
        const qreal gap = 3, kh = (well.height() - 8 - 3 * gap) / 4, kw = (well.width() - 8 - 11 * gap) / 12;
        auto key = [&](int row, int col, int span) {
            p.drawRoundedRect(QRectF(well.left() + 4 + col * (kw + gap), well.top() + 4 + row * (kh + gap),
                                     span * kw + (span - 1) * gap, kh), 2, 2);
        };
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 12; ++col)
                if (row < 3 || col < 4 || col > 7)
                    key(row, col, 1);
        key(3, 4, 4); // space bar

        const QRectF pad(b.center().x() - 48, well.bottom() + 9, 96, b.bottom() - well.bottom() - 18); // trackpad
        p.setPen(QPen(t.outline, 1));
        p.setBrush(QColor(255, 255, 255, t.dark ? 14 : 90));
        p.drawRoundedRect(pad, 6, 6);

        p.setClipPath(shell);
        for (int s = 0; s < 2; ++s) {
            qreal a = s == active ? 0.24 : s == hot ? 0.13 : 0;
            if (s == glowSide && glow.state() == QAbstractAnimation::Running)
                a = qMax(a, 0.75 * glow.currentValue().toReal());
            QColor tint = t.accent;
            tint.setAlphaF(a);
            p.fillRect(QRectF(s ? b.center().x() : b.left(), b.top(), b.width() / 2, b.height()), tint);
        }
        p.setClipping(false);
        QColor dash = t.accent;
        dash.setAlpha(170);
        QPen dashes(dash, 1);
        dashes.setDashPattern({3, 4});
        p.setPen(dashes);
        p.drawLine(QPointF(b.center().x(), b.top() + 5), QPointF(b.center().x(), b.bottom() - 5));

        f = font();
        f.setPixelSize(10);
        f.setWeight(QFont::DemiBold);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        p.setFont(f);
        for (int s = 0; s < 2; ++s) { // on the palm rests, either side of the trackpad
            p.setPen(s == active || s == hot ? (t.dark ? QColor(Qt::white) : t.accent) : t.dark ? QColor("#d1d1d6") : t.sub);
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
    static constexpr int W = 270, LidH = 152, BaseH = 142;

    QRectF base() const { return QRectF((width() - W) / 2.0, 2 + LidH + 4, W, BaseH); }
    int sideAt(QPointF at) const
    {
        const QRectF b = base();
        return !b.contains(at) ? -1 : at.x() < b.center().x() ? 0 : 1;
    }

    int hot = -1, glowSide = -1;
    float level = 0;
    QString headline, detail;
    QVariantAnimation glow;
    QTimer idle;
};

// Floating panel with an arrow on its top edge, shown over the window's other
// content. A child widget rather than a real popup window, so the combo boxes
// and dialogs opened from inside it behave normally.
class Popover : public QWidget
{
public:
    static constexpr int Pad = 14 /* room for the shadow */, Arrow = 9;
    int arrowX = 0; // where the arrow's tip sits, in this widget's coordinates

    using QWidget::QWidget;

protected:
    // Swallow clicks on the panel's own background. Unhandled, they would travel
    // on to the window behind and count there as a click outside the popover.
    void mousePressEvent(QMouseEvent *e) override { e->accept(); }
    void paintEvent(QPaintEvent *) override
    {
        const Theme &t = theme();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF body = QRectF(rect()).adjusted(Pad, Pad + Arrow, -Pad, -Pad);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, t.dark ? 16 : 8)); // stacked translucent layers: a soft shadow
        for (int i = Pad - 2; i > 0; i -= 2)
            p.drawRoundedRect(body.adjusted(-i, -i + 2, i, i + 2), 12 + i, 12 + i);
        QPainterPath shape, arrow;
        shape.addRoundedRect(body, 12, 12);
        arrow.moveTo(arrowX - Arrow - 1, body.top() + 1);
        arrow.lineTo(arrowX, body.top() - Arrow);
        arrow.lineTo(arrowX + Arrow + 1, body.top() + 1);
        arrow.closeSubpath();
        p.setPen(QPen(t.border, 1));
        p.setBrush(t.card);
        p.drawPath(shape.united(arrow));
    }
};

#endif // WIDGETS_H
