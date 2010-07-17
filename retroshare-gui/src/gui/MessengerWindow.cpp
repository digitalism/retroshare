/****************************************************************
 *  RetroShare is distributed under the following license:
 *
 *  Copyright (C) 2006, crypton
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

#include <QFile>
#include <QFileInfo>
#include "common/vmessagebox.h"

#include "rsiface/rsiface.h"
#include "rsiface/rspeers.h"
#include "rsiface/rsmsgs.h"
#include "rsiface/rsstatus.h"
#include "rsiface/rsnotify.h"

#include "rshare.h"
#include "MessengerWindow.h"
#include "MainWindow.h"
#include "RsAutoUpdatePage.h"

#include "chat/PopupChatDialog.h"
#include "msgs/MessageComposer.h"
#include "connect/ConfCertDialog.h"
#include "util/PixmapMerging.h"
#include "LogoBar.h"
#include "util/Widget.h"
#include "settings/rsharesettings.h"

#include "gui/connect/ConnectFriendWizard.h"
#include "RetroShareLink.h"
#include "PeersDialog.h"
#include "ShareManager.h"

#include <iostream>
#include <sstream>

/* Images for context menu icons */
#define IMAGE_REMOVEFRIEND       ":/images/removefriend16.png"
#define IMAGE_EXPIORTFRIEND      ":/images/exportpeers_16x16.png"
#define IMAGE_CHAT               ":/images/chat.png"
#define IMAGE_MSG                ":/images/message-mail.png"
#define IMAGE_CONNECT            ":/images/connect_friend.png"
#define IMAGE_PEERINFO           ":/images/peerdetails_16x16.png"
#define IMAGE_AVAIBLE            ":/images/user/identityavaiblecyan24.png"
#define IMAGE_CONNECT2           ":/images/reload24.png"
#define IMAGE_PASTELINK          ":/images/pasterslink.png"

/* Images for Status icons */
#define IMAGE_ONLINE             ":/images/im-user.png"
#define IMAGE_OFFLINE            ":/images/im-user-offline.png"
#define IMAGE_AWAY             ":/images/im-user-away.png"
#define IMAGE_BUSY            ":/images/im-user-busy.png"
#define IMAGE_INACTIVE		":/images/im-user-inactive.png"

#define COLUMN_COUNT    3
#define COLUMN_NAME     0
#define COLUMN_STATE    1
#define COLUMN_INFO     2

#define COLUMN_DATA     0 // column for storing the userdata id

#define ROLE_SORT  Qt::UserRole
#define ROLE_ID    Qt::UserRole + 1

/******
 * #define MSG_DEBUG 1
 *****/
MessengerWindow* MessengerWindow::mv = 0;

// quick and dirty for sorting, better use QTreeView and QSortFilterProxyModel
class MyMessengerTreeWidgetItem : public QTreeWidgetItem
{
public:
    MyMessengerTreeWidgetItem(QTreeWidget *pWidget, int type) : QTreeWidgetItem(type)
    {
        m_pWidget = pWidget; // can't access the member "view"
    }

    bool operator<(const QTreeWidgetItem &other) const
    {
        int column = m_pWidget ? m_pWidget->sortColumn() : 0;

        switch (column) {
        case COLUMN_NAME:
            {
                const QVariant v1 = data(column, ROLE_SORT);
                const QVariant v2 = other.data(column, ROLE_SORT);
                return (v1.toString().compare (v2.toString(), Qt::CaseInsensitive) < 0);
            }
        }

        // let the standard do the sort
        return QTreeWidgetItem::operator<(other);
    }

private:
    QTreeWidget *m_pWidget; // the member "view" is private
};

MessengerWindow* MessengerWindow::getInstance()
{
	if(mv == 0)
	{
		mv = new MessengerWindow();
	}
	return mv;
}

void MessengerWindow::releaseInstance()
{
	if(mv != 0)
	{
		delete mv;
	}
}

