#pragma once
#include <QDialog>
#include <QStringList>

class QListWidget;
class QComboBox;
class QSpinBox;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;

class CreateArchiveDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CreateArchiveDialog(QWidget *parent = nullptr);
    ~CreateArchiveDialog() override = default;

signals:
    void archiveCreated(const QString &path);

private slots:
    void pickFiles();
    void pickFolder();
    void removeFile();
    void doCreate();
    void formatChanged(int idx);
    void togglePassword(bool on);
    void updateBtn();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildUI();
    void addFiles(const QStringList &paths);
    void updateSizeLabel();

    QListWidget *m_fileList    = nullptr;
    QComboBox   *m_fmtCombo    = nullptr;
    QSpinBox    *m_levelSpin   = nullptr;
    QLineEdit   *m_nameEdit    = nullptr;
    QLineEdit   *m_pwdEdit     = nullptr;
    QLineEdit   *m_confirmEdit = nullptr;
    QCheckBox   *m_pwdChk      = nullptr;
    QPushButton *m_createBtn   = nullptr;
    QLabel      *m_sizeLabel   = nullptr;
    QStringList  m_files;
};
