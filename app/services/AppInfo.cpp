// SPDX-License-Identifier: GPL-2.0-only
#include "AppInfo.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
static void initInformationResources()
{
    Q_INIT_RESOURCE(app_info);
}
namespace Wenku8::Reader
{
    QString applicationName()
    {
        return "轻小说文库";
    }
    QString applicationVersion()
    {
        return "1.0.4";
    }
    QUrl applicationRepositoryUrl()
    {
        return QUrl("https://github.com/lucky-ji/wenku8_reader");
    }
    QUrl applicationFeedbackUrl()
    {
        return QUrl("https://github.com/lucky-ji/wenku8_reader/issues/new");
    }
    bool isApplicationLink(const QUrl& oUrl)
    {
        return oUrl == applicationRepositoryUrl() || oUrl == applicationFeedbackUrl() ||
               oUrl == QUrl("https://github.com/lucky-ji");
    }
    QString licenseText(const QString& strResource)
    {
        static const bool bInitialized = []()
        {
            initInformationResources();
            return true;
        }();
        Q_UNUSED(bInitialized);
        QFile oFile(":/app-info/" + strResource);
        return oFile.open(QIODevice::ReadOnly)
                   ? QString::fromUtf8(oFile.readAll())
                   : QString("暂时无法显示此许可，请查看发布包中的许可证文件。");
    }
    QString versionDescription()
    {
        return QString("<h2>轻小说文库</h2><p>让阅读，随时继续。</p><p><b>版本</b>　%1</p>"
                       "<p><b>开发者</b>　lucky_ji</p><p><a "
                       "href=\"https://github.com/lucky-ji\">GitHub · lucky-ji</a></p>"
                       "<p>"
                       "用于浏览和阅读轻小说，提供书架、小说发现、阅读历史、书签、下载缓存与下载到本地，支持明"
                       "亮、暗色和护眼外观。</p>"
                       "<p><b>开源项目</b><br/><a href=\"%2\">"
                       "github.com/lucky-ji/wenku8_reader</a></p>"
                       "<p><a href=\"%3\">意见反馈 · GitHub Issues</a></p>"
                       "<p>反馈链接会在系统浏览器中打开，由你自行填写和提交问题或建议。</p>"
                       "<p>正式版本的功能更新与问题修复在「更新日志」中独立维护。</p>")
            .arg(applicationVersion().toHtmlEscaped(),
                 applicationRepositoryUrl().toString().toHtmlEscaped(),
                 applicationFeedbackUrl().toString().toHtmlEscaped());
    }
    QString releaseNotes()
    {
        const auto oData = QJsonDocument::fromJson(licenseText("release_notes.json").toUtf8())
                               .object()
                               .value("releases")
                               .toArray();
        if (oData.isEmpty())
            return "<h2>更新日志</h2><p>暂无正式版本更新记录。</"
                   "p><p>首个正式版本发布后，将从该版本开始记录功能更新与问题修复。</p>";
        QString strHtml = "<h2>更新日志</h2>";
        for (const auto& oValue : oData)
        {
            const auto oItem = oValue.toObject();
            strHtml += "<h3>" + oItem.value("version").toString().toHtmlEscaped() + "</h3><p>" +
                       oItem.value("date").toString().toHtmlEscaped() + "</p><ul>";
            for (const auto& oChange : oItem.value("changes").toArray())
                strHtml += "<li>" + oChange.toString().toHtmlEscaped() + "</li>";
            strHtml += "</ul>";
        }
        return strHtml;
    }
    QString licenseOverview()
    {
#ifdef Q_OS_ANDROID
        return licenseText("android-notice.txt");
#else
        return licenseText("notice.txt");
#endif
    }
    QVector<LicenseEntry> applicationLicenses()
    {
        QVector<LicenseEntry> vItems = {
            {"轻小说文库", "GPL-2.0-only", "REFERENCE-GPL-2.0.txt"},
            {"Qt 5", "GPL-2.0 许可选项", "Qt-5.15.2-GPL-2.0.txt"},
            {"Qt 内置第三方组件", "原始版权与许可证", "Qt-bundled-notices.txt"},
            {"curl", "curl license", "curl-8.0.1.txt"},
            {"libxml2", "MIT 与补充声明", "libxml2-2.15.4.txt"},
            {"TinyXML-2", "zlib license", "tinyxml2.txt"}};
#ifdef Q_OS_ANDROID
        vItems.append({"GNU libiconv / libcharset", "LGPL-2.1-or-later", "libiconv-1.17.txt"});
        vItems.append({"Mbed TLS", "GPL-2.0-or-later 许可选项", "mbedtls-3.6.5.txt"});
        vItems.append(
            {"LLVM libc++ filesystem", "Apache-2.0 WITH LLVM-exception", "llvm-libcxx.txt"});
#else
        vItems.append({"Qt WebEngine / Chromium", "原始版权与许可证", "Qt-5.15.2-Chromium.txt"});
        vItems.append({"OpenSSL（Qt 组件依赖）", "OpenSSL / SSLeay license", "OpenSSL-1.1.1t.txt"});
        vItems.append({"zlib（Qt 组件依赖）", "zlib license", "zlib-1.2.13.txt"});
#endif
        return vItems;
    }
} // namespace Wenku8::Reader
