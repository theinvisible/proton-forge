#include "ProtonVersionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QFont>
#include <QVariant>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QCloseEvent>

ProtonVersionDialog::ProtonVersionDialog(const QList<ProtonManager::ProtonRelease>& releases,
                                         const QString& currentVersion,
                                         QWidget* parent)
    : QDialog(parent)
    , m_releases(releases)
    , m_currentVersion(currentVersion)
{
    m_installedVersions = getInstalledVersions();
    setupUI();
    populateVariants();
}

// ---------------------------------------------------------------------------
// Static helper: draw a filled circle with a centered letter
// ---------------------------------------------------------------------------
QIcon ProtonVersionDialog::makeVariantIcon(const QColor& color, const QString& letter)
{
    QPixmap pixmap(36, 36);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(1, 1, 34, 34);

    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(18);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(QRect(0, 0, 36, 36), Qt::AlignCenter, letter);

    return QIcon(pixmap);
}

void ProtonVersionDialog::setupUI()
{
    setWindowTitle("Select Proton Version");
    resize(1100, 750);
    setMinimumSize(850, 580);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Three-panel splitter
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // Left panel – variant selector
    QWidget* leftPanel = new QWidget(splitter);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* variantLabel = new QLabel("Proton Variant", leftPanel);
    variantLabel->setStyleSheet("font-weight: bold; font-size: 13px; margin-bottom: 4px;");
    m_variantList = new QListWidget(leftPanel);
    m_variantList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_variantList->setStyleSheet(
        "QListWidget { border: 1px solid #444; border-radius: 6px; background: #1e1e1e; padding: 4px; }"
        "QListWidget::item { padding: 6px 10px; border-radius: 4px; margin: 2px 4px; font-size: 13px; font-weight: bold; }"
        "QListWidget::item:selected { background: #1e3a0a; color: #9dff00; border: 1px solid #77c71f; }"
        "QListWidget::item:hover:!selected { background: #2a2a2a; }"
    );
    leftLayout->addWidget(variantLabel);
    leftLayout->addWidget(m_variantList);
    leftPanel->setMinimumWidth(160);
    splitter->addWidget(leftPanel);

    // Middle panel – version list
    QWidget* midPanel = new QWidget(splitter);
    QVBoxLayout* midLayout = new QVBoxLayout(midPanel);
    midLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* versionLabel = new QLabel("Versions", midPanel);
    versionLabel->setStyleSheet("font-weight: bold; font-size: 13px; margin-bottom: 4px;");
    QLabel* infoLabel = new QLabel("✓ Installed versions are marked in green", midPanel);
    infoLabel->setStyleSheet("font-size: 11px; color: #888; margin-bottom: 4px;");
    m_versionList = new QListWidget(midPanel);
    m_versionList->setSelectionMode(QAbstractItemView::SingleSelection);
    midLayout->addWidget(versionLabel);
    midLayout->addWidget(infoLabel);
    midLayout->addWidget(m_versionList);
    midPanel->setMinimumWidth(220);
    splitter->addWidget(midPanel);

    // Right panel – changelog
    QWidget* rightPanel = new QWidget(splitter);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* changelogLabel = new QLabel("Changelog", rightPanel);
    changelogLabel->setStyleSheet("font-weight: bold; font-size: 13px; margin-bottom: 4px;");
    m_changelogView = new QTextBrowser(rightPanel);
    m_changelogView->setOpenExternalLinks(true);
    rightLayout->addWidget(changelogLabel);
    rightLayout->addWidget(m_changelogView);
    rightPanel->setMinimumWidth(300);
    splitter->addWidget(rightPanel);

    // Proportions: left 1, middle 2, right 3
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setStretchFactor(2, 3);

    mainLayout->addWidget(splitter, 1);

    // Progress panel (initially hidden)
    m_progressFrame = new QFrame(this);
    m_progressFrame->setFrameShape(QFrame::StyledPanel);
    m_progressFrame->setStyleSheet("QFrame { border: 1px solid #555; border-radius: 6px; padding: 8px; }");
    m_progressFrame->setVisible(false);

    QVBoxLayout* progressLayout = new QVBoxLayout(m_progressFrame);
    progressLayout->setContentsMargins(12, 8, 12, 8);
    progressLayout->setSpacing(6);

    m_progressPhaseLabel = new QLabel(m_progressFrame);
    QFont boldFont = m_progressPhaseLabel->font();
    boldFont.setBold(true);
    boldFont.setPointSize(boldFont.pointSize() + 1);
    m_progressPhaseLabel->setFont(boldFont);

    m_progressDetailLabel = new QLabel(m_progressFrame);
    m_progressDetailLabel->setStyleSheet("color: #aaa; font-size: 11px;");

    m_progressBar = new QProgressBar(m_progressFrame);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);

    progressLayout->addWidget(m_progressPhaseLabel);
    progressLayout->addWidget(m_progressDetailLabel);
    progressLayout->addWidget(m_progressBar);

    mainLayout->addWidget(m_progressFrame);

    // Button row
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    m_deleteButton = new QPushButton("Delete Selected", this);
    m_deleteButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; padding: 8px 16px; }");
    buttonLayout->addWidget(m_deleteButton);

    buttonLayout->addStretch();

    m_cancelButton = new QPushButton("Cancel", this);
    buttonLayout->addWidget(m_cancelButton);

    m_installButton = new QPushButton("Install Selected", this);
    m_installButton->setDefault(true);
    m_installButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 8px 16px; }");
    buttonLayout->addWidget(m_installButton);

    mainLayout->addLayout(buttonLayout);

    // Connections
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_installButton, &QPushButton::clicked, this, &ProtonVersionDialog::startInstallation);
    connect(m_deleteButton, &QPushButton::clicked, this, &ProtonVersionDialog::deleteSelectedVersion);
    connect(m_versionList, &QListWidget::itemDoubleClicked, this, &ProtonVersionDialog::startInstallation);
    connect(m_versionList, &QListWidget::itemSelectionChanged, this, &ProtonVersionDialog::updateButtonStates);
    connect(m_versionList, &QListWidget::itemSelectionChanged, this, &ProtonVersionDialog::updateChangelog);
    connect(m_variantList, &QListWidget::itemSelectionChanged, this, &ProtonVersionDialog::onVariantSelected);

    m_installButton->setEnabled(false);
    m_deleteButton->setEnabled(false);
}

