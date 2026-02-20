#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QPainter>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void onCategoryChanged();
    void saveSettings();

private:
    void setupUI();
    void loadSettings();
    QWidget* buildGithubPage();
    static QIcon makeCategoryIcon(const QColor& color, const QString& letter);

    QListWidget*    m_categoryList;
    QStackedWidget* m_stack;
    QLineEdit*      m_tokenEdit;
    QPushButton*    m_toggleTokenBtn;
};

#endif // SETTINGSDIALOG_H
