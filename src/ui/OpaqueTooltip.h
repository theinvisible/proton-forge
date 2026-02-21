#pragma once

#include <QObject>
#include <QLabel>
#include <QTimer>
#include <QEvent>
#include <QHelpEvent>
#include <QWidget>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>

// Application-wide event filter that replaces Qt's native tooltips
// with a custom opaque QLabel window to avoid compositor transparency issues.
class OpaqueTooltip : public QObject {
public:
    static OpaqueTooltip &instance() {
        static OpaqueTooltip inst;
        return inst;
    }

    void install(QApplication *app) {
        app->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::ToolTip) {
            auto *helpEvent = static_cast<QHelpEvent *>(event);
            auto *widget = qobject_cast<QWidget *>(obj);
            if (widget && !widget->toolTip().isEmpty()) {
                showTooltip(widget->toolTip(), helpEvent->globalPos());
                event->accept();
                return true; // prevent native tooltip
            }
        }
        if (event->type() == QEvent::Leave ||
            event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::WindowDeactivate ||
            event->type() == QEvent::FocusOut) {
            hideTooltip();
        }
        return QObject::eventFilter(obj, event);
    }

private:
    OpaqueTooltip() {
        m_label = new QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::BypassWindowManagerHint);
        m_label->setAutoFillBackground(true);
        m_label->setWordWrap(true);
        m_label->setMaximumWidth(400);
        m_label->setTextFormat(Qt::PlainText);
        m_label->setMargin(6);
        m_label->setStyleSheet(
            "background-color: #2a2a2a;"
            "color: #e0e0e0;"
            "border: 1px solid #555;"
            "padding: 4px 8px;"
            "font-size: 12px;"
        );
        m_label->setAttribute(Qt::WA_TranslucentBackground, false);

        m_hideTimer = new QTimer(m_label);
        m_hideTimer->setSingleShot(true);
        m_hideTimer->setInterval(10000);
        QObject::connect(m_hideTimer, &QTimer::timeout, m_label, [this]() {
            hideTooltip();
        });
    }

    void showTooltip(const QString &text, const QPoint &globalPos) {
        m_label->setText(text);
        m_label->adjustSize();

        // Position tooltip near cursor, keeping it on screen
        QPoint pos = globalPos + QPoint(16, 16);
        QScreen *screen = QGuiApplication::screenAt(globalPos);
        if (screen) {
            QRect screenRect = screen->availableGeometry();
            if (pos.x() + m_label->width() > screenRect.right())
                pos.setX(globalPos.x() - m_label->width() - 4);
            if (pos.y() + m_label->height() > screenRect.bottom())
                pos.setY(globalPos.y() - m_label->height() - 4);
        }

        m_label->move(pos);
        m_label->show();
        m_hideTimer->start();
    }

    void hideTooltip() {
        m_label->hide();
        m_hideTimer->stop();
    }

    QLabel *m_label = nullptr;
    QTimer *m_hideTimer = nullptr;
};