/** Constructor */
MessengerWindow::MessengerWindow(QWidget* parent, Qt::WFlags flags)
    : 	RWindow("MessengerWindow", parent, flags)
{
    /* Invoke the Qt Designer generated object setup routine */
    ui.setupUi(this);

    connect( ui.messengertreeWidget, SIGNAL( customContextMenuRequested( QPoint ) ), this, SLOT( messengertreeWidgetCostumPopupMenu( QPoint ) ) );
    connect( ui.messengertreeWidget, SIGNAL(itemDoubleClicked ( QTreeWidgetItem *, int)), this, SLOT(chatfriend(QTreeWidgetItem *)));

    connect( ui.avatarButton, SIGNAL(clicked()), SLOT(getAvatar()));
    connect( ui.shareButton, SIGNAL(clicked()), SLOT(openShareManager()));
    connect( ui.addIMAccountButton, SIGNAL(clicked( bool ) ), this , SLOT( addFriend() ) );
    connect( ui.actionHide_Offline_Friends, SIGNAL(triggered()), this, SLOT(insertPeers()));
    connect( ui.actionSort_by_State, SIGNAL(triggered()), this, SLOT(insertPeers()));
    connect(ui.clearButton, SIGNAL(clicked()), this, SLOT(clearFilter()));

    connect(ui.messagelineEdit, SIGNAL(textChanged(const QString &)), this, SLOT(savestatusmessage()));
    connect(ui.statuscomboBox, SIGNAL(activated(int)), this, SLOT(statusChanged(int)));
    connect(ui.filterPatternLineEdit, SIGNAL(textChanged(const QString &)), this, SLOT(filterRegExpChanged()));

    QTimer *timer = new QTimer(this);
    timer->connect(timer, SIGNAL(timeout()), this, SLOT(updateMessengerDisplay()));
    timer->start(1000); /* one second */


    /* to hide the header  */
    ui.messengertreeWidget->header()->hide();

    /* Set header resize modes and initial section sizes */
    ui.messengertreeWidget->setColumnCount(COLUMN_COUNT);
    ui.messengertreeWidget->setColumnHidden ( COLUMN_INFO, true);
    ui.messengertreeWidget->sortItems( COLUMN_NAME, Qt::AscendingOrder );

    QHeaderView * _header = ui.messengertreeWidget->header () ;
    _header->setResizeMode (COLUMN_NAME, QHeaderView::Stretch);
    _header->setResizeMode (COLUMN_STATE, QHeaderView::Custom);
    _header->setStretchLastSection(false);


    _header->resizeSection ( COLUMN_NAME, 200 );
    _header->resizeSection ( COLUMN_STATE, 42 );

    //LogoBar
    _rsLogoBarmessenger = NULL;
    _rsLogoBarmessenger = new LogoBar(ui.logoframe);
    Widget::createLayout(ui.logoframe)->addWidget(_rsLogoBarmessenger);

    ui.statuscomboBox->setMinimumWidth(20);
    ui.messagelineEdit->setMinimumWidth(20);

    itemFont = QFont("ARIAL", 10);
    itemFont.setBold(true);

    displayMenu();

    // load settings
    processSettings(true);

    MainWindow *pMainWindow = MainWindow::getInstance();
    if (pMainWindow) {
        pMainWindow->initializeStatusObject(ui.statuscomboBox);
    }
    insertPeers();
    updateAvatar();
    loadmystatusmessage();

    ui.clearButton->hide();

    updateMessengerDisplay();

    /* Hide platform specific features */
#ifdef Q_WS_WIN
#endif
}

MessengerWindow::~MessengerWindow ()
{
    // save settings
    processSettings(false);

    MainWindow *pMainWindow = MainWindow::getInstance();
    if (pMainWindow) {
        pMainWindow->removeStatusObject(ui.statuscomboBox);
    }
}

void MessengerWindow::processSettings(bool bLoad)
{
    QHeaderView *header = ui.messengertreeWidget->header ();

    Settings->beginGroup(_name);

    if (bLoad) {
        // load settings

        // state of messenger tree
        header->restoreState(Settings->value("MessengerTree").toByteArray());

        // state of actionHide_Offline_Friends
        ui.actionHide_Offline_Friends->setChecked(Settings->value("hideOfflineFriends", false).toBool());

        // state of actionSort_by_State
        ui.actionSort_by_State->setChecked(Settings->value("sortByState", false).toBool());

        // state of actionRoot_is_decorated
        ui.actionRoot_is_decorated->setChecked(Settings->value("rootIsDecorated", true).toBool());
        on_actionRoot_is_decorated_activated();
    } else {
        // save settings

        // state of messenger tree
        Settings->setValue("MessengerTree", header->saveState());

        // state of actionSort_by_State
        Settings->setValue("sortByState", ui.actionSort_by_State->isChecked());

        // state of actionHide_Offline_Friends
        Settings->setValue("hideOfflineFriends", ui.actionHide_Offline_Friends->isChecked());

        // state of actionRoot_is_decorated
        Settings->setValue("rootIsDecorated", ui.actionRoot_is_decorated->isChecked());
    }

    Settings->endGroup();
}