void ProtonVersionDialog::populateVariants()
{
    m_variantList->setIconSize(QSize(36, 36));

    QListWidgetItem* cachyItem = new QListWidgetItem("Proton-CachyOS", m_variantList);
    cachyItem->setIcon(makeVariantIcon(QColor("#77c71f"), "C"));
    cachyItem->setSizeHint(QSize(0, 56));
    cachyItem->setData(Qt::UserRole, QVariant::fromValue(static_cast<int>(ProtonManager::ProtonCachyOS)));

    QListWidgetItem* geItem = new QListWidgetItem("Proton-GE", m_variantList);
    geItem->setIcon(makeVariantIcon(QColor("#e85d04"), "G"));
    geItem->setSizeHint(QSize(0, 56));
    geItem->setData(Qt::UserRole, QVariant::fromValue(static_cast<int>(ProtonManager::ProtonGE)));

    m_variantList->setCurrentRow(0);
    // onVariantSelected is triggered via signal
}

void ProtonVersionDialog::onVariantSelected()
{
    QListWidgetItem* item = m_variantList->currentItem();
    if (!item) {
        return;
    }
    m_selectedType = static_cast<ProtonManager::ProtonType>(item->data(Qt::UserRole).toInt());
    m_changelogView->clear();
    updateVersionList();
}

