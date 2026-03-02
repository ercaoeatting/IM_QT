#ifndef SEARCHDIALOG_H
#define SEARCHDIALOG_H

#include "qtmetamacros.h"
#include <QDialog>
#include <utility>

namespace Ui {
class SearchDialog;
}

class SearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit SearchDialog(const std::vector<std::pair<QString, QString>>& history,
                          const QString& peerName, QWidget* parent = nullptr);
    ~SearchDialog();
private slots:

    void on_buttonFind_clicked();

private:
    Ui::SearchDialog*                        ui;
    std::vector<std::pair<QString, QString>> m_history;
};

#endif // SEARCHDIALOG_H
