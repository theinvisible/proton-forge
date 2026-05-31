#ifndef RECOMMENDATIONSDIALOG_H
#define RECOMMENDATIONSDIALOG_H

#include <QDialog>
#include <QString>
#include <QList>
#include "network/ProtonDBClient.h"
#include "utils/LaunchOptionExtractor.h"

// Shows the ProtonDB tier for a game plus launch-option suggestions mined from
// user reports. Each suggestion has an "Apply" button; clicking it emits
// applySnippet() and closes the dialog. When no suggestions are available an
// explanatory message is shown, but the tier badge and a "View on ProtonDB"
// link are still presented.
class RecommendationsDialog : public QDialog {
    Q_OBJECT

public:
    RecommendationsDialog(const QString& appId,
                          const QString& gameName,
                          const ProtonDBClient::Summary& summary,
                          const QList<LaunchOptionExtractor::Suggestion>& suggestions,
                          const QString& message,
                          QWidget* parent = nullptr);

signals:
    // Emitted with the chosen launch-option snippet when the user clicks Apply.
    void applySnippet(const QString& snippet);
};

#endif // RECOMMENDATIONSDIALOG_H
