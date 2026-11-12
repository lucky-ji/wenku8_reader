// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "Icons.h"
#include "TouchScroll.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QScrollArea>
namespace Wenku8::Reader::PageControls
{
    inline QPushButton* action(const QString& strTitle, const char* pName, Icon eIcon)
    {
        auto pB = new QPushButton(strTitle);
        pB->setObjectName(pName);
        pB->setMinimumSize(48, 56);
        pB->setProperty("mobileRow", true);
        pB->setCursor(Qt::PointingHandCursor);
        setButtonIcon(pB, eIcon);
        return pB;
    }
    inline QLabel* paragraph(const QString& strValue, const char* pName = "muted")
    {
        auto pL = new QLabel(strValue);
        pL->setObjectName(pName);
        pL->setTextFormat(Qt::PlainText);
        pL->setWordWrap(true);
        return pL;
    }
    inline QVBoxLayout* controlGroup(QVBoxLayout* pParent)
    {
        auto* pGroup = new QWidget;
        pGroup->setObjectName("controlGroup");
        pGroup->setAttribute(Qt::WA_StyledBackground);
        auto* pLayout = new QVBoxLayout(pGroup);
        pLayout->setContentsMargins(10, 10, 10, 10);
        pLayout->setSpacing(6);
        pParent->addWidget(pGroup);
        return pLayout;
    }
    inline void addGroupAction(QVBoxLayout* pLayout, QPushButton* pButton)
    {
        pButton->setProperty("groupRow", true);
        pLayout->addWidget(pButton);
    }
    inline QWidget* scrollPage(QVBoxLayout*& pContent)
    {
        auto pPage = new QWidget;
        pPage->setProperty("touchUi", true);
        auto pOuter = new QVBoxLayout(pPage);
        pOuter->setContentsMargins(0, 0, 0, 0);
        auto pScroll = new QScrollArea;
        pScroll->setFrameShape(QFrame::NoFrame);
        pScroll->setWidgetResizable(true);
        pScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto pBody = new QWidget;
        pContent = new QVBoxLayout(pBody);
        pContent->setContentsMargins(12, 16, 12, 24);
        pContent->setSpacing(10);
        pScroll->setWidget(pBody);
        pOuter->addWidget(pScroll);
        (new TouchScroll(pScroll))->setEnabled(true);
        return pPage;
    }
}
