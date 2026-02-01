#include "ProtonVersionDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QFont>
#include <QVariant>

ProtonVersionDialog::ProtonVersionDialog(const QList<ProtonManager::ProtonRelease>& releases,
                                         const QString& currentVersion,
                                         QWidget* parent)
    : QDialog(parent)
    , m_releases(releases)
    , m_currentVersion(currentVersion)
{
    setupUI();
    populateList();
}

void ProtonVersionDialog::setupUI()
{
    setWindowTitle("Select Proton-CachyOS Version");
    setMinimumWidth(600);
    setMinimumHeight(400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // Header
    m_headerLabel = new QLabel("Select a Proton-CachyOS version to install:", this);
    m_headerLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    mainLayout->addWidget(m_headerLabel);

    if (!m_currentVersion.isEmpty()) {
        QLabel* currentLabel = new QLabel(QString("Currently installed: %1").arg(m_currentVersion), this);
        currentLabel->setStyleSheet("color: #888; margin-bottom: 10px;");
        mainLayout->addWidget(currentLabel);
    }

    // Version list
    m_versionList = new QListWidget(this);
    m_versionList->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_versionList, 1);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
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
    connect(m_installButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_versionList, &QListWidget::itemDoubleClicked, this, &QDialog::accept);

    // Enable/disable install button based on selection
    connect(m_versionList, &QListWidget::itemSelectionChanged, this, [this]() {
        m_installButton->setEnabled(!m_versionList->selectedItems().isEmpty());
    });

    m_installButton->setEnabled(false);
}

void ProtonVersionDialog::populateList()
{
    for (const ProtonManager::ProtonRelease& release : m_releases) {
        // Extract version info from filename
        // Format: proton-cachyos-10.0-20260127-slr-x86_64.tar.xz
        QRegularExpression regex(R"(proton-cachyos-([0-9.]+)-(\d+)-([\w-]+))");
        QRegularExpressionMatch match = regex.match(release.fileName);

        QString displayText;
        QString detailText;

        if (match.hasMatch()) {
            QString version = match.captured(1);      // e.g., "10.0"
            QString date = match.captured(2);         // e.g., "20260127"
            QString variant = match.captured(3);      // e.g., "slr"

            // Format date as YYYY-MM-DD
            if (date.length() == 8) {
                QString year = date.mid(0, 4);
                QString month = date.mid(4, 2);
                QString day = date.mid(6, 2);
                date = QString("%1-%2-%3").arg(year, month, day);
            }

            displayText = QString("Proton %1 (%2)").arg(version, date);
            detailText = QString("Variant: %1  •  Tag: %2").arg(variant.toUpper(), release.version);
        } else {
            displayText = release.version;
            detailText = release.fileName;
        }

        QListWidgetItem* item = new QListWidgetItem(m_versionList);
        item->setText(displayText);
        item->setToolTip(detailText + "\n" + release.fileName);
        item->setData(Qt::UserRole, QVariant::fromValue(release));

        // Add subtitle with details
        QString subtitle = QString("  %1").arg(detailText);
        item->setData(Qt::UserRole + 1, subtitle);

        // Style the first item (latest) differently
        if (m_versionList->count() == 1) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
            item->setText(displayText + "  [Latest]");
        }
    }

    // Select first item by default
    if (m_versionList->count() > 0) {
        m_versionList->setCurrentRow(0);
    }
}

ProtonManager::ProtonRelease ProtonVersionDialog::selectedRelease() const
{
    QListWidgetItem* item = m_versionList->currentItem();
    if (item) {
        return item->data(Qt::UserRole).value<ProtonManager::ProtonRelease>();
    }
    return ProtonManager::ProtonRelease();
}
