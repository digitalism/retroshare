/****************************************************************
 * This file is distributed under the following license:
 *
 * Copyright (c) 2010, RetroShare Team
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

#ifndef GROUPTREEWIDGET_H
#define GROUPTREEWIDGET_H

#include <QWidget>
#include <QIcon>
#include <QTreeWidgetItem>
#include <QDateTime>

namespace Ui {
	class GroupTreeWidget;
}

class GroupItemInfo
{
public:
	GroupItemInfo()
	{
		popularity = 0;
		privatekey = false;
	}

public:
	QString   id;
	QString   name;
	QString   description;
	int       popularity;
	QDateTime lastpost;
	QIcon     icon;
	bool      privatekey;
};

class GroupTreeWidget : public QWidget
{
	Q_OBJECT

public:
	GroupTreeWidget(QWidget *parent = 0);
	~GroupTreeWidget();

	// Add a new category item
	QTreeWidgetItem *addCategoryItem(const QString &name, const QIcon &icon, bool expand);
	// Get id of item
	QString itemId(QTreeWidgetItem *item);
	// Fill items of a group
	void fillGroupItems(QTreeWidgetItem *categoryItem, const QList<GroupItemInfo> &itemList);
	// Set the unread count of an item
	void setUnreadCount(QTreeWidgetItem *item, int unreadCount);

signals:
    void treeCustomContextMenuRequested(const QPoint &pos);
    void treeCurrentItemChanged(const QString &id);

protected:
	void changeEvent(QEvent *e);

private slots:
	void customContextMenuRequested(const QPoint &pos);
    void currentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
	void filterChanged();
	void clearFilter();

private:
	void calculateScore(QTreeWidgetItem *item);

private:
	Ui::GroupTreeWidget *ui;
};

class GroupTreeWidgetItem : public QTreeWidgetItem
{
public:
	GroupTreeWidgetItem();

	/**
	 * reimplementing comparison operator so GroupTreeWidgetItem can be ordered in terms
	 * of occurences of property filterText in its data columns
	 */
	bool operator<(const QTreeWidgetItem &other) const;
};

#endif // GROUPTREEWIDGET_H