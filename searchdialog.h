#ifndef SEARCHDIALOG_H
#define SEARCHDIALOG_H

#include "qtmetamacros.h"
#include <QDialog>

namespace Ui {
class SearchDialog;
}

class SearchDialog : public QDialog {
    Q_OBJECT

public:
    explicit SearchDialog(const std::vector<QString>& history, const QString& peerName,
                          QWidget* parent = nullptr);
    ~SearchDialog();
private slots:

    void on_buttonFind_clicked();

private:
    Ui::SearchDialog*    ui;
    std::vector<QString> m_history;
};

#endif // SEARCHDIALOG_H
