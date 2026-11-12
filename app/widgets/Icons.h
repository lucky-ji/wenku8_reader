// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <QIcon>
#include <QColor>
class QAbstractButton;
namespace Wenku8::Reader
{
    // Original SVG assets embedded in icons.qrc; rendered at the target screen DPR.
    enum class Icon
    {
        Library,
        Discover,
        Download,
        Search,
        Refresh,
        SignIn,
        Book,
        List,
        Left,
        Right,
        TextSmaller,
        TextLarger,
        Bookmark,
        RestoreBookmark,
        Cancel,
        Sun,
        Moon,
        Leaf,
        Shield,
        History,
        ShelfAdd,
        Fullscreen,
        Profile,
        Folder,
        Settings
    };
    QIcon makeIcon(Icon, const QColor& oNormal, const QColor& oDisabled, const QColor& oActive);
    void setButtonIcon(QAbstractButton*, Icon);
} // namespace Wenku8::Reader