void MessengerWindow::messengertreeWidgetCostumPopupMenu( QPoint point )
{
      QTreeWidgetItem *c = getCurrentPeer();
	  	if (!c) 
	  	{
 	  	  //no peer selected
	  	  return;
	  	}

      QMenu contextMnu( this );

      QAction* expandAll = new QAction(tr( "Expand all" ), &contextMnu );
      connect( expandAll , SIGNAL( triggered() ), ui.messengertreeWidget, SLOT (expandAll()) );

      QAction* collapseAll = new QAction(tr( "Collapse all" ), &contextMnu );
      connect( collapseAll , SIGNAL( triggered() ), ui.messengertreeWidget, SLOT(collapseAll()) );

      QAction* chatAct = new QAction(QIcon(IMAGE_CHAT), tr( "Chat" ), &contextMnu );
      connect( chatAct , SIGNAL( triggered() ), this, SLOT( chatfriendproxy() ) );

      QAction* sendMessageAct = new QAction(QIcon(IMAGE_MSG), tr( "Message Friend" ), &contextMnu );
      connect( sendMessageAct , SIGNAL( triggered() ), this, SLOT( sendMessage() ) );

      QAction* connectfriendAct = new QAction(QIcon(IMAGE_CONNECT), tr( "Connect To Friend" ), &contextMnu );
      connect( connectfriendAct , SIGNAL( triggered() ), this, SLOT( connectfriend() ) );

      QAction* configurefriendAct = new QAction(QIcon(IMAGE_PEERINFO), tr( "Peer Details" ), &contextMnu );
      connect( configurefriendAct , SIGNAL( triggered() ), this, SLOT( configurefriend() ) );

      QAction* recommendfriendAct = new QAction(QIcon(IMAGE_EXPIORTFRIEND), tr( "Recomend this Friend to..." ), &contextMnu );
      connect( recommendfriendAct , SIGNAL( triggered() ), this, SLOT( recommendfriend() ) );

      QAction* pastePersonAct = new QAction(QIcon(IMAGE_PASTELINK), tr( "Paste retroshare Link" ), &contextMnu );
      if(!RSLinkClipboard::empty(RetroShareLink::TYPE_PERSON)) {
          connect( pastePersonAct , SIGNAL( triggered() ), this, SLOT( pastePerson() ) );
      } else {
          pastePersonAct->setDisabled(true);
      }

      //QAction* profileviewAct = new QAction(QIcon(IMAGE_PEERINFO), tr( "Profile View" ), &contextMnu );
      //connect( profileviewAct , SIGNAL( triggered() ), this, SLOT( viewprofile() ) );

      QAction* exportfriendAct = new QAction(QIcon(IMAGE_EXPIORTFRIEND), tr( "Export Friend" ), &contextMnu );
      connect( exportfriendAct , SIGNAL( triggered() ), this, SLOT( exportfriend() ) );

      QAction* removefriendAct;
      if (c->type() == 0) {
          //this is a GPG key
          removefriendAct = new QAction(QIcon(IMAGE_REMOVEFRIEND), tr( "Deny Friend" ), &contextMnu );
      } else {
          removefriendAct = new QAction(QIcon(IMAGE_REMOVEFRIEND), tr( "Remove Friend Location" ), &contextMnu );
      }
      connect( removefriendAct , SIGNAL( triggered() ), this, SLOT( removefriend() ) );


      QWidget *widget = new QWidget();
      widget->setStyleSheet( ".QWidget{background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,stop:0 #FEFEFE, stop:1 #E8E8E8); border: 1px solid #CCCCCC;}");  
      
      QHBoxLayout *hbox = new QHBoxLayout(&contextMnu);
      hbox->setMargin(0);
      hbox->setSpacing(6);
    
      QLabel *iconLabel = new QLabel(&contextMnu);
      iconLabel->setPixmap(QPixmap(":/images/user/friends24.png"));
      iconLabel->setMaximumSize( iconLabel->frameSize().height() + 24, 24 );
      hbox->addWidget(iconLabel);

      QLabel *textLabel;
      if (c->type() == 0) {
          //this is a GPG key
         textLabel = new QLabel( tr("<strong>GPG Key</strong>"), &contextMnu );
      } else {
         textLabel = new QLabel( tr("<strong>RetroShare instance</strong>"), &contextMnu );
      }

      hbox->addWidget(textLabel);

      QSpacerItem *spacerItem = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);
      hbox->addItem(spacerItem); 
       
      widget->setLayout( hbox );
    
      QWidgetAction *widgetAction = new QWidgetAction(this); 
      widgetAction->setDefaultWidget(widget); 

      contextMnu.addAction( widgetAction);
      contextMnu.addAction( chatAct);
      contextMnu.addAction( sendMessageAct);
      contextMnu.addAction( configurefriendAct);
      //contextMnu.addAction( profileviewAct);
      if (c->type() == 0) {
          contextMnu.addAction( recommendfriendAct);
      } else {
          //this is a SSL key
          contextMnu.addAction( connectfriendAct);
      }
      contextMnu.addAction(pastePersonAct);
      contextMnu.addAction( removefriendAct);
      //contextMnu.addAction( exportfriendAct);
      contextMnu.addSeparator();
      contextMnu.addAction( expandAll);
      contextMnu.addAction( collapseAll);
      contextMnu.exec(QCursor::pos());
}

void MessengerWindow::updateMessengerDisplay()
{
	if(RsAutoUpdatePage::eventsLocked())
		return ;
        // add self nick and Avatar to Friends.
        RsPeerDetails pd ;
        if (rsPeers->getPeerDetails(rsPeers->getOwnId(),pd)) {
        
                QString titleStr("<span style=\"font-size:14pt; font-weight:500;"
                       "color:#FFFFFF;\">%1</span>");
                ui.nicklabel->setText(titleStr.arg(QString::fromStdString(pd.name) + tr(" - ") + QString::fromStdString(pd.location))) ;
        }

        insertPeers();
}

