// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QPointer>
#include <QSettings>
#include <QWidget>
#include <QVector>
class QLabel;
class QLineEdit;
class QPushButton;
namespace Wenku8::Reader
{
    class ReadAloudController;
    class SleepTimerControl final : public QWidget
    {
      public:
        explicit SleepTimerControl(ReadAloudController* pController, bool bMobile,
                                   QWidget* pParent = nullptr);

      private:
        QPointer<ReadAloudController> m_pController;
        QSettings m_oSettings;
        QVector<QPushButton*> m_vChoices;
        QLabel* m_pRemaining = nullptr;
        QLabel* m_pHint = nullptr;
        QWidget* m_pCustom = nullptr;
        QLineEdit* m_pMinutes = nullptr;
        QPushButton* m_pApply = nullptr;
        bool m_bEditing = false;
        int m_nCustomMinutes = 45;
        void chooseMode(int nMode);
        void updateState();
        void updateInput();
        void applyCustom();
    };
} // namespace Wenku8::Reader
