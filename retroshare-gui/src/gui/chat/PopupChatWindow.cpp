/****************************************************************
 *
 *  RetroShare is distributed under the following license:
 *
 *  Copyright (C) 2006,  crypton
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA  02110-1301, USA.
 ****************************************************************/

#include <QPixmap>

#include "PopupChatWindow.h"
#include "ChatDialog.h"
#include "gui/settings/rsharesettings.h"
#include "gui/settings/RsharePeerSettings.h"
#include "gui/common/StatusDefs.h"
#include "gui/style/RSStyle.h"
#include"util/misc.h"

#include <retroshare/rsmsgs.h>
#include <retroshare/rsnotify.h>

#define IMAGE_WINDOW         ":/images/rstray3.png"
#define IMAGE_TYPING         ":/images/typing.png"
#define IMAGE_CHAT           ":/images/chat.png"

static PopupChatWindow *instance = NULL;

/*static*/ PopupChatWindow *PopupChatWindow::getWindow(bool needSingleWindow)
{
	if (needSingleWindow == false && (Settings->getChatFlags() & RS_CHAT_TABBED_WINDOW)) {
		if (instance == NULL) {
			instance = new PopupChatWindow(true);
		}

		return instance;
	}

	return new PopupChatWindow(false);
}

/*static*/ void PopupChatWindow::cleanup()
{
	if (instance) {
	   delete(instance);
	   instance = NULL;
	}
}

/** Default constructor */
PopupChatWindow::PopupChatWindow(bool tabbed, QWidget *parent, Qt::WFlags flags) : QMainWindow(parent, flags)
{
	/* Invoke Qt Designer generated QObject setup routine */
	ui.setupUi(this);

	tabbedWindow = tabbed;
	firstShow = true;
	chatDialog = NULL;

	ui.tabWidget->setVisible(tabbedWindow);

	if (Settings->getChatFlags() & RS_CHAT_TABBED_WINDOW) {
		ui.actionDockTab->setVisible(tabbedWindow == false);
		ui.actionUndockTab->setVisible(tabbedWindow);
	} else {
		ui.actionDockTab->setVisible(false);
		ui.actionUndockTab->setVisible(false);
	}

	setAttribute(Qt::WA_DeleteOnClose, true);

	connect(ui.actionAvatar, SIGNAL(triggered()),this, SLOT(getAvatar()));
	connect(ui.actionColor, SIGNAL(triggered()), this, SLOT(setStyle()));
	connect(ui.actionDockTab, SIGNAL(triggered()), this, SLOT(dockTab()));
	connect(ui.actionUndockTab, SIGNAL(triggered()), this, SLOT(undockTab()));
	connect(ui.actionSetOnTop, SIGNAL(toggled(bool)), this, SLOT(setOnTop()));

	connect(ui.tabWidget, SIGNAL(tabChanged(ChatDialog*)), this, SLOT(tabChanged(ChatDialog*)));

	if (tabbedWindow) {
		/* signal toggled is called */
		ui.actionSetOnTop->setChecked(Settings->valueFromGroup("ChatWindow", "OnTop", false).toBool());
	}

	setWindowIcon(QIcon(IMAGE_WINDOW));
}

/** Destructor. */
PopupChatWindow::~PopupChatWindow()
{
	saveSettings();

	if (this == instance) {
		instance = NULL;
	}
}

void PopupChatWindow::saveSettings()
{
	if (tabbedWindow) {
		Settings->saveWidgetInformation(this);

		Settings->setValueToGroup("ChatWindow", "OnTop", ui.actionSetOnTop->isChecked());
	} else {
		if (!peerId.empty()) {
			PeerSettings->saveWidgetInformation(peerId, this);
			PeerSettings->setPrivateChatOnTop(peerId, ui.actionSetOnTop->isChecked());
		}
	}
}

void PopupChatWindow::showEvent(QShowEvent */*event*/)
{
	if (firstShow) {
		firstShow = false;

		if (tabbedWindow) {
			Settings->loadWidgetInformation(this);
		} else {
			this->move(qrand()%100, qrand()%100); //avoid to stack multiple popup chat windows on the same position
			PeerSettings->loadWidgetInformation(peerId, this);
		}
	}
}

ChatDialog *PopupChatWindow::getCurrentDialog()
{
	if (tabbedWindow) {
		return dynamic_cast<ChatDialog*>(ui.tabWidget->currentWidget());
	}

	return chatDialog;
}

void PopupChatWindow::addDialog(ChatDialog *dialog)
{
	if (tabbedWindow) {
		ui.tabWidget->addDialog(dialog);
	} else {
		ui.horizontalLayout->addWidget(dialog);
		dialog->addToParent(this);
		ui.horizontalLayout->setContentsMargins(0, 0, 0, 0);
		peerId = dialog->getPeerId();
		chatDialog = dialog;
		calculateStyle(dialog);

		/* signal toggled is called */
		ui.actionSetOnTop->setChecked(PeerSettings->getPrivateChatOnTop(peerId));
	}

	QObject::connect(dialog, SIGNAL(infoChanged(ChatDialog*)), this, SLOT(tabInfoChanged(ChatDialog*)));
	QObject::connect(dialog, SIGNAL(newMessage(ChatDialog*)), this, SLOT(tabNewMessage(ChatDialog*)));
	QObject::connect(dialog, SIGNAL(dialogClose(ChatDialog*)), this, SLOT(dialogClose(ChatDialog*)));
}