/* get the list of peers from the RsIface.  */
void  MessengerWindow::insertPeers()
{
    std::list<std::string> gpgFriends;
    std::list<std::string>::iterator it;
    std::list<StatusInfo> statusInfo;
    rsStatus->getStatus(statusInfo);

    // if(isIdle)
    //   QMessageBox::StandardButton sb = QMessageBox::warning ( NULL, tr("Idle"),
    //                  tr("You are Idle"), QMessageBox::Ok);

    if (!rsPeers) {
        /* not ready yet! */
        std::cerr << "PeersDialog::insertPeers() not ready yet : rsPeers unintialized."  << std::endl;
        return;
    }

    rsPeers->getGPGAcceptedList(gpgFriends);

    std::string sOwnId = rsPeers->getGPGOwnId();

    //add own gpg id, if we have more than on location (ssl client)
    std::list<std::string> ownSslContacts;
    rsPeers->getSSLChildListOfGPGId(sOwnId, ownSslContacts);
    if (ownSslContacts.size() > 0) {
        gpgFriends.push_back(sOwnId);
    }

    /* get a link to the table */
    QTreeWidget *peertreeWidget = ui.messengertreeWidget;

    bool bSortState = ui.actionSort_by_State->isChecked();

    //remove items that are not fiends anymore
    int index = 0;
    while (index < peertreeWidget->topLevelItemCount()) {
        std::string gpg_widget_id = peertreeWidget->topLevelItem(index)->data(COLUMN_DATA, ROLE_ID).toString().toStdString();
        std::list<std::string>::iterator gpgfriendIt;
        bool found = false;
        for (gpgfriendIt =  gpgFriends.begin(); gpgfriendIt != gpgFriends.end(); gpgfriendIt++) {
            if (gpg_widget_id == *gpgfriendIt) {
                found = true;
                break;
            }
        }
        if (!found) {
            delete (peertreeWidget->takeTopLevelItem(index));
        } else {
            index++;
        }
    }

    //add the gpg friends
    for(it = gpgFriends.begin(); it != gpgFriends.end(); it++) {
        //            if (*it == sOwnId) {
        //                continue;
        //            }

        /* make a widget per friend */
        QTreeWidgetItem *gpg_item = NULL;
        QTreeWidgetItem *gpg_item_loop = NULL;
        QString gpgid = QString::fromStdString(*it);
        for (int nIndex = 0; nIndex < peertreeWidget->topLevelItemCount(); nIndex++) {
            gpg_item_loop = peertreeWidget->topLevelItem(nIndex);
            if (gpg_item_loop->data(COLUMN_DATA, ROLE_ID).toString() == gpgid) {
                gpg_item = gpg_item_loop;
                break;
            }
        }

        RsPeerDetails detail;
        if ((!rsPeers->getPeerDetails(*it, detail) || !detail.accept_connection)
            && detail.gpg_id != sOwnId) {
            //don't accept anymore connection, remove from the view
            delete (peertreeWidget->takeTopLevelItem(peertreeWidget->indexOfTopLevelItem(gpg_item)));
            continue;
        }

        if (gpg_item == NULL) {
            gpg_item = new MyMessengerTreeWidgetItem(peertreeWidget, 0); //set type to 0 for custom popup menu
            gpg_item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicatorWhenChildless);
        }

        gpg_item -> setTextAlignment(COLUMN_NAME, Qt::AlignLeft | Qt::AlignVCenter );

        gpg_item -> setSizeHint(COLUMN_NAME,  QSize( 27,27 ) );

        /* not displayed, used to find back the item */
        gpg_item -> setData(COLUMN_DATA, ROLE_ID, QString::fromStdString(detail.id));

        //remove items that are not friends anymore
        int childIndex = 0;
        while (childIndex < gpg_item->childCount()) {
            std::string ssl_id = gpg_item->child(childIndex)->data(COLUMN_DATA, ROLE_ID).toString().toStdString();
            if (!rsPeers->isFriend(ssl_id)) {
                delete (gpg_item->takeChild(childIndex));
            } else {
                childIndex++;
            }
        }

        //update the childs (ssl certs)
        bool gpg_connected = false;
        bool gpg_online = false;
        std::list<std::string> sslContacts;
        rsPeers->getSSLChildListOfGPGId(detail.gpg_id, sslContacts);
        for(std::list<std::string>::iterator sslIt = sslContacts.begin(); sslIt != sslContacts.end(); sslIt++) {
            QTreeWidgetItem *sslItem = NULL;

            //find the corresponding sslItem child item of the gpg item
            bool newChild = true;
            for (int childIndex = 0; childIndex < gpg_item->childCount(); childIndex++) {
                if (gpg_item->child(childIndex)->data(COLUMN_DATA, ROLE_ID).toString().toStdString() == *sslIt) {
                    sslItem = gpg_item->child(childIndex);
                    newChild = false;
                    break;
                }
            }

            RsPeerDetails sslDetail;
            if (!rsPeers->getPeerDetails(*sslIt, sslDetail) || !rsPeers->isFriend(*sslIt)) {
                std::cerr << "Removing widget from the view : id : " << *sslIt << std::endl;
                if (sslItem) {
                    //child has disappeared, remove it from the gpg_item
                    gpg_item->removeChild(sslItem);
                }
                continue;
            }

            if (sslItem == NULL) {
                sslItem = new MyMessengerTreeWidgetItem(peertreeWidget, 1); //set type to 1 for custom popup menu
            }

            /* not displayed, used to find back the item */
            sslItem -> setData(COLUMN_DATA, ROLE_ID, QString::fromStdString(sslDetail.id));

            QString sCustomString = QString::fromStdString(rsMsgs->getCustomStateString(sslDetail.id));
            if (sCustomString != "") {
                sslItem -> setText( COLUMN_NAME, tr("location : ") + QString::fromStdString(sslDetail.location) + " " + QString::fromStdString(sslDetail.autoconnect) );
                sslItem -> setToolTip( COLUMN_NAME, tr("location : ") + QString::fromStdString(sslDetail.location) + tr(" - ") + sCustomString);
                gpg_item -> setText(COLUMN_NAME, QString::fromStdString(detail.name) + tr("\n") + sCustomString);
            } else {
                sslItem -> setText( COLUMN_NAME, tr("location : ") + QString::fromStdString(sslDetail.location) + " " + QString::fromStdString(sslDetail.autoconnect));
                sslItem -> setToolTip( COLUMN_NAME, tr("location : ") + QString::fromStdString(sslDetail.location));
                gpg_item -> setText(COLUMN_NAME, QString::fromStdString(detail.name) + tr("\n") + QString::fromStdString(sslDetail.location));
            }

            /* not displayed, used to find back the item */
            //sslItem -> setText(1, QString::fromStdString(sslDetail.autoconnect));

            int i;
            if (sslDetail.state & RS_PEER_STATE_CONNECTED) {
                sslItem->setHidden(false);
                gpg_connected = true;

                /* change color and icon */
                sslItem -> setIcon(COLUMN_NAME,(QIcon(":/images/connect_established.png")));
                QFont font;
                font.setBold(true);
                for(i = 0; i < COLUMN_COUNT; i++) {
                    sslItem -> setTextColor(i,(Qt::darkBlue));
                    sslItem -> setFont(i,font);
                }
            } else if (sslDetail.state & RS_PEER_STATE_ONLINE) {
                sslItem->setHidden(ui.actionHide_Offline_Friends->isChecked());
                gpg_online = true;

                QFont font;
                font.setBold(true);
                for(i = 0; i < COLUMN_COUNT; i++) {
                    sslItem -> setTextColor(i,(Qt::black));
                    sslItem -> setFont(i,font);
                }
            } else {
                sslItem->setHidden(ui.actionHide_Offline_Friends->isChecked());
                if (sslDetail.autoconnect !="Offline") {
                    sslItem -> setIcon(COLUMN_NAME, (QIcon(":/images/connect_creating.png")));
                } else {
                    sslItem -> setIcon(COLUMN_NAME, (QIcon(":/images/connect_no.png")));
                }

                QFont font;
                font.setBold(false);
                for(i = 0; i < COLUMN_COUNT; i++) {
                    sslItem -> setTextColor(i,(Qt::black));
                    sslItem -> setFont(i,font);
                }
            }

#ifdef PEERS_DEBUG
            std::cerr << "PeersDialog::insertPeers() inserting sslItem." << std::endl;
#endif
            /* add sl child to the list. If item is already in the list, it won't be duplicated thanks to Qt */
            gpg_item->addChild(sslItem);
            if (newChild) {
                gpg_item->setExpanded(true);
            }
        }

        int i = 0;
        if (gpg_connected) {
            gpg_item->setHidden(false);
            //gpg_item -> setText(COLUMN_STATE, tr("Online")); // set to online regardless on update

            std::list<StatusInfo>::iterator it = statusInfo.begin();


            for(; it != statusInfo.end() ; it++){

                std::list<std::string>::iterator cont_it = sslContacts.begin();

                // don't forget the kids
                for(;  cont_it != sslContacts.end(); cont_it++){


                    if((it->id == *cont_it) && (rsPeers->isOnline(*cont_it))) {

                        std::string status;
                        rsStatus->getStatusString(it->status, status);
                        gpg_item -> setText(COLUMN_STATE, QString::fromStdString(status));

                        QFont font;
                        font.setBold(true);

                        unsigned char *data = NULL;
                        int size = 0 ;
                        rsMsgs->getAvatarData(it->id ,data,size);

                        if(size != 0){
                            QPixmap avatar ;
                            avatar.loadFromData(data,size,"PNG") ;
                            QIcon avatar_icon(avatar);
                            QSize av_icon_size(100, 100);
                            gpg_item-> setIcon(1, avatar_icon);
                            delete[] data;

                        } else                         {
                            gpg_item -> setIcon(COLUMN_STATE,(QIcon(":/images/no_avatar_70.png")));
                        }
                        if(it->status == RS_STATUS_INACTIVE)
                        {
                            gpg_item -> setIcon(COLUMN_NAME,(QIcon(IMAGE_INACTIVE)));
                            gpg_item -> setToolTip(COLUMN_NAME, tr("Peer Idle"));
                            gpg_item->setData(COLUMN_NAME, ROLE_SORT, BuildStateSortString(bSortState, gpg_item->text(COLUMN_NAME), PEER_STATE_INACTIVE));

                            for(i = 0; i < COLUMN_COUNT; i++) {
                                gpg_item -> setTextColor(i,(Qt::gray));
                                gpg_item -> setFont(i,font);
                            }
                        }
                        else if(it->status == RS_STATUS_ONLINE)
                        {
                            gpg_item -> setIcon(COLUMN_NAME,(QIcon(IMAGE_ONLINE)));
                            gpg_item -> setToolTip(COLUMN_NAME, tr("Peer Online"));
                            gpg_item->setData(COLUMN_NAME, ROLE_SORT, BuildStateSortString(bSortState, gpg_item->text(COLUMN_NAME), PEER_STATE_ONLINE));

                            for(i = 0; i < COLUMN_COUNT; i++) {
                                gpg_item -> setTextColor(i,(Qt::darkBlue));
                                gpg_item -> setFont(i,font);
                            }
                        }
                        else if(it->status == RS_STATUS_AWAY)
                        {
                            gpg_item -> setIcon(COLUMN_NAME,(QIcon(IMAGE_AWAY)));
                            gpg_item -> setToolTip(COLUMN_NAME, tr("Peer Away"));
                            gpg_item->setData(COLUMN_NAME, ROLE_SORT, BuildStateSortString(bSortState, gpg_item->text(COLUMN_NAME), PEER_STATE_AWAY));

                            for(i = 0; i < COLUMN_COUNT; i++) {
                                gpg_item -> setTextColor(i,(Qt::gray));
                                gpg_item -> setFont(i,font);
                            }
                        }
                        else if(it->status == RS_STATUS_BUSY)
                        {
                            gpg_item -> setIcon(COLUMN_NAME,(QIcon(IMAGE_BUSY)));
                            gpg_item -> setToolTip(COLUMN_NAME, tr("Peer Busy"));
                            gpg_item->setData(COLUMN_NAME, ROLE_SORT, BuildStateSortString(bSortState, gpg_item->text(COLUMN_NAME), PEER_STATE_BUSY));

                            for(i = 0; i < COLUMN_COUNT; i++) {
                                gpg_item -> setTextColor(i,(Qt::gray));
                                gpg_item -> setFont(i,font);
                            }
                        }

                    }
                }
            }
        } else if (gpg_online) {
            gpg_item->setHidden(ui.actionHide_Offline_Friends->isChecked());
            gpg_item -> setIcon(COLUMN_NAME,(QIcon(IMAGE_AVAIBLE)));
            gpg_item->setData(COLUMN_NAME, ROLE_SORT, BuildStateSortString(bSortState, gpg_item->text(COLUMN_NAME), PEER_STATE_ONLINE));
            //gpg_item -> setText(COLUMN_STATE, tr("Available"));
            QFont font;
            font.setBold(true);
            for(i = 0; i < COLUMN_COUNT; i++) {
                gpg_item -> setTextColor(i,(Qt::black));
                gpg_item -> setFont(i,font);
            }
        } else {
            gpg_item->setHidden(ui.actionHide_Offline_Friends->isChecked());
            gpg_item -> setIcon(COLUMN_NAME,(QIcon(IMAGE_OFFLINE)));
            gpg_item->setData(COLUMN_NAME, ROLE_SORT, BuildStateSortString(bSortState, gpg_item->text(COLUMN_NAME), PEER_STATE_OFFLINE));
            //gpg_item -> setText(COLUMN_STATE, tr("Offline"));
            QFont font;
            font.setBold(false);
            for(i = 0; i < COLUMN_COUNT; i++) {
                gpg_item -> setTextColor(i,(Qt::black));
                gpg_item -> setFont(i,font);
            }
        }

        /* add gpg item to the list. If item is already in the list, it won't be duplicated thanks to Qt */
        peertreeWidget->addTopLevelItem(gpg_item);
    }

    if (ui.filterPatternLineEdit->text().isEmpty() == false) {
        FilterItems();
    }
}