void ProtonVersionDialog::updateVersionList()
{
    m_versionList->clear();

    bool isFirst = true;
    for (const ProtonManager::ProtonRelease& release : m_releases) {
        if (release.type != m_selectedType) {
            continue;
        }

        bool installed = isVersionInstalled(release);
        QString displayText;

        if (release.type == ProtonManager::ProtonCachyOS) {
            QRegularExpression regex(R"(proton-cachyos-([0-9.]+)-(\d+)-([\w-]+))");
            QRegularExpressionMatch match = regex.match(release.fileName);

            if (match.hasMatch()) {
                QString version = match.captured(1);
                QString date = match.captured(2);

                if (date.length() == 8) {
                    date = date.mid(0, 4) + "-" + date.mid(4, 2) + "-" + date.mid(6, 2);
                }

                displayText = QString("Proton %1 (%2)").arg(version, date);
            } else {
                displayText = release.version;
            }
        } else {
            displayText = release.version;
        }

        if (installed) {
            displayText = QString("✓ ") + displayText;
        }

        if (isFirst) {
            displayText += "  [Latest]";
        }

        QListWidgetItem* item = new QListWidgetItem(displayText, m_versionList);
        item->setData(Qt::UserRole, QVariant::fromValue(release));

        if (isFirst) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }

        if (installed) {
            item->setForeground(QColor(100, 255, 100));
        }

        isFirst = false;
    }

    if (m_versionList->count() > 0) {
        m_versionList->setCurrentRow(0);
        // updateChangelog is triggered via signal
    }
}

void ProtonVersionDialog::updateChangelog()
{
    QListWidgetItem* item = m_versionList->currentItem();
    if (!item) {
        m_changelogView->clear();
        return;
    }

    ProtonManager::ProtonRelease release = item->data(Qt::UserRole).value<ProtonManager::ProtonRelease>();

    QString html;
    html += QString("<h3>%1</h3>").arg(release.displayName.isEmpty() ? release.version : release.displayName);

    if (release.changelog.isEmpty()) {
        html += "<p><i>No changelog available.</i></p>";
    } else {
        QString body = release.changelog.toHtmlEscaped();
        body.replace("\r\n", "\n");

        // Markdown links [text](url) → <a href="url">text</a>
        body.replace(QRegularExpression(R"(\[([^\]\n]+)\]\((https?://[^\s)]+)\))"),
                     R"(<a href="\2">\1</a>)");

        // Plain URLs not already inside an href="..." attribute
        // Negative lookbehind for the fixed 6-char sequence href="
        body.replace(QRegularExpression(R"((?<!href=")(https?://[^\s<>\[\]"]+))"),
                     R"(<a href="\1">\1</a>)");

        body.replace("\n", "<br>");
        html += body;
    }

    m_changelogView->setHtml(html);
}

ProtonManager::ProtonRelease ProtonVersionDialog::selectedRelease() const
{
    QListWidgetItem* item = m_versionList->currentItem();
    if (item) {
        return item->data(Qt::UserRole).value<ProtonManager::ProtonRelease>();
    }
    return ProtonManager::ProtonRelease();
}

QStringList ProtonVersionDialog::getInstalledVersions() const
{
    QStringList installed;
    QString path = ProtonManager::protonCachyOSPath();
    QDir dir(path);

    if (!dir.exists()) {
        return installed;
    }

    QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        QString protonExe = path + "/" + entry + "/proton";
        if (QFile::exists(protonExe)) {
            installed << entry;
        }
    }

    return installed;
}

bool ProtonVersionDialog::isVersionInstalled(const ProtonManager::ProtonRelease& release) const
{
    QString dirName = release.fileName;

    if (dirName.endsWith(".tar.xz")) {
        dirName.chop(7);
    } else if (dirName.endsWith(".tar.gz")) {
        dirName.chop(7);
    }

    for (const QString& installed : m_installedVersions) {
        if (installed == dirName || installed.startsWith(dirName)) {
            return true;
        }
    }

    return false;
}

