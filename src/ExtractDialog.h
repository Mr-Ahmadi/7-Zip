#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;

class ExtractDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ExtractDialog(const QString &archivePath, QWidget *parent = nullptr);

private slots:
    void browseDest();
    void doExtract();
    void togglePassword(bool on);

private:
    void buildUI();

    QString     m_path;
    QString     m_name;
    QLineEdit  *m_destEdit   = nullptr;
    QLineEdit  *m_pwdEdit    = nullptr;
    QCheckBox  *m_pwdChk     = nullptr;
    QPushButton *m_extractBtn = nullptr;
};