/* Utility Fns */
std::string getPeersRsCertId(QTreeWidgetItem *i)
{
    std::string id = i -> data(COLUMN_DATA, ROLE_ID).toString().toStdString();
    return id;
}

/** Add a Friend ShortCut */
void MessengerWindow::addFriend()
{
    ConnectFriendWizard connwiz (this);

    connwiz.exec ();
}

/** Open a QFileDialog to browse for export a file. */
void MessengerWindow::exportfriend()
{
        QTreeWidgetItem *c = getCurrentPeer();

#ifdef PEERS_DEBUG
        std::cerr << "PeersDialog::exportfriend()" << std::endl;
#endif
	if (!c)
	{
#ifdef PEERS_DEBUG
                std::cerr << "PeersDialog::exportfriend() None Selected -- sorry" << std::endl;
#endif
		return;
	}

	std::string id = getPeersRsCertId(c);
	QString fileName = QFileDialog::getSaveFileName(this, tr("Save Certificate"), "",
	                                                     tr("Certificates (*.pqi)"));

	std::string file = fileName.toStdString();
	if (file != "")
	{
#ifdef PEERS_DEBUG
        	std::cerr << "PeersDialog::exportfriend() Saving to: " << file << std::endl;
        	std::cerr << std::endl;
#endif
		if (rsPeers)
		{
                        rsPeers->saveCertificateToFile(id, file);
		}
	}

}

