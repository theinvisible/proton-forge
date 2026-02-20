#ifndef ABOUTDIALOG_H
#define ABOUTDIALOG_H

#include <QDialog>
#include <QTimer>
#include <QWidget>
#include <QList>
#include <QColor>

// Animated "PF" coin that rotates around the Y axis with orbiting stars.
class AnimatedLogoWidget : public QWidget {
    Q_OBJECT

    struct Star {
        float angle;   // current orbit angle (degrees)
        float radius;  // orbit radius
        float size;    // dot radius
        float speed;   // degrees per frame
        QColor color;
    };

public:
    explicit AnimatedLogoWidget(QWidget* parent = nullptr);
    ~AnimatedLogoWidget() override = default;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QTimer*     m_timer;
    float       m_coinAngle = 0.0f;  // Y-rotation of coin (degrees)
    float       m_bobPhase  = 0.0f;  // vertical bobbing phase
    float       m_glowPhase = 0.0f;  // glow pulse phase
    QList<Star> m_stars;
};

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

#endif // ABOUTDIALOG_H
