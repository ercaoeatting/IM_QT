#pragma once
#include "chatclient.h"
#include <QDialog>
#include <QProgressDialog>
#include <QSettings>
QT_BEGIN_NAMESPACE
namespace Ui {
class Dialog;
}
QT_END_NAMESPACE

class Dialog : public QDialog {
    Q_OBJECT
public:
    explicit Dialog(QWidget* parent = nullptr);
    ~Dialog();

    QString     host() const;
    quint16     port() const;
    uint32_t    userId() const;
    QString     userName() const;
    ChatClient* m_client;

private slots:
    void on_buttonLogin_clicked();
    void on_buttonExit_clicked();

private:
    Ui::Dialog*      ui;
    QProgressDialog* loginin = nullptr;
};