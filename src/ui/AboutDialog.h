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

class QLabel;
class QPropertyAnimation;

// Horizontal strip of equally sized cards showing one at a time. Unlike a
// QStackedWidget it can be dragged with the mouse: the strip follows the cursor
// and snaps to the nearest card on release.
class CardCarousel : public QWidget {
    Q_OBJECT
public:
    explicit CardCarousel(QWidget* parent = nullptr);

    void addCard(QWidget* card);
    int  count() const { return m_cards.size(); }
    int  currentIndex() const { return m_index; }
    void setCurrentIndex(int index, bool animate = true);

    QSize sizeHint() const override;

signals:
    void currentChanged(int index);
    void touched();   // driven by hand — callers restart their auto-advance dwell

protected:
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    void layoutCards();

    QWidget*            m_strip;
    QList<QWidget*>     m_cards;
    QPropertyAnimation* m_anim;
    int                 m_index       = 0;
    int                 m_pressX      = 0;
    int                 m_pressOffset = 0;
    bool                m_dragging    = false;
};

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);

private:
    // The info cards share one slot and are paged through, so the dialog stays
    // short no matter how many features get listed.
    void updateDots(int index);

    CardCarousel* m_cards     = nullptr;
    QLabel*       m_cardDots  = nullptr;
    QTimer*       m_cardTimer = nullptr;
};

#endif // ABOUTDIALOG_H
