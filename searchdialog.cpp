#include "searchdialog.h"
#include "ui_searchdialog.h"
#include <QMessageBox>

static void parseForSearch(const QString& raw, QString& sender, QString& plain)
{
    QString s = raw.trimmed();
    sender    = "未知";
    if (s.startsWith("[me]")) {
        sender = "我";
        plain  = s.mid(4).trimmed();
    }
    else if (s.startsWith('[')) {
        int r = s.indexOf(']');
        if (r > 1) {
            sender = s.mid(1, r - 1); // 提取ID
            plain  = s.mid(r + 1).trimmed();
        }
    }
    else {
        plain = s;
    }
}
SearchDialog::SearchDialog(const std::vector<std::pair<QString, QString>>& history,
                           const QString& peerName, QWidget* parent)
    : QDialog(parent), ui(new Ui::SearchDialog), m_history(history)
{
    ui->setupUi(this);
    setWindowTitle(QString("查找与 %1 的聊天记录").arg(peerName));
}

SearchDialog::~SearchDialog()
{
    delete ui;
}

void SearchDialog::on_buttonFind_clicked()
{
    QString keyword = ui->findText->text().trimmed();
    if (keyword.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入搜索关键词");
        return;
    }

    ui->res->clear(); // 清空上次的搜索结果
    int matchCount = 0;
    for (const auto [_, msg] : m_history) {
        QString sender, text;
        parseForSearch(msg, sender, text);
        if (text.contains(keyword, Qt::CaseInsensitive)) {
            QString displayStr = QString("%1: %2").arg(sender).arg(text);
            ui->res->addItem(displayStr);
            matchCount++;
        }
    }

    if (matchCount == 0) { ui->res->addItem("—— 没有找到相关记录 ——"); }
}
