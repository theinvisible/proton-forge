#include "AboutDialog.h"
#include "Version.h"
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
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
// AboutDialog
// ─────────────────────────────────────────────────────────────────────────────

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("About ProtonForge");
    setFixedWidth(420);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    setStyleSheet(
        "QPushButton {"
        "    background-color: #333333;"
        "    color: #cccccc;"
        "    border: 1px solid #555555;"
        "    padding: 6px 16px;"
        "    border-radius: 4px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #404040;"
        "    border: 1px solid #76B900;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #2a2a2a;"
        "}");

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
        "Manage Proton-CachyOS and Proton-GE. Unleash HDR. Your games, your rules.");
    descLabel->setWordWrap(true);
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setStyleSheet("color: #bbb; font-size: 11px;");
    bodyLayout->addWidget(descLabel);

    // Powered by card
    auto* poweredFrame = new QFrame;
    poweredFrame->setStyleSheet(
        "QFrame { background: #1c1c1c; border: 1px solid #333;"
        "         border-radius: 6px; padding: 10px; }");
    auto* poweredLayout = new QVBoxLayout(poweredFrame);
    poweredLayout->setSpacing(1);
    poweredLayout->setContentsMargins(8, 8, 8, 8);

    auto* poweredTitle = new QLabel("⚡  POWERED BY");
    poweredTitle->setStyleSheet("color: #76B900; font-size: 10px; font-weight: bold; border: none;");
    poweredLayout->addWidget(poweredTitle);

    auto* itemsLabel = new QLabel(
        "├─ NVIDIA DLSS Technology<br>"
        "├─ Proton-CachyOS &amp; Proton-GE<br>"
        "├─ Qt6 Framework<br>"
        "└─ The Linux Gaming Community");
    itemsLabel->setStyleSheet("color: #777; font-size: 10px; font-family: monospace; border: none;");
    poweredLayout->addWidget(itemsLabel);
    bodyLayout->addWidget(poweredFrame);

    // Stats line
    auto* statsLabel = new QLabel(
        "🚀 FPS: Unlimited &nbsp;|&nbsp; Ray Tracing: On &nbsp;|&nbsp; Quality: Ultra");
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
