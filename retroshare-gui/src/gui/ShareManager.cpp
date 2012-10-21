/****************************************************************
 *  RetroShare is distributed under the following license:
 *
 *  Copyright (C) 2006, 2007 crypton
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

#include <QContextMenuEvent>
#include <QMenu>
#include <QCheckBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QUrl>

#include <retroshare/rsfiles.h>

#include "ShareManager.h"
#include "ShareDialog.h"
#include "settings/rsharesettings.h"
#include <gui/common/GroupFlagsWidget.h>

/* Images for context menu icons */
#define IMAGE_CANCEL               ":/images/delete.png"
#define IMAGE_EDIT                 ":/images/edit_16.png"

#define COLUMN_PATH         0
#define COLUMN_VIRTUALNAME  1
#define COLUMN_SHARE_FLAGS  2
#define COLUMN_GROUPS       3

ShareManager *ShareManager::_instance = NULL ;

/** Default constructor */
ShareManager::ShareManager(QWidget *parent, Qt::WFlags flags)
  : QDialog(parent, flags)
{
    /* Invoke Qt Designer generated QObject setup routine */
    ui.setupUi(this);

    ui.headerFrame->setHeaderImage(QPixmap(":/images/fileshare64.png"));
    ui.headerFrame->setHeaderText(tr("Share Manager"));

    isLoading = false;
    load();

    Settings->loadWidgetInformation(this);

    connect(ui.addButton, SIGNAL(clicked( bool ) ), this , SLOT( showShareDialog() ) );
    connect(ui.editButton, SIGNAL(clicked( bool ) ), this , SLOT( editShareDirectory() ) );
    connect(ui.removeButton, SIGNAL(clicked( bool ) ), this , SLOT( removeShareDirectory() ) );
    connect(ui.closeButton, SIGNAL(clicked()), this, SLOT(close()));

    connect(ui.shareddirList, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(shareddirListCostumPopupMenu(QPoint)));
    connect(ui.shareddirList, SIGNAL(currentCellChanged(int,int,int,int)), this, SLOT(shareddirListCurrentCellChanged(int,int,int,int)));

    ui.editButton->setEnabled(false);
    ui.removeButton->setEnabled(false);

    QHeaderView* header = ui.shareddirList->horizontalHeader();
    header->setResizeMode( COLUMN_PATH, QHeaderView::Stretch);

    //header->setResizeMode(COLUMN_NETWORKWIDE, QHeaderView::Fixed);
    //header->setResizeMode(COLUMN_BROWSABLE, QHeaderView::Fixed);

    header->setHighlightSections(false);

    setAcceptDrops(true);
    setAttribute(Qt::WA_DeleteOnClose, true);
}

ShareManager::~ShareManager()
{
    _instance = NULL;

    Settings->saveWidgetInformation(this);
}

void ShareManager::shareddirListCostumPopupMenu( QPoint /*point*/ )
{
    QMenu contextMnu( this );

    QAction *editAct = new QAction(QIcon(IMAGE_EDIT), tr( "Edit" ), &contextMnu );
    connect( editAct , SIGNAL( triggered() ), this, SLOT( editShareDirectory() ) );

    QAction *removeAct = new QAction(QIcon(IMAGE_CANCEL), tr( "Remove" ), &contextMnu );
    connect( removeAct , SIGNAL( triggered() ), this, SLOT( removeShareDirectory() ) );

    contextMnu.addAction( editAct );
    contextMnu.addAction( removeAct );

    contextMnu.exec(QCursor::pos());
}

/** Loads the settings for this page */
void ShareManager::load()
{
    isLoading = true;
    std::cerr << "ShareManager:: In load !!!!!" << std::endl ;

    std::list<SharedDirInfo>::const_iterator it;
    std::list<SharedDirInfo> dirs;
    rsFiles->getSharedDirectories(dirs);

    /* get a link to the table */
    QTableWidget *listWidget = ui.shareddirList;

    /* set new row count */
    listWidget->setRowCount(dirs.size());

    connect(this,SIGNAL(itemClicked(QTableWidgetItem*)),this,SLOT(updateFlags(QTableWidgetItem*))) ;

    int row=0 ;
    for(it = dirs.begin(); it != dirs.end(); it++,++row)
    {
        listWidget->setItem(row, COLUMN_PATH, new QTableWidgetItem(QString::fromUtf8((*it).filename.c_str())));
        listWidget->setItem(row, COLUMN_VIRTUALNAME, new QTableWidgetItem(QString::fromUtf8((*it).virtualname.c_str())));

		  QWidget* widget = new GroupFlagsWidget(NULL,(*it).shareflags);

		  //listWidget->setRowHeight(row, 32);
		  listWidget->setCellWidget(row, COLUMN_SHARE_FLAGS, widget);
    }

    //ui.incomingDir->setText(QString::fromStdString(rsFiles->getDownloadDirectory()));

    listWidget->update(); /* update display */
    update();

    isLoading = false ;
}

void ShareManager::showYourself()
{
    if(_instance == NULL)
        _instance = new ShareManager(NULL,0) ;

    _instance->show() ;
    _instance->activateWindow();
}

/*static*/ void ShareManager::postModDirectories(bool update_local)
{
   if (_instance == NULL || _instance->isHidden()) {
       return;
   }

   if (update_local) {
       _instance->load();
   }
}

void ShareManager::updateFlags(bool b)
{
    if(isLoading)
        return ;

    std::cerr << "Updating flags (b=" << b << ") !!!" << std::endl ;

    std::list<SharedDirInfo>::iterator it;
    std::list<SharedDirInfo> dirs;
    rsFiles->getSharedDirectories(dirs);

    int row=0 ;
    for(it = dirs.begin(); it != dirs.end(); it++,++row)
    {
        std::cerr << "Looking for row=" << row << ", file=" << (*it).filename << ", flags=" << (*it).shareflags << std::endl ;
        uint32_t current_flags = (dynamic_cast<GroupFlagsWidget*>(ui.shareddirList->cellWidget(row,COLUMN_SHARE_FLAGS)->children().front()))->flags() ;

        if( (*it).shareflags ^ current_flags )
        {
            (*it).shareflags = current_flags ;
            rsFiles->updateShareFlags(*it) ;	// modifies the flags

            std::cout << "Updating share flags for directory " << (*it).filename << " to " << current_flags << std::endl ;
        }
    }
}

void ShareManager::editShareDirectory()
{
    /* id current dir */
    int row = ui.shareddirList->currentRow();
    QTableWidgetItem *item = ui.shareddirList->item(row, COLUMN_PATH);

    if (item) {
        std::string filename = item->text().toUtf8().constData();

        std::list<SharedDirInfo> dirs;
        rsFiles->getSharedDirectories(dirs);

        std::list<SharedDirInfo>::const_iterator it;
        for (it = dirs.begin(); it != dirs.end(); it++) {
            if (it->filename == filename) {
                /* file name found, show dialog */
                ShareDialog sharedlg (it->filename, this);
                sharedlg.exec();
                break;
            }
        }
    }
}

void ShareManager::removeShareDirectory()
{
    /* id current dir */
    /* ask for removal */
    QTableWidget *listWidget = ui.shareddirList;
    int row = listWidget -> currentRow();
    QTableWidgetItem *qdir = listWidget->item(row, COLUMN_PATH);

    if (qdir)
    {
        if ((QMessageBox::question(this, tr("Warning!"),tr("Do you really want to stop sharing this directory ?"),QMessageBox::Yes|QMessageBox::No, QMessageBox::Yes))== QMessageBox::Yes)
        {
            rsFiles->removeSharedDirectory( qdir->text().toUtf8().constData());
            load();
        }
    }
}

void ShareManager::showEvent(QShowEvent *event)
{
    if (!event->spontaneous())
    {
        load();
    }
}

void ShareManager::showShareDialog()
{
    ShareDialog sharedlg ("", this);
    sharedlg.exec();
}

void ShareManager::shareddirListCurrentCellChanged(int currentRow, int currentColumn, int previousRow, int previousColumn)
{
    Q_UNUSED(currentColumn);
    Q_UNUSED(previousRow);
    Q_UNUSED(previousColumn);

    if (currentRow >= 0) {
        ui.editButton->setEnabled(true);
        ui.removeButton->setEnabled(true);
    } else {
        ui.editButton->setEnabled(false);
        ui.removeButton->setEnabled(false);
    }
}

void ShareManager::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls()) {
		event->acceptProposedAction();
	}
}

void ShareManager::dropEvent(QDropEvent *event)
{
	if (!(Qt::CopyAction & event->possibleActions())) {
		/* can't do it */
		return;
	}

	QStringList formats = event->mimeData()->formats();
	QStringList::iterator it;

	bool errorShown = false;

	if (event->mimeData()->hasUrls()) {
		QList<QUrl> urls = event->mimeData()->urls();
		QList<QUrl>::iterator it;
		for (it = urls.begin(); it != urls.end(); it++) {
			QString localpath = it->toLocalFile();

			if (localpath.isEmpty() == false) {
				QDir dir(localpath);
				if (dir.exists()) {
					SharedDirInfo sdi;
					sdi.filename = localpath.toUtf8().constData();
					sdi.virtualname.clear();

					sdi.shareflags = 0;

					/* add new share */
					rsFiles->addSharedDirectory(sdi);
				} else if (QFile::exists(localpath)) {
					if (errorShown == false) {
						QMessageBox mb(tr("Drop file error."), tr("File can't be dropped, only directories are accepted."), QMessageBox::Information, QMessageBox::Ok, 0, 0, this);
						mb.exec();
						errorShown = true;
					}
				} else {
					QMessageBox mb(tr("Drop file error."), tr("Directory not found or directory name not accepted."), QMessageBox::Information, QMessageBox::Ok, 0, 0, this);
					mb.exec();
				}
			}
		}
	}

	event->setDropAction(Qt::CopyAction);
	event->accept();
}
