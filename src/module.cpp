/***************************************************************************
 *   Copyright (C) 2010~2011 by CSSlayer                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/


// Qt
#include <QFile>
#include <QPaintEngine>
#include <QDir>
#include <QDebug>

// KDE
#include <KAboutData>
#include <KPluginFactory>

// system
#include <libintl.h>

// Fcitx
#include <fcitx-utils/utils.h>
#include <fcitx/addon.h>
#include <fcitx-config/xdg.h>
#include <fcitx/module/dbus/dbusstuff.h>
#include <fcitx/module/ipc/ipc.h>
#include <fcitxqtkeyboardlayout.h>
#include <fcitxqtconnection.h>

// self
#include "config.h"
#include "ui_module.h"
#include "module.h"
#include "addonselector.h"
#include "configwidget.h"
#include "global.h"
#include "subconfigparser.h"
#include "skinpage.h"
#include "impage.h"
#include "imconfigdialog.h"
#include "uipage.h"
#include "configpage.h"
#include "erroroverlay.h"

K_PLUGIN_FACTORY_DECLARATION(KcmFcitxFactory);

namespace Fcitx
{

const UT_icd addonicd = {sizeof(FcitxAddon), 0, 0, FcitxAddonFree};

Module::Module(QWidget *parent, const QVariantList &args) :
    KCModule(parent, args),
    ui(new Ui::Module),
    addonSelector(0),
    m_addons(0),
    m_configPage(0),
    m_skinPage(0),
    m_imPage(0),
    m_uiPage(0)
{
    bindtextdomain("fcitx", LOCALEDIR);
    bind_textdomain_codeset("fcitx", "UTF-8");
    FcitxLogSetLevel(FCITX_NONE);

    FcitxQtInputMethodItem::registerMetaType();
    FcitxQtKeyboardLayout::registerMetaType();

    KAboutData *about = new KAboutData("kcm_fcitx",
                                       i18n("Fcitx Configuration Module"),
                                       VERSION_STRING_FULL,
                                       i18n("Configure Fcitx"),
                                       KAboutLicense::GPL_V2,
                                       i18n("Copyright 2012 Xuetian Weng"),
                                       QString(), QString(),
                                       "wengxt@gmail.com");

    about->addAuthor(i18n("Xuetian Weng"), i18n("Author"), "wengxt@gmail.com");
    setAboutData(about);

    if (FcitxAddonGetConfigDesc() != NULL) {
        utarray_new(m_addons, &addonicd);
        FcitxAddonsLoad(m_addons);
    }

    Global::instance();

    ui->setupUi(this);
    {
        m_imPage = new IMPage(this);
        ui->pageWidget->addTab(m_imPage, i18n("Input Method"));
        connect(m_imPage, SIGNAL(changed()), this, SLOT(changed()));
    }

    {
        FcitxConfigFileDesc* configDesc = Global::instance()->GetConfigDesc("config.desc");

        if (configDesc) {
            m_configPage = new ConfigPage;
            ui->pageWidget->addTab(m_configPage, i18n("Global Config"));
            connect(m_configPage, SIGNAL(changed()), this, SLOT(changed()));
        }
    }

    {
        if (Global::instance()->inputMethodProxy()) {
            m_uiPage = new UIPage(this);
            ui->pageWidget->addTab(m_uiPage, i18n("Appearance"));
            connect(m_uiPage, SIGNAL(changed()), this, SLOT(changed()));
        }
    }

    {
        if (FcitxAddonGetConfigDesc() != NULL) {
            addonSelector = new AddonSelector(this);
            ui->pageWidget->addTab(addonSelector, i18n("Addon Config"));
        }
    }

    if (m_addons) {
        for (FcitxAddon* addon = (FcitxAddon *) utarray_front(m_addons);
                addon != NULL;
                addon = (FcitxAddon *) utarray_next(m_addons, addon)) {
            this->addonSelector->addAddon(addon);
        }
    }

    if (args.size() != 0) {
        m_arg = args[0].toString();
    }
}

Module::~Module()
{
    delete ui;
    if (addonSelector) {
        delete addonSelector;
    }
    if (m_addons) {
        utarray_free(m_addons);
    }
    Global::deInit();
}

FcitxAddon* Module::findAddonByName(const QString& name)
{
    if (!m_addons) {
        return NULL;
    }

    FcitxAddon* addon = NULL;
    for (addon = (FcitxAddon *) utarray_front(m_addons);
         addon != NULL;
         addon = (FcitxAddon *) utarray_next(m_addons, addon)) {
        if (QString::fromUtf8(addon->name) == name)
            break;
    }
    return addon;
}

SkinPage* Module::skinPage() {

    if (!m_skinPage) {
        m_skinPage = new SkinPage(this);
        ui->pageWidget->addTab(m_skinPage, i18n("Manage Skin"));
        connect(m_skinPage, SIGNAL(changed()), this, SLOT(changed()));
    }

    return m_skinPage;
}

void Module::load()
{
    QDialog* configDialog = NULL;
    if (!m_arg.isEmpty()) {
        do {
            if (!Global::instance()->inputMethodProxy())
                break;
            QDBusPendingReply< QString > result = Global::instance()->inputMethodProxy()->GetIMAddon(m_arg);
            result.waitForFinished();
            if (!result.isValid() || result.value().isEmpty())
                break;
            FcitxAddon* addonEntry = findAddonByName(result.value());
            if (!addonEntry)
                break;
            configDialog = new IMConfigDialog(m_arg, addonEntry, 0);
        } while(0);
        if (!configDialog) {
            FcitxAddon* addonEntry = findAddonByName(m_arg);
            if (addonEntry)
                configDialog = ConfigWidget::configDialog(0, addonEntry);
        }
        if (configDialog) {
            configDialog->setAttribute(Qt::WA_DeleteOnClose);
            configDialog->open();
        }
        m_arg = QString();
    }

    if (m_imPage)
        m_imPage->load();
    if (m_skinPage)
        m_skinPage->load();
    if (m_configPage)
        m_configPage->load();
}

void Module::save()
{

    if (m_imPage)
        m_imPage->save();
    if (m_skinPage)
        m_skinPage->save();
    if (m_configPage)
        m_configPage->save();
    if (m_uiPage)
        m_uiPage->save();
}

void Module::defaults()
{
    if (m_configPage) {
        m_configPage->defaults();
    }
    if (m_imPage) {
        m_imPage->defaults();
    }
    KCModule::changed();
}

}
