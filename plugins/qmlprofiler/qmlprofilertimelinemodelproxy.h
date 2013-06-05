/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/


#ifndef QMLPROFILERTIMELINEMODELPROXY_H
#define QMLPROFILERTIMELINEMODELPROXY_H

#include <QObject>
#include <qmldebug/qmlprofilereventtypes.h>
#include <qmldebug/qmlprofilereventlocation.h>
//#include <QHash>
//#include <QVector>
#include <QVariantList>
//#include <QVariantMap>


namespace QmlProfiler {
namespace Internal {

class QmlProfilerModelManager;

class QmlProfilerTimelineModelProxy : public QObject
{
    Q_PROPERTY(bool empty READ empty NOTIFY emptyChanged)

    Q_OBJECT
public:
    struct QmlRangeEventData
    {
        QString displayName;
//        QString eventHashStr;
        QString details;
        QmlDebug::QmlEventLocation location;
        QmlDebug::QmlEventType eventType;

        int eventId;  // separate
    };

    struct QmlRangeEventStartInstance {
        qint64 startTime;
        qint64 duration;

//        int endTimeIndex;

        // not-expanded, per type
        int displayRowExpanded;
        int displayRowCollapsed;
        int baseEventIndex; // used by findfirstindex


//        QmlRangeEventData *statsInfo;
        int eventId;

        // animation-related data
//        int frameRate;
//        int animationCount;

        int bindingLoopHead;
    };

    struct QmlRangeEventEndInstance {
        int startTimeIndex;
        qint64 endTime;
    };

//    struct QmlRangedEvent {
//        int bindingType; // TODO: only makes sense for bindings!
//        QString displayName;
//        QString eventHashStr;
//        QString details;
//        QmlDebug::QmlEventLocation location;
//        QmlDebug::QmlEventType eventType;
//        //int eventType;

//        qint64 duration;
//    };

    QmlProfilerTimelineModelProxy(QmlProfilerModelManager *modelManager, QObject *parent = 0);
    ~QmlProfilerTimelineModelProxy();

    const QVector<QmlRangeEventStartInstance> getData() const;
    const QVector<QmlRangeEventStartInstance> getData(qint64 fromTime, qint64 toTime) const;
    void loadData();
    Q_INVOKABLE int count() const;
    void clear();


// QML interface
    bool empty() const;

    Q_INVOKABLE qint64 lastTimeMark() const;
    Q_INVOKABLE qint64 traceStartTime() const;
    Q_INVOKABLE qint64 traceEndTime() const;
    Q_INVOKABLE qint64 traceDuration() const;
    Q_INVOKABLE int getState() const;

    Q_INVOKABLE void setExpanded(int category, bool expanded);
    Q_INVOKABLE int categoryDepth(int categoryIndex) const;
    Q_INVOKABLE int categoryCount() const;
    Q_INVOKABLE const QString categoryLabel(int categoryIndex) const;

    int findFirstIndex(qint64 startTime) const;
    int findFirstIndexNoParents(qint64 startTime) const;
    int findLastIndex(qint64 endTime) const;

    int getEventType(int index) const;
    int getEventRow(int index) const;
    Q_INVOKABLE qint64 getDuration(int index) const;
    Q_INVOKABLE qint64 getStartTime(int index) const;
    Q_INVOKABLE qint64 getEndTime(int index) const;
    Q_INVOKABLE int getEventId(int index) const;
    int getBindingLoopDest(int index) const;

    const QmlProfilerTimelineModelProxy::QmlRangeEventData &getRangeEventData(int index) const;
    Q_INVOKABLE const QVariantList getLabelsForCategory(int category) const;

    Q_INVOKABLE const QVariantList getEventDetails(int index) const;


signals:
    void countChanged();
    void dataAvailable();
    void stateChanged();
    void emptyChanged();
    void expandedChanged();

private slots:
    void dataChanged();

private:
    class QmlProfilerTimelineModelProxyPrivate;
    QmlProfilerTimelineModelProxyPrivate *d;

};

}
}

#endif
