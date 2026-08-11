#include "AboutDialog.h"
#include "AppStyle.h"
#include "Version.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QPropertyAnimation>
#include <QMouseEvent>
#include <QStringList>
#include <cmath>

static constexpr float PI = 3.14159265f;

// ─────────────────────────────────────────────────────────────────────────────
// AnimatedLogoWidget
// ─────────────────────────────────────────────────────────────────────────────

AnimatedLogoWidget::AnimatedLogoWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedSize(240, 230);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    // Build the ring of orbiting stars with varying colors, radii and speeds
    const QColor colors[3] = { QColor("#76B900"), QColor("#1f6feb"), QColor("#ffffff") };
    for (int i = 0; i < 12; ++i) {
        Star s;
        s.angle  = i * 30.0f;
        s.radius = 92.0f + (i % 3) * 9.0f;
        s.size   = 2.0f + (i % 4) * 0.8f;
        s.speed  = 0.4f + (i % 3) * 0.15f;
        s.color  = colors[i % 3];
        m_stars.append(s);
    }

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        m_coinAngle += 1.4f;
        if (m_coinAngle >= 360.0f) m_coinAngle -= 360.0f;

        m_bobPhase += 0.04f;
        if (m_bobPhase > 2.0f * PI) m_bobPhase -= 2.0f * PI;

        m_glowPhase += 0.035f;
        if (m_glowPhase > 2.0f * PI) m_glowPhase -= 2.0f * PI;

        for (Star& s : m_stars)
            s.angle += s.speed;

        update();
    });
    m_timer->start(16); // ~60 fps
}

void AnimatedLogoWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const float cx   = width()  / 2.0f;
    const float cy   = height() / 2.0f;
    const float R    = 65.0f;
    const float bob  = 5.0f * std::sin(m_bobPhase);
    const float coinCy = cy + bob;

    const float cosY     = std::cos(m_coinAngle * PI / 180.0f);
    const bool  showFront = cosY >= 0.0f;
    const float glowPulse = 0.55f + 0.45f * std::sin(m_glowPhase);

    // ── Stars BEHIND the coin (sin(angle) < 0 → upper half of orbit) ─────────
    for (const Star& s : m_stars) {
        const float rad = s.angle * PI / 180.0f;
        const float sy  = s.radius * std::sin(rad) * 0.38f;
        if (sy >= 0.0f) continue;                      // skip front stars
        const float sx  = s.radius * std::cos(rad);
        const float t   = (-sy) / (s.radius * 0.38f);  // 0..1, deeper = more opaque
        QColor c = s.color;
        c.setAlphaF(0.25f + 0.35f * t);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(cx + sx, coinCy + sy), s.size * 0.7f, s.size * 0.7f);
    }

    // ── Soft outer glow (radial gradient, colour follows coin face) ───────────
    {
        const float glowR = R * 2.1f;
        QRadialGradient g(QPointF(cx, coinCy), glowR);
        QColor gc = showFront ? QColor("#76B900") : QColor("#1f6feb");
        gc.setAlphaF(0.18f * glowPulse);
        g.setColorAt(0.0, gc);
        gc.setAlphaF(0.0f);
        g.setColorAt(1.0, gc);
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(cx, coinCy), glowR, glowR);
    }

    // ── Coin ──────────────────────────────────────────────────────────────────
    p.save();
    p.translate(cx, coinCy);

    if (std::abs(cosY) > 0.012f) {
        p.scale(cosY, 1.0f);  // negative cosY mirrors → automatic back-face flip

        // Body gradient
        QRadialGradient bodyGrad(QPointF(0.0f, -R * 0.38f), R * 1.9f);
        if (showFront) {
            bodyGrad.setColorAt(0.0, QColor("#c4f000"));
            bodyGrad.setColorAt(0.5, QColor("#76B900"));
            bodyGrad.setColorAt(1.0, QColor("#1a2e00"));
        } else {
            bodyGrad.setColorAt(0.0, QColor("#80c8ff"));
            bodyGrad.setColorAt(0.5, QColor("#1f6feb"));
            bodyGrad.setColorAt(1.0, QColor("#04102a"));
        }
        p.setBrush(bodyGrad);
        p.setPen(QPen(QColor(255, 255, 255, 35), 1.5f));
        p.drawEllipse(QPointF(0, 0), R, R);

        // Top-half highlight (lens flare feel)
        QPainterPath hlPath;
        hlPath.moveTo(-R, 0);
        hlPath.arcTo(-R, -R, 2 * R, 2 * R, 0, 180);
        hlPath.closeSubpath();
        QLinearGradient hlGrad(0, -R, 0, 0);
        hlGrad.setColorAt(0.0, QColor(255, 255, 255, 55));
        hlGrad.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setBrush(hlGrad);
        p.setPen(Qt::NoPen);
        p.drawPath(hlPath);

        // "PF" text — also projected (cosY scale), which is correct for a 3D disc
        QFont font;
        font.setPixelSize(46);
        font.setBold(true);
        p.setFont(font);

        // Drop shadow
        p.setPen(QColor(0, 0, 0, 110));
        p.drawText(QRectF(-R + 2, -R + 2, 2 * R, 2 * R), Qt::AlignCenter, "PF");
        // Main text
        p.setPen(Qt::white);
        p.drawText(QRectF(-R, -R, 2 * R, 2 * R), Qt::AlignCenter, "PF");

    } else {
        // Edge-on: thin coloured line
        p.setPen(QPen(showFront ? QColor("#76B900") : QColor("#1f6feb"), 3));
        p.drawLine(QPointF(-R, 0), QPointF(R, 0));
    }

    p.restore();

    // ── Stars IN FRONT of the coin (sin(angle) >= 0 → lower half) ─────────────
    for (const Star& s : m_stars) {
        const float rad = s.angle * PI / 180.0f;
        const float sy  = s.radius * std::sin(rad) * 0.38f;
        if (sy < 0.0f) continue;                       // skip back stars
        const float sx  = s.radius * std::cos(rad);
        const float t   = sy / (s.radius * 0.38f);     // 0..1
        QColor c = s.color;
        c.setAlphaF(0.45f + 0.55f * t);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(QPointF(cx + sx, coinCy + sy), s.size, s.size);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CardCarousel
// ─────────────────────────────────────────────────────────────────────────────

CardCarousel::CardCarousel(QWidget* parent)
    : QWidget(parent)
{
    m_strip = new QWidget(this);       // clipped to our rect, so off-card content hides
    m_strip->move(0, 0);

    m_anim = new QPropertyAnimation(m_strip, "pos", this);
    m_anim->setDuration(220);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);

    setCursor(Qt::OpenHandCursor);
}

