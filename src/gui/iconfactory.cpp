/*
    Copyright (c) 2018, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "iconfactory.h"

#include "gui/icons.h"
#include "gui/iconfont.h"

#include <QBitmap>
#include <QCoreApplication>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPixmap>
#include <QPointer>
#include <QVariant>
#include <QWidget>

#if QT_VERSION < 0x050000
# include <QIconEngineV2>
using QtIconEngine = QIconEngineV2;
#else
# include <QIconEngine>
using QtIconEngine = QIconEngine;
#endif

#ifndef COPYQ_ICON_NAME
# define COPYQ_ICON_NAME "copyq"
#endif

namespace {

const char imagesRecourcePath[] = ":/images/";

/// Up to this value of background lightness, icon color will be lighter.
const int lightThreshold = 100;

QPointer<QObject> activePaintDevice;

void replaceColor(QPixmap *pix, const QColor &from, const QColor &to)
{
    if (from == to)
        return;

    QPixmap pix2( pix->size() );
    pix2.fill(to);
    pix2.setMask( pix->createMaskFromColor(from, Qt::MaskOutColor) );

    QPainter p(pix);
    p.drawPixmap(0, 0, pix2);
}

QString sessionName()
{
    return qApp->property("CopyQ_session_name").toString();
}

QString textFromEnv(const char *envVaribleName)
{
    const auto name = qgetenv(envVaribleName);
    return QString::fromUtf8(name);
}

QColor colorFromEnv(const char *envVaribleName)
{
    return QColor( textFromEnv(envVaribleName) );
}

QColor appIconColorHelper()
{
    const auto color = colorFromEnv("COPYQ_APP_COLOR");
    return color.isValid() ? QColor(color) : QColor(0x7f, 0xca, 0x9b);
}

QColor appIconColor()
{
    static const QColor color = appIconColorHelper();
    return color;
}

QColor sessionNameToColor(const QString &name)
{
    if (name.isEmpty())
        return appIconColor();

    int r = 0;
    int g = 0;
    int b = 0;

    for (const auto &c : name) {
        const ushort x = c.unicode() % 3;
        if (x == 0)
            r += 255;
        else if (x == 1)
            g += 255;
        else
            b += 255;
    }

    int max = qMax(r, qMax(g, b));
    r = r * 255 / max;
    g = g * 255 / max;
    b = b * 255 / max;

    return QColor(r, g, b);
}

QColor sessionIconColorHelper()
{
    const auto color = colorFromEnv("COPYQ_SESSION_COLOR");
    return color.isValid() ? QColor(color) : sessionNameToColor( sessionName() );
}

QColor &sessionIconColorVariable()
{
    static QColor color = sessionIconColorHelper();
    return color;
}

QString &sessionIconTagVariable()
{
    static QString tag = textFromEnv("COPYQ_SESSION_TAG");
    return tag;
}

QColor &sessionIconTagColorVariable()
{
    static QColor color = colorFromEnv("COPYQ_SESSION_TAG_COLOR");
    return color;
}

QPixmap imageFromPrefix(const QString &iconSuffix, const QString &resources)
{
#ifdef COPYQ_ICON_PREFIX
    const QString fileName(COPYQ_ICON_PREFIX + iconSuffix);
    if ( QFile::exists(fileName) )
        return QPixmap(fileName);
#else
    Q_UNUSED(iconSuffix)
#endif
    return QPixmap(imagesRecourcePath + resources);
}

void drawFontIcon(QPixmap *pix, ushort id, int w, int h, const QColor &color)
{
    QPainter painter(pix);
    const QFont font = iconFontFitSize(w, h);

    painter.setFont(font);
    painter.setPen(color);

    // Center the icon to whole pixels so it stays sharp.
    const auto flags = Qt::AlignTop | Qt::AlignLeft;
    const auto iconText = QString(QChar(id));
    auto boundingRect = painter.boundingRect(0, 0, w, h, flags, iconText);
    const auto pos = QPoint(w - boundingRect.width(), h - boundingRect.height()) / 2;
    boundingRect.moveTopLeft(pos);

    painter.drawText(boundingRect, flags, iconText);
}

QColor getDefaultIconColor(const QColor &color)
{
    QColor c = color;
    bool menuBackgrounIsLight = c.lightness() > lightThreshold;
    c.setHsl(c.hue(),
             qMax(0, qMin(255, c.saturation() + (menuBackgrounIsLight ? 30 : 10))),
             qMax(0, qMin(255, c.lightness() + (menuBackgrounIsLight ? -140 : 100))));

    return c;
}

class IconEngine : public QtIconEngine
{
public:
    static bool useSystemIcons;

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override
    {
        painter->drawPixmap( rect, createPixmap(rect.size(), mode, state, painter) );
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return createPixmap(size, mode, state);
    }

    QtIconEngine *clone() const override
    {
        return new IconEngine(*this);
    }

    QPixmap createPixmap(QSize size, QIcon::Mode mode, QIcon::State state, QPainter *painter = nullptr)
    {
        if ( useSystemIcons || m_iconId == 0 || !loadIconFont() ) {
            // Tint tab icons.
            if ( m_iconName.startsWith(imagesRecourcePath + QString("tab_")) ) {
                QPixmap pixmap(m_iconName);
                pixmap = pixmap.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                QPainter painter2(&pixmap);
                painter2.setCompositionMode(QPainter::CompositionMode_SourceIn);
                painter2.fillRect( pixmap.rect(), color(painter, mode) );
                return taggedIcon(&pixmap);
            }

            QIcon icon = m_iconName.startsWith(':') ? QIcon(m_iconName) : QIcon::fromTheme(m_iconName);
            if ( !icon.isNull() ) {
                auto pixmap = icon.pixmap(size, mode, state);
                return taggedIcon(&pixmap);
            }
        }

#if QT_VERSION >= 0x050000
        if (painter)
            size *= painter->paintEngine()->paintDevice()->devicePixelRatio();
#endif
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);

        if (m_iconId == 0)
            return taggedIcon(&pixmap);

        drawFontIcon( &pixmap, m_iconId, size.width(), size.height(), color(painter, mode) );

        return taggedIcon(&pixmap);
    }

    // QIconEngine doesn't seem to work in menus on OS X.
#ifdef Q_OS_MAC
    void updateIcon(QIcon *icon, const QSize &size, QIcon::Mode mode)
    {
        icon->addPixmap( createPixmap(size, mode, QIcon::Off), mode, QIcon::Off );
    }

    void updateIcon(QIcon *icon, int extent)
    {
        const QSize size(extent, extent);
        updateIcon(icon, size, QIcon::Normal);
        updateIcon(icon, size, QIcon::Disabled);
        updateIcon(icon, size, QIcon::Active);
    }

    static QIcon createIcon(ushort iconId, const QString &iconName, const QString &tag = QString(), const QColor &tagColor = QColor())
    {
        IconEngine iconEngine(iconId, iconName, tag, tagColor);
        QIcon icon;
        iconEngine.updateIcon(&icon, 16);
        iconEngine.updateIcon(&icon, 32);
        iconEngine.updateIcon(&icon, 64);
        iconEngine.updateIcon(&icon, 128);
        return icon;
    }
#else
    static QIcon createIcon(ushort iconId, const QString &iconName, const QString &tag = QString(), const QColor &tagColor = QColor())
    {
        return QIcon( new IconEngine(iconId, iconName, tag, tagColor) );
    }
#endif

    static void tagIcon(QPixmap *pix, const QString &tag, QColor color)
    {
        if ( tag.isEmpty() )
            return;

#if QT_VERSION >= 0x050000
        const auto ratio = pix->devicePixelRatio();
        pix->setDevicePixelRatio(1);
#else
        const auto ratio = 1;
#endif
        QPainter painter(pix);
        painter.setRenderHint(QPainter::HighQualityAntialiasing, true);

        const int h = pix->height();
        const int strokeWidth = static_cast<int>(ratio + h / 16);

        QFont font;
        const auto pixelSize = pix->width() / 2;
        if ( tag.size() == 1 ) {
            font = iconFontFitSize(pixelSize, pixelSize);
        } else {
            font.setPixelSize(pixelSize);
            font.setBold(true);
        }
        painter.setFont(font);

        const auto rect = painter.fontMetrics().tightBoundingRect(tag);
        const auto baseLineY = rect.bottom();
        const auto pos = QPoint(strokeWidth, h - strokeWidth - baseLineY);

        QPainterPath path;
        path.addText(pos, font, tag);
        const auto strokeColor = color.lightness() < 100 ? Qt::white : Qt::black;
        painter.strokePath(path, QPen(strokeColor, strokeWidth));

        painter.setPen(color);
        painter.drawText(pos, tag);
    }

private:
    IconEngine(ushort iconId, const QString &iconName, const QString &tag, QColor tagColor)
        : m_iconId(iconId)
        , m_iconName(iconName)
        , m_tag(tag)
        , m_tagColor(tagColor)
    {
    }

    QColor color(QPainter *painter, QIcon::Mode mode)
    {
        auto parent = painter
                ? dynamic_cast<QWidget*>(painter->device())
                : qobject_cast<QWidget*>(activePaintDevice.data());

        const bool selected = (mode == QIcon::Active || mode == QIcon::Selected);
        QColor color = parent ? getDefaultIconColor(*parent, selected) : Qt::darkGray;

        if (mode == QIcon::Disabled)
            color.setAlphaF(0.5);

        return color;
    }

    QPixmap taggedIcon(QPixmap *pix)
    {
        tagIcon(pix, m_tag, m_tagColor);
        return *pix;
    }

    ushort m_iconId;
    QString m_iconName;
    QString m_tag;
    QColor m_tagColor;
};

bool IconEngine::useSystemIcons = false;

void updateIcon(QIcon *icon, const QPixmap &pix, int extent)
{
    if (pix.height() > extent)
        icon->addPixmap( pix.scaledToHeight(extent, Qt::SmoothTransformation) );
}

} // namespace

QIcon getIcon(const QString &themeName, unsigned short id)
{
    return loadIconFont() || !themeName.isEmpty()
            ? IconEngine::createIcon(loadIconFont() ? id : 0, themeName)
            : QIcon();
}

QIcon getIcon(const QVariant &iconOrIconId)
{
    if (iconOrIconId.canConvert(QVariant::UInt))
        return getIcon( QString(), iconOrIconId.value<ushort>() );

    if (iconOrIconId.canConvert(QVariant::Icon))
        return iconOrIconId.value<QIcon>();

    return QIcon();
}

QIcon getIconFromResources(const QString &iconName)
{
    return IconEngine::createIcon(0, imagesRecourcePath + iconName);
}

QIcon iconFromFile(const QString &fileName, const QString &tag, const QColor &color)
{
    if ( fileName.isEmpty() )
        return QIcon();

    const auto unicode = toIconId(fileName);
    if (unicode != 0)
        return loadIconFont() ? IconEngine::createIcon(unicode, "", tag, color) : QIcon();

    return IconEngine::createIcon(0, fileName, tag, color);
}

QPixmap createPixmap(unsigned short id, const QColor &color, int size)
{
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    if (loadIconFont())
        drawFontIcon(&pixmap, id, size, size, color);

    return pixmap;
}

QIcon appIcon(AppIconType iconType)
{
    const bool running = iconType == AppIconRunning;
    const QString suffix = running ? "-busy" : "-normal";
    const QString sessionName = ::sessionName();

    QIcon icon;

    if (sessionName.isEmpty())
        icon = QIcon::fromTheme(COPYQ_ICON_NAME + suffix);
    else
        icon = QIcon::fromTheme(COPYQ_ICON_NAME "_" + sessionName + "-" + suffix);

    QPixmap pix;

    if (icon.isNull())
        pix = imageFromPrefix(suffix + ".svg", running ? "icon-running" : "icon");
    else
        pix = icon.pixmap(128);

#if QT_VERSION >= 0x050000
    pix.setDevicePixelRatio(1);
#endif

    const auto sessionColor = sessionIconColor();
    const auto appColor = appIconColor();

    if (sessionColor != appColor)
        replaceColor(&pix, appColor, sessionColor);

    const auto &tag = sessionIconTagVariable();
    if ( !tag.isEmpty() )
        IconEngine::tagIcon(&pix, tag, sessionIconTagColorVariable());

    QIcon icon2;
    icon2.addPixmap(pix);

    // This makes the icon smoother on some systems.
    updateIcon(&icon2, pix, 48);
    updateIcon(&icon2, pix, 32);
    updateIcon(&icon2, pix, 24);
    updateIcon(&icon2, pix, 16);

    return icon2;
}

void setActivePaintDevice(QObject *device)
{
    activePaintDevice = device;
}

QColor getDefaultIconColor(const QWidget &widget, bool selected)
{
    const QWidget *parent = &widget;
    while ( parent->parentWidget()
            && !parent->isTopLevel()
            && !parent->testAttribute(Qt::WA_OpaquePaintEvent) )
    {
        parent = parent->parentWidget();
    }

    QPalette::ColorRole role = selected ? QPalette::Highlight : parent->backgroundRole();
    return getDefaultIconColor( parent->palette().color(QPalette::Active, role) );
}

unsigned short toIconId(const QString &fileNameOrId)
{
    if ( fileNameOrId.size() != 1 )
        return 0;

    return fileNameOrId.at(0).unicode();
}

void setSessionIconColor(QColor color)
{
    sessionIconColorVariable() = color;
}

void setSessionIconTag(const QString &tag)
{
    sessionIconTagVariable() = tag;
}

void setSessionIconTagColor(QColor color)
{
    sessionIconTagColorVariable() = color;
}

QColor sessionIconColor()
{
    return ::sessionIconColorVariable();
}

QString sessionIconTag()
{

    return ::sessionIconTagVariable();
}

QColor sessionIconTagColor()
{

    return ::sessionIconTagColorVariable();
}

void setUseSystemIcons(bool useSystemIcons)
{
    IconEngine::useSystemIcons = useSystemIcons;
}