void MessengerWindow::chatfriendproxy()
{
    chatfriend(getCurrentPeer());
}

void MessengerWindow::chatfriend(QTreeWidgetItem *pPeer)
{
    if (pPeer == NULL) {
        return;
    }

    std::string id = pPeer->data(COLUMN_DATA, ROLE_ID).toString().toStdString();
    PopupChatDialog::chatFriend(id);
}

QTreeWidgetItem *MessengerWindow::getCurrentPeer()
{
	/* get the current, and extract the Id */

	/* get a link to the table */
        QTreeWidget *peerWidget = ui.messengertreeWidget;
        QTreeWidgetItem *item = peerWidget -> currentItem();
        if (!item)
        {
#ifdef PEERS_DEBUG
		std::cerr << "Invalid Current Item" << std::endl;
#endif
		return NULL;
	}

#ifdef PEERS_DEBUG
	/* Display the columns of this item. */
	std::ostringstream out;
        out << "CurrentPeerItem: " << std::endl;

	for(int i = 1; i < 6; i++)
	{
		QString txt = item -> text(i);
		out << "\t" << i << ":" << txt.toStdString() << std::endl;
	}
	std::cerr << out.str();
#endif
	return item;
}

/* So from the Peers Dialog we can call the following control Functions:
 * (1) Remove Current.              FriendRemove(id)
 * (2) Allow/DisAllow.              FriendStatus(id, accept)
 * (2) Connect.                     FriendConnectAttempt(id, accept)
 * (3) Set Address.                 FriendSetAddress(id, str, port)
 * (4) Set Trust.                   FriendTrustSignature(id, bool)
 * (5) Configure (GUI Only) -> 3/4
 *
 * All of these rely on the finding of the current Id.
 */