void CardCarousel::addCard(QWidget* card)
{
    card->setParent(m_strip);
    card->show();
    m_cards.append(card);
    layoutCards();
    updateGeometry();
}

QSize CardCarousel::sizeHint() const
{
    QSize hint(0, 0);
    for (QWidget* card : m_cards)
        hint = hint.expandedTo(card->sizeHint());
    return hint;
}

void CardCarousel::resizeEvent(QResizeEvent*)
{
    layoutCards();
}

void CardCarousel::layoutCards()
{
    if (m_cards.isEmpty())
        return;

    const int w = width();
    const int h = height();
    m_strip->resize(w * m_cards.size(), h);
    for (int i = 0; i < m_cards.size(); ++i)
        m_cards[i]->setGeometry(i * w, 0, w, h);

    if (m_anim->state() != QAbstractAnimation::Running && !m_dragging)
        m_strip->move(-m_index * w, 0);
}

void CardCarousel::setCurrentIndex(int index, bool animate)
{
    index = qBound(0, index, qMax(0, m_cards.size() - 1));
    const int target = -index * width();

    m_anim->stop();
    if (animate && isVisible()) {
        m_anim->setStartValue(m_strip->pos());
        m_anim->setEndValue(QPoint(target, 0));
        m_anim->start();
    } else {
        m_strip->move(target, 0);
    }

    if (index != m_index) {
        m_index = index;
        emit currentChanged(m_index);
    }
}

void CardCarousel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_cards.size() < 2) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_anim->stop();
    m_dragging    = true;
    m_pressX      = event->position().x();
    m_pressOffset = m_strip->x();
    setCursor(Qt::ClosedHandCursor);
    emit touched();
    event->accept();
}

void CardCarousel::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const int minX = -(m_cards.size() - 1) * width();
    const int dx   = int(event->position().x()) - m_pressX;
    int x = m_pressOffset + dx;
    // Past the first/last card the strip still moves, but sluggishly, so the
    // end of the strip is felt rather than hit as a wall.
    if (x > 0)         x /= 4;
    else if (x < minX) x = minX + (x - minX) / 4;
    m_strip->move(x, 0);
    event->accept();
}

void CardCarousel::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_dragging) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    m_dragging = false;
    setCursor(Qt::OpenHandCursor);

    const int dx        = int(event->position().x()) - m_pressX;
    const int threshold = qMax(24, width() / 6);
    int index = m_index;
    if (dx <= -threshold)      ++index;
    else if (dx >= threshold)  --index;

    setCurrentIndex(index);   // clamps, and snaps back when the drag was short
    emit touched();
    event->accept();
}

// ─────────────────────────────────────────────────────────────────────────────
// AboutDialog
// ─────────────────────────────────────────────────────────────────────────────

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("About ProtonForge");
    setFixedWidth(420);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setStyleSheet(AppStyle::dialogButtonStyle());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 16);
    root->setSpacing(0);

    // ── Animated logo ──────────────────────────────────────────────────────
    auto* logoArea = new QWidget;
    logoArea->setStyleSheet("background: #0d0d0d;");
    auto* logoLayout = new QHBoxLayout(logoArea);
    logoLayout->setContentsMargins(0, 16, 0, 8);
    auto* logo = new AnimatedLogoWidget;
    logoLayout->addStretch();
    logoLayout->addWidget(logo);
    logoLayout->addStretch();
    root->addWidget(logoArea);

    // ── Title ──────────────────────────────────────────────────────────────
    auto* titleLabel = new QLabel(QString("ProtonForge <span style='color:#76B900;'>v%1</span>").arg(APP_VERSION));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet(
        "font-size: 20px; font-weight: bold; color: #e0e0e0;"
        "background: #0d0d0d; padding-bottom: 4px;");
    root->addWidget(titleLabel);

    // ── Tagline ────────────────────────────────────────────────────────────
    auto* tagLabel = new QLabel("Forge the perfect settings for your games.");
    tagLabel->setAlignment(Qt::AlignCenter);
    tagLabel->setStyleSheet(
        "font-size: 11px; color: #888; font-style: italic;"
        "background: #0d0d0d; padding-bottom: 14px;");
    root->addWidget(tagLabel);

    // ── Separator ──────────────────────────────────────────────────────────
    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("background: #2a2a2a; border: none; max-height: 1px;");
    root->addWidget(sep);

    // ── Body ───────────────────────────────────────────────────────────────
    auto* body = new QWidget;
    body->setStyleSheet("background: #121212;");
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(28, 16, 28, 16);
    bodyLayout->setSpacing(12);

    // Description
    auto* descLabel = new QLabel(
        "Fine-tune DLSS Super Resolution, Ray Reconstruction &amp; Frame Generation.<br>"
        "Manage Proton-CachyOS and Proton-GE. Unleash HDR.<br>"
        "Steam and GOG in one library. Your games, your rules.");
    descLabel->setWordWrap(true);
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setStyleSheet("color: #bbb; font-size: 11px;");
    bodyLayout->addWidget(descLabel);

    // ── Info cards, paged ──────────────────────────────────────────────
    auto makeCard = [](const QString& title, const QString& items) {
        auto* frame = new QFrame;
        frame->setStyleSheet(
            "QFrame { background: #1c1c1c; border: 1px solid #333;"
            "         border-radius: 6px; padding: 10px; }");
        auto* layout = new QVBoxLayout(frame);
        layout->setSpacing(1);
        layout->setContentsMargins(8, 8, 8, 8);

        auto* titleLabel = new QLabel(title);
        titleLabel->setStyleSheet(
            "color: #76B900; font-size: 10px; font-weight: bold; border: none;");
        layout->addWidget(titleLabel);

        auto* itemsLabel = new QLabel(items);
        itemsLabel->setStyleSheet(
            "color: #777; font-size: 10px; font-family: monospace; border: none;");
        layout->addWidget(itemsLabel);
        return frame;
    };

    m_cards = new CardCarousel;
    m_cards->addCard(makeCard("🎮  WHAT IT DOES",
        "├─ DLSS 4 presets, Reflex &amp; Smooth Motion<br>"
        "├─ Compatibility gating per driver &amp; Proton<br>"
        "├─ HDR on Wayland, KDE Plasma &amp; Gnome<br>"
        "├─ Steam and GOG in one game library<br>"
        "├─ GOG: sign in, download, update, launch<br>"
        "└─ Credentials in your system keyring"));
    m_cards->addCard(makeCard("⚡  POWERED BY",
        "├─ NVIDIA DLSS Technology<br>"
        "├─ Proton-CachyOS &amp; Proton-GE<br>"
        "├─ GOG Content System &amp; Galaxy API<br>"
        "├─ ProtonDB · MangoHud · NVML<br>"
        "├─ Qt6 Framework<br>"
        "└─ The Linux Gaming Community"));
    bodyLayout->addWidget(m_cards);

    // Pager row: ‹ · dots · ›
    const QString navStyle =
        "QPushButton { background: transparent; border: none; color: #666;"
        "              font-size: 14px; padding: 0 6px; min-width: 18px; }"
        "QPushButton:hover { color: #76B900; }";

    auto* prevBtn = new QPushButton("‹");
    prevBtn->setStyleSheet(navStyle);
    prevBtn->setCursor(Qt::PointingHandCursor);
    prevBtn->setFocusPolicy(Qt::NoFocus);
    prevBtn->setToolTip("Previous");

    auto* nextBtn = new QPushButton("›");
    nextBtn->setStyleSheet(navStyle);
    nextBtn->setCursor(Qt::PointingHandCursor);
    nextBtn->setFocusPolicy(Qt::NoFocus);
    nextBtn->setToolTip("Next");

    m_cardDots = new QLabel;
    m_cardDots->setAlignment(Qt::AlignCenter);
    m_cardDots->setStyleSheet("font-size: 10px;");
    m_cardDots->setToolTip("Drag the card sideways to switch");

    auto* pagerRow = new QHBoxLayout;
    pagerRow->setContentsMargins(0, 0, 0, 0);
    pagerRow->setSpacing(0);
    pagerRow->addStretch();
    pagerRow->addWidget(prevBtn);
    pagerRow->addWidget(m_cardDots);
    pagerRow->addWidget(nextBtn);
    pagerRow->addStretch();
    bodyLayout->addLayout(pagerRow);

    const int cardCount = m_cards->count();
    connect(m_cards, &CardCarousel::currentChanged, this, &AboutDialog::updateDots);

    auto step = [this, cardCount](int delta) {
        m_cards->setCurrentIndex((m_cards->currentIndex() + delta + cardCount) % cardCount);
        m_cardTimer->start();   // manual paging restarts the dwell time
    };
    connect(prevBtn, &QPushButton::clicked, this, [step]() { step(-1); });
    connect(nextBtn, &QPushButton::clicked, this, [step]() { step(+1); });

    m_cardTimer = new QTimer(this);
    m_cardTimer->setInterval(6000);
    connect(m_cardTimer, &QTimer::timeout, this, [this, cardCount]() {
        m_cards->setCurrentIndex((m_cards->currentIndex() + 1) % cardCount);
    });
    connect(m_cards, &CardCarousel::touched, m_cardTimer, [this]() { m_cardTimer->start(); });
    m_cardTimer->start();

    updateDots(0);

    // Stats line
    auto* statsLabel = new QLabel(
        "🚀 FPS: Unlimited &nbsp;|&nbsp; Ray Tracing: On &nbsp;|&nbsp; Quality: Ultra");
    statsLabel->setTextFormat(Qt::RichText);   // entities alone don't trigger auto-detection
    statsLabel->setAlignment(Qt::AlignCenter);
    statsLabel->setStyleSheet("color: #555; font-size: 10px;");
    bodyLayout->addWidget(statsLabel);

    root->addWidget(body);

    // ── Footer ─────────────────────────────────────────────────────────────
    auto* footer = new QLabel(
        "Made with ❤️ for gamers who refuse to compromise<br>"
        "<span style='color:#444;'>MIT License &nbsp;·&nbsp; "
        "github.com/theinvisible/proton-forge</span>");
    footer->setAlignment(Qt::AlignCenter);
    footer->setStyleSheet("color: #555; font-size: 9px; padding: 8px 0 0 0;");
    root->addWidget(footer);

    // ── Close button ───────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(0, 0, 16, 0);
    btnRow->addStretch();
    auto* closeBtn = new QPushButton("Close");
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);
}

void AboutDialog::updateDots(int index)
{
    QStringList dots;
    for (int i = 0; i < m_cards->count(); ++i)
        dots << QString("<span style='color:%1;'>●</span>")
                    .arg(i == index ? "#76B900" : "#3a3a3a");
    m_cardDots->setText(dots.join("&nbsp;"));
}