void ProtonVersionDialog::deleteSelectedVersion()
{
    QListWidgetItem* item = m_versionList->currentItem();
    if (!item) {
        return;
    }

    ProtonManager::ProtonRelease release = item->data(Qt::UserRole).value<ProtonManager::ProtonRelease>();

    if (!isVersionInstalled(release)) {
        QMessageBox::warning(this, "Cannot Delete",
                           "This version is not installed and cannot be deleted.");
        return;
    }

    QString versionName = release.version;
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Confirm Deletion",
        QString("Are you sure you want to delete %1?\n\nThis will permanently remove all files for this Proton version.").arg(versionName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (reply != QMessageBox::Yes) {
        return;
    }

    bool success = ProtonManager::instance().deleteProtonVersion(release);

    if (success) {
        QMessageBox::information(this, "Success",
                                QString("%1 has been deleted successfully.").arg(versionName));

        m_installedVersions = getInstalledVersions();
        updateVersionList();
    } else {
        QMessageBox::critical(this, "Error",
                            QString("Failed to delete %1.\n\nPlease check file permissions.").arg(versionName));
    }
}

void ProtonVersionDialog::updateButtonStates()
{
    if (m_installing) {
        return;
    }

    QListWidgetItem* item = m_versionList->currentItem();
    bool hasSelection = item != nullptr;

    if (hasSelection) {
        ProtonManager::ProtonRelease release = item->data(Qt::UserRole).value<ProtonManager::ProtonRelease>();
        bool installed = isVersionInstalled(release);

        m_installButton->setEnabled(true);
        m_deleteButton->setEnabled(installed);
    } else {
        m_installButton->setEnabled(false);
        m_deleteButton->setEnabled(false);
    }
}

// ---------------------------------------------------------------------------
// Installation workflow
// ---------------------------------------------------------------------------

void ProtonVersionDialog::startInstallation()
{
    ProtonManager::ProtonRelease release = selectedRelease();
    if (release.downloadUrl.isEmpty()) {
        return;
    }

    m_installing = true;

    // Disable action buttons
    m_installButton->setEnabled(false);
    m_deleteButton->setEnabled(false);
    m_cancelButton->setEnabled(false);

    // Show progress panel
    m_progressFrame->setStyleSheet("QFrame { border: 1px solid #555; border-radius: 6px; padding: 8px; }");
    m_progressPhaseLabel->setText("Download");
    m_progressDetailLabel->setText("Preparing...");
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressFrame->setVisible(true);

    // Connect ProtonManager signals
    ProtonManager& pm = ProtonManager::instance();
    connect(&pm, &ProtonManager::downloadProgress,
            this, &ProtonVersionDialog::onDownloadProgress, Qt::UniqueConnection);
    connect(&pm, &ProtonManager::extractionStarted,
            this, &ProtonVersionDialog::onExtractionStarted, Qt::UniqueConnection);
    connect(&pm, &ProtonManager::installationComplete,
            this, &ProtonVersionDialog::onInstallationComplete, Qt::UniqueConnection);

    pm.installProtonCachyOS(release);
}

void ProtonVersionDialog::onDownloadProgress(qint64 received, qint64 total, const QString& name)
{
    m_progressPhaseLabel->setText("Download");
    if (total > 0) {
        int percent = static_cast<int>(received * 100 / total);
        double mb = received / (1024.0 * 1024.0);
        double totalMb = total / (1024.0 * 1024.0);
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(percent);
        m_progressDetailLabel->setText(
            QString("%1 – %2% (%3 / %4 MB)")
                .arg(name)
                .arg(percent)
                .arg(mb, 0, 'f', 1)
                .arg(totalMb, 0, 'f', 1)
        );
    }
}

void ProtonVersionDialog::onExtractionStarted()
{
    m_progressPhaseLabel->setText("Extracting");
    m_progressDetailLabel->setText("Extracting archive...");
    m_progressBar->setRange(0, 0);  // indeterminate
}

void ProtonVersionDialog::onInstallationComplete(bool success, const QString& message)
{
    // Disconnect ProtonManager signals
    ProtonManager& pm = ProtonManager::instance();
    disconnect(&pm, &ProtonManager::downloadProgress, this, &ProtonVersionDialog::onDownloadProgress);
    disconnect(&pm, &ProtonManager::extractionStarted, this, &ProtonVersionDialog::onExtractionStarted);
    disconnect(&pm, &ProtonManager::installationComplete, this, &ProtonVersionDialog::onInstallationComplete);

    m_installing = false;

    // Hide progress panel and restore buttons
    m_progressFrame->setVisible(false);
    m_cancelButton->setEnabled(true);

    // Refresh installed version list
    m_installedVersions = getInstalledVersions();
    updateVersionList();
    updateButtonStates();

    if (!success) {
        QMessageBox::warning(this, "Installation Failed", message);
    }
}

void ProtonVersionDialog::closeEvent(QCloseEvent* event)
{
    if (m_installing) {
        event->ignore();
    } else {
        QDialog::closeEvent(event);
    }
}