void MessengerWindow::removefriend()
{
        QTreeWidgetItem *c = getCurrentPeer();
#ifdef PEERS_DEBUG
        std::cerr << "PeersDialog::removefriend()" << std::endl;
#endif
	if (!c)
	{
#ifdef PEERS_DEBUG
        	std::cerr << "PeersDialog::removefriend() Noone Selected -- sorry" << std::endl;
#endif
		return;
	}

	if (rsPeers)
	{
		rsPeers->removeFriend(getPeersRsCertId(c));
		emit friendsUpdated() ;
	}
}

void MessengerWindow::connectfriend()
{
	QTreeWidgetItem *c = getCurrentPeer();
#ifdef PEERS_DEBUG
	std::cerr << "PeersDialog::connectfriend()" << std::endl;
#endif
	if (!c)
	{
#ifdef PEERS_DEBUG
        	std::cerr << "PeersDialog::connectfriend() Noone Selected -- sorry" << std::endl;
#endif
		return;
	}

	if (rsPeers)
	{
		rsPeers->connectAttempt(getPeersRsCertId(c));
                c -> setIcon(COLUMN_NAME,(QIcon(IMAGE_CONNECT2)));
	}
}

/* GUI stuff -> don't do anything directly with Control */
void MessengerWindow::configurefriend()
{
	ConfCertDialog::show(getPeersRsCertId(getCurrentPeer()));
}

void MessengerWindow::recommendfriend()
{
    QTreeWidgetItem *peer = getCurrentPeer();

    if (!peer)
        return;

    std::list <std::string> ids;
    ids.push_back(peer->data(COLUMN_DATA, ROLE_ID).toString().toStdString());
    MessageComposer::recommendFriend(ids);
}

void MessengerWindow::pastePerson()
{
    RSLinkClipboard::process(RetroShareLink::TYPE_PERSON, RSLINK_PROCESS_NOTIFY_ERROR);
}

void MessengerWindow::updatePeersAvatar(const QString& peer_id)
{
	std::cerr << "PeersDialog: Got notified of new avatar for peer " << peer_id.toStdString() << std::endl ;

        PopupChatDialog *pcd = PopupChatDialog::getPrivateChat(peer_id.toStdString(),rsPeers->getPeerName(peer_id.toStdString()), 0);
	pcd->updatePeerAvatar(peer_id.toStdString());
}

//============================================================================


/** Overloads the default show  */
void MessengerWindow::show()
{

  if (!this->isVisible()) {
    QWidget::show();
  } else {
    QWidget::activateWindow();
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    QWidget::raise();
  }
}

void MessengerWindow::closeEvent (QCloseEvent * event)
{
	//Settings->saveWidgetInformation(this);

    hide();
    event->ignore();
}

/** Shows Share Manager */
void MessengerWindow::openShareManager()
{
	ShareManager::showYourself();

}

void MessengerWindow::sendMessage()
{
    QTreeWidgetItem *peer = getCurrentPeer();

    if (!peer)
        return;

    std::string id = peer->data(COLUMN_DATA, ROLE_ID).toString().toStdString();
    MessageComposer::msgFriend(id);
}

