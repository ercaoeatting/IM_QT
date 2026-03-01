#ifndef FileDialog_H
#define FileDialog_H

#include <QDialog>
#include <QTableWidget>

namespace Ui {
class FileDialog;
}

class FileDialog : public QDialog {
    Q_OBJECT

public:
    explicit FileDialog(QWidget* parent = nullptr);
    ~FileDialog();

    // for MainWindow
    // 添加一个新任务到表格中
    void addTask(uint32_t taskId, const QString& fileName, int totalSize, bool isSender);
    // 更新进度条
    void updateProgress(uint32_t taskId, int transferred, int totalSize);
    // 更新状态文字
    void updateStatus(uint32_t taskId, const QString& status);

signals:
    // 点击接受/拒绝
    void taskAccepted(uint32_t taskId);
    void taskRejected(uint32_t taskId);
    void taskCanceled(uint32_t taskId);

private:
    Ui::FileDialog* ui;

    int getRowByTaskId(uint32_t taskId);
};

#endif // FileDialog_H