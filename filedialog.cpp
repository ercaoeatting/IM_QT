#include "filedialog.h"
#include "ui_filedialog.h"
#include <QProgressBar>
#include <QPushButton>
#include <QHBoxLayout>
#include <QWidget>

FileDialog::FileDialog(QWidget* parent) : QDialog(parent), ui(new Ui::FileDialog)
{
    ui->setupUi(this);
    setWindowTitle("文件传输");

    ui->tableWidget->setColumnCount(5);
    ui->tableWidget->setHorizontalHeaderLabels({"文件名", "大小", "传输进度", "状态", "操作"});

    ui->tableWidget->horizontalHeader()->setStretchLastSection(true);
    ui->tableWidget->setColumnWidth(0, 180);
    ui->tableWidget->setColumnWidth(2, 150);
    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableWidget->verticalHeader()->setVisible(false);
}

FileDialog::~FileDialog()
{
    delete ui;
}

int FileDialog::getRowByTaskId(uint32_t taskId)
{
    for (int i = 0; i < ui->tableWidget->rowCount(); ++i) {
        uint32_t id = ui->tableWidget->item(i, 0)->data(Qt::UserRole).toUInt();
        if (id == taskId) return i;
    }
    return -1;
}

void FileDialog::addTask(uint32_t taskId, const QString& fileName, int totalSize, bool isSender)
{
    int row = ui->tableWidget->rowCount();
    ui->tableWidget->insertRow(row);

    // 第0列：文件名(这里同时存一下taskID)
    QTableWidgetItem* nameItem = new QTableWidgetItem(fileName);
    nameItem->setData(Qt::UserRole, taskId);
    ui->tableWidget->setItem(row, 0, nameItem);

    // 第1列：文件大小
    QString sizeStr = QString::number(totalSize / 1024.0, 'f', 2) + " KB";
    if (totalSize > 1024 * 1024) {
        sizeStr = QString::number(totalSize / 1024.0 / 1024.0, 'f', 2) + " MB";
    }
    ui->tableWidget->setItem(row, 1, new QTableWidgetItem(sizeStr));

    // 第2列：进度条
    QProgressBar* bar = new QProgressBar();
    bar->setRange(0, totalSize);
    bar->setValue(0);
    bar->setAlignment(Qt::AlignCenter);
    ui->tableWidget->setCellWidget(row, 2, bar);

    // 第3列：状态
    QString initStatus = isSender ? "等待对方接收..." : "收到文件，请确认";
    ui->tableWidget->setItem(row, 3, new QTableWidgetItem(initStatus));

    // 第4列：操作按钮
    QWidget*     actionWidget = new QWidget();
    QHBoxLayout* layout       = new QHBoxLayout(actionWidget);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(5);
    if (isSender) {
        QPushButton* btnCancel = new QPushButton("取消");
        layout->addWidget(btnCancel);
        connect(btnCancel, &QPushButton::clicked, this,
                [this, taskId]() { emit taskCanceled(taskId); });
    }
    else {
        QPushButton* btnAccept = new QPushButton("接收");
        QPushButton* btnReject = new QPushButton("拒绝");
        layout->addWidget(btnAccept);
        layout->addWidget(btnReject);
        connect(btnAccept, &QPushButton::clicked, this, [this, taskId, btnAccept, btnReject]() {
            emit taskAccepted(taskId);
            btnAccept->setEnabled(false);
            btnReject->setEnabled(false);
        });
        connect(btnReject, &QPushButton::clicked, this, [this, taskId, btnAccept, btnReject]() {
            emit taskRejected(taskId);
            btnAccept->setEnabled(false);
            btnReject->setEnabled(false);
        });
    }
    ui->tableWidget->setCellWidget(row, 4, actionWidget);
}


// 更新指定任务的进度条
void FileDialog::updateProgress(uint32_t taskId, int transferred, int totalSize)
{
    int row = getRowByTaskId(taskId);
    if (row == -1) return;

    QProgressBar* bar = (QProgressBar*)ui->tableWidget->cellWidget(row, 2);
    if (bar) {
        bar->setMaximum(totalSize);
        bar->setValue(transferred);
    }
}

// 更新指定任务的状态文字
void FileDialog::updateStatus(uint32_t taskId, const QString& status)
{
    int row = getRowByTaskId(taskId);
    if (row == -1) return;

    QTableWidgetItem* item = ui->tableWidget->item(row, 3);
    if (item) { item->setText(status); }
    if (status == "已手动取消" || status == "对方已取消" || status == "对方已拒绝" ||
        status == "接收完成！" || status == "发送完成！") {
        ui->tableWidget->removeCellWidget(row, 4);
        ui->tableWidget->setItem(row, 4, new QTableWidgetItem(""));
    }
}