LogoBar & MessengerWindow::getLogoBar() const {
	return *_rsLogoBarmessenger;
}

void MessengerWindow::changeAvatarClicked() 
{

	updateAvatar();
}

void MessengerWindow::updateAvatar()
{
	unsigned char *data = NULL;
	int size = 0 ;

	rsMsgs->getOwnAvatarData(data,size); 

	std::cerr << "Image size = " << size << std::endl ;

	if(size == 0)
	   std::cerr << "Got no image" << std::endl ;

	// set the image
	QPixmap pix ;
	pix.loadFromData(data,size,"PNG") ;
	ui.avatarButton->setIcon(pix); // writes image into ba in PNG format

	delete[] data ;
}

void MessengerWindow::getAvatar()
{
	QString fileName = QFileDialog::getOpenFileName(this, "Load File", QDir::homePath(), "Pictures (*.png *.xpm *.jpg)");
	if(!fileName.isEmpty())
	{
		picture = QPixmap(fileName).scaled(82,82, Qt::IgnoreAspectRatio,Qt::SmoothTransformation);

		std::cerr << "Sending avatar image down the pipe" << std::endl ;

		// send avatar down the pipe for other peers to get it.
		QByteArray ba;
		QBuffer buffer(&ba);
		buffer.open(QIODevice::WriteOnly);
		picture.save(&buffer, "PNG"); // writes image into ba in PNG format

		std::cerr << "Image size = " << ba.size() << std::endl ;

		rsMsgs->setOwnAvatarData((unsigned char *)(ba.data()),ba.size()) ;	// last char 0 included.

		updateAvatar() ;
	}
}

/** Loads own personal status message */
void MessengerWindow::loadmystatusmessage()
{ 
    ui.messagelineEdit->setText(QString::fromStdString(rsMsgs->getCustomStateString()));
}

/** Save own status message */
void MessengerWindow::savestatusmessage()
{
  Settings->setValueToGroup("Profile", "StatusMessage",ui.messagelineEdit->text());
	
  rsMsgs->setCustomStateString(ui.messagelineEdit->text().toStdString());
}

void MessengerWindow::statusChanged(int index)
{
    if (index < 0) {
        return;
    }

    MainWindow *pMainWindow = MainWindow::getInstance();
    if (pMainWindow) {
        pMainWindow->setStatus(ui.statuscomboBox, ui.statuscomboBox->itemData(index, Qt::UserRole).toInt());
    }
}

void MessengerWindow::on_actionSort_Peers_Descending_Order_activated()
{
  ui.messengertreeWidget->sortItems ( COLUMN_NAME, Qt::DescendingOrder );
}

void MessengerWindow::on_actionSort_Peers_Ascending_Order_activated()
{
  ui.messengertreeWidget->sortItems ( COLUMN_NAME, Qt::AscendingOrder );
}

void MessengerWindow::on_actionRoot_is_decorated_activated()
{
    ui.messengertreeWidget->setRootIsDecorated(ui.actionRoot_is_decorated->isChecked());
}

void MessengerWindow::displayMenu()
{
    QMenu *lookmenu = new QMenu();
    lookmenu->addAction(ui.actionSort_Peers_Descending_Order);
    lookmenu->addAction(ui.actionSort_Peers_Ascending_Order);
    lookmenu->addAction(ui.actionSort_by_State);
    lookmenu->addAction(ui.actionHide_Offline_Friends);
    lookmenu->addAction(ui.actionRoot_is_decorated);

    ui.displaypushButton->setMenu(lookmenu);
}

/* clear Filter */
void MessengerWindow::clearFilter()
{
    ui.filterPatternLineEdit->clear();
    ui.filterPatternLineEdit->setFocus();
}

void MessengerWindow::filterRegExpChanged()
{

    QString text = ui.filterPatternLineEdit->text();

    if (text.isEmpty()) {
        ui.clearButton->hide();
    } else {
        ui.clearButton->show();
    }

    FilterItems();
}

void MessengerWindow::FilterItems()
{
    QString sPattern = ui.filterPatternLineEdit->text();

    int nCount = ui.messengertreeWidget->topLevelItemCount ();
    for (int nIndex = 0; nIndex < nCount; nIndex++) {
        FilterItem(ui.messengertreeWidget->topLevelItem(nIndex), sPattern);
    }
}

bool MessengerWindow::FilterItem(QTreeWidgetItem *pItem, QString &sPattern)
{
    bool bVisible = true;

    if (sPattern.isEmpty() == false) {
        if (pItem->text(0).contains(sPattern, Qt::CaseInsensitive) == false) {
            bVisible = false;
        }
    }

    int nVisibleChildCount = 0;
    int nCount = pItem->childCount();
    for (int nIndex = 0; nIndex < nCount; nIndex++) {
        if (FilterItem(pItem->child(nIndex), sPattern)) {
            nVisibleChildCount++;
        }
    }

    if (bVisible || nVisibleChildCount) {
        pItem->setHidden(false);
    } else {
        pItem->setHidden(true);
    }

    return (bVisible || nVisibleChildCount);
}