void PopupChatWindow::removeDialog(ChatDialog *dialog)
{
	QObject::disconnect(dialog, SIGNAL(infoChanged(ChatDialog*)), this, SLOT(tabInfoChanged(ChatDialog*)));
	QObject::disconnect(dialog, SIGNAL(newMessage(ChatDialog*)), this, SLOT(tabNewMessage(ChatDialog*)));
	QObject::disconnect(dialog, SIGNAL(dialogClose(ChatDialog*)), this, SLOT(dialogClose(ChatDialog*)));

	if (tabbedWindow) {
		ui.tabWidget->removeDialog(dialog);

		if (ui.tabWidget->count() == 0) {
			deleteLater();
		}
	} else {
		if (chatDialog == dialog) {
			saveSettings();
			dialog->removeFromParent(this);
			ui.horizontalLayout->removeWidget(dialog);
			chatDialog = NULL;
			peerId.erase();
			deleteLater();
		}
	}
}

void PopupChatWindow::showDialog(ChatDialog *dialog, uint chatflags)
{
	if (chatflags & RS_CHAT_FOCUS) {
		if (tabbedWindow) {
			ui.tabWidget->setCurrentWidget(dialog);
		}
		show();
		activateWindow();
		setWindowState((windowState() & (~Qt::WindowMinimized)) | Qt::WindowActive);
		raise();
		dialog->focusDialog();
	} else {
		if (isVisible() == false) {
			showMinimized();
		}
		alertDialog(dialog);
	}
}

void PopupChatWindow::alertDialog(ChatDialog */*dialog*/)
{
	QApplication::alert(this);
}

void PopupChatWindow::calculateTitle(ChatDialog *dialog)
{
	bool hasNewMessages = false;
	ChatDialog *cd;

	/* is typing */
	bool isTyping = false;
	if (ui.tabWidget->isVisible()) {
		ui.tabWidget->getInfo(isTyping, hasNewMessages, NULL);
	} else {
		if (dialog) {
			isTyping = dialog->isTyping();
			hasNewMessages = dialog->hasNewMessages();
		}
	}

	if (ui.tabWidget->isVisible()) {
		cd = dynamic_cast<ChatDialog*>(ui.tabWidget->currentWidget());
	} else {
		cd = dialog;
	}

	QIcon icon;
	if (isTyping) {
		icon = QIcon(IMAGE_TYPING);
	} else if (hasNewMessages) {
		icon = QIcon(IMAGE_CHAT);
	} else {
		if (cd && cd->hasPeerStatus()) {
			icon = QIcon(StatusDefs::imageIM(cd->getPeerStatus()));
		} else {
			icon = QIcon(IMAGE_WINDOW);
		}
	}

	setWindowIcon(icon);

	if (cd) {
		QString title = cd->getTitle();
		if (cd->hasPeerStatus()) {
			title += " (" + StatusDefs::name(cd->getPeerStatus()) + ")";
		}
		setWindowTitle(title);
	} else {
		setWindowTitle("RetroShare");
	}
}

void PopupChatWindow::getAvatar()
{
	QByteArray ba;
	if (misc::getOpenAvatarPicture(this, ba)) {
		std::cerr << "Avatar image size = " << ba.size() << std::endl ;

		rsMsgs->setOwnAvatarData((unsigned char *)(ba.data()), ba.size());	// last char 0 included.
	}
}

void PopupChatWindow::dialogClose(ChatDialog *dialog)
{
	removeDialog(dialog);
}

void PopupChatWindow::tabChanged(ChatDialog *dialog)
{
	calculateStyle(dialog);
	calculateTitle(dialog);
}

void PopupChatWindow::tabInfoChanged(ChatDialog *dialog)
{
	calculateTitle(dialog);
}

void PopupChatWindow::tabNewMessage(ChatDialog *dialog)
{
	alertDialog(dialog);
}

void PopupChatWindow::dockTab()
{
	if ((Settings->getChatFlags() & RS_CHAT_TABBED_WINDOW) && chatDialog) {
		PopupChatWindow *pcw = getWindow(false);
		if (pcw) {
			ChatDialog *pcd = chatDialog;
			removeDialog(pcd);
			pcw->addDialog(pcd);
			pcw->show();
			pcw->calculateTitle(pcd);
		}
	}
}

void PopupChatWindow::undockTab()
{
	ChatDialog *cd = dynamic_cast<ChatDialog*>(ui.tabWidget->currentWidget());

	if (cd) {
		PopupChatWindow *pcw = getWindow(true);
		if (pcw) {
			removeDialog(cd);
			pcw->addDialog(cd);
			cd->show();
			pcw->show();
			pcw->calculateTitle(cd);
		}
	}
}

void PopupChatWindow::setStyle()
{
	ChatDialog *cd = getCurrentDialog();

	if (cd && cd->setStyle()) {
		calculateStyle(cd);
	}
}

void PopupChatWindow::setOnTop()
{
	Qt::WindowFlags flags = windowFlags();
	if (ui.actionSetOnTop->isChecked()) {
		flags |= Qt::WindowStaysOnTopHint;
	} else {
		flags &= ~Qt::WindowStaysOnTopHint;
	}
	setWindowFlags(flags);

	/* Show window again */
	show();
}

void PopupChatWindow::calculateStyle(ChatDialog *dialog)
{
	QString toolSheet;
	QString statusSheet;
	QString widgetSheet;

	if (dialog) {
		const RSStyle *style = dialog->getStyle();
		if (style) {
			QString styleSheet = style->getStyleSheet();

			if (styleSheet.isEmpty() == false) {
				toolSheet = QString("QToolBar{%1}").arg(styleSheet);
				statusSheet = QString(".QStatusBar{%1}").arg(styleSheet);
				widgetSheet = QString(".QWidget{%1}").arg(styleSheet);
			}
		}
	}

	ui.chattoolBar->setStyleSheet(toolSheet);
	ui.chatstatusbar->setStyleSheet(statusSheet);
	ui.chatcentralwidget->setStyleSheet(widgetSheet);
}
