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

#include "qmlprofilertimelinemodelproxy.h"
#include "qmlprofilermodelmanager.h"
#include "qmlprofilersimplemodel.h"

#include <QVector>
#include <QHash>
#include <QUrl>
#include <QString>
#include <QStack>

#include <QDebug>

namespace QmlProfiler {
namespace Internal {

struct CategorySpan {
    bool expanded;
    int expandedRows;
    int contractedRows;
};

class QmlProfilerTimelineModelProxy::QmlProfilerTimelineModelProxyPrivate
{
public:
    QmlProfilerTimelineModelProxyPrivate(QmlProfilerTimelineModelProxy *qq) : q(qq) {}
    ~QmlProfilerTimelineModelProxyPrivate() {}

    // convenience functions
    void prepare();
    void computeNestingContracted();
    void computeExpandedLevels();
    void computeBaseEventIndexes();
    void buildEndTimeList();
    void findBindingLoops();

    QString displayTime(double time);

    QVector <QmlProfilerTimelineModelProxy::QmlRangeEventData> eventDict;
    QVector <QString> eventHashes;
    QVector <QmlProfilerTimelineModelProxy::QmlRangeEventStartInstance> startTimeData;
    QVector <QmlProfilerTimelineModelProxy::QmlRangeEventEndInstance> endTimeData;
    QVector <CategorySpan> categorySpan;

    QmlProfilerModelManager *modelManager;
    QmlProfilerTimelineModelProxy *q;
};

QmlProfilerTimelineModelProxy::QmlProfilerTimelineModelProxy(QmlProfilerModelManager *modelManager, QObject *parent)
    : QObject(parent), d(new QmlProfilerTimelineModelProxyPrivate(this))
{
    d->modelManager = modelManager;
    connect(d->modelManager->simpleModel(),SIGNAL(changed()),this,SLOT(dataChanged()));
}

QmlProfilerTimelineModelProxy::~QmlProfilerTimelineModelProxy()
{
    delete d;
}

const QVector<QmlProfilerTimelineModelProxy::QmlRangeEventStartInstance> QmlProfilerTimelineModelProxy::getData() const
{
    return d->startTimeData;
}

const QVector<QmlProfilerTimelineModelProxy::QmlRangeEventStartInstance> QmlProfilerTimelineModelProxy::getData(qint64 fromTime, qint64 toTime) const
{
    int fromIndex = findFirstIndex(fromTime);
    int toIndex = findLastIndex(toTime);
    if (fromIndex != -1 && toIndex > fromIndex)
        return d->startTimeData.mid(fromIndex, toIndex - fromIndex + 1);
    else
        return QVector<QmlProfilerTimelineModelProxy::QmlRangeEventStartInstance>();
}

void QmlProfilerTimelineModelProxy::clear()
{
    d->eventDict.clear();
    d->eventHashes.clear();
    d->startTimeData.clear();
    d->endTimeData.clear();
    d->categorySpan.clear();
}

void QmlProfilerTimelineModelProxy::dataChanged()
{
    loadData();

    emit stateChanged();
    emit dataAvailable();
    emit emptyChanged();
}

void QmlProfilerTimelineModelProxy::QmlProfilerTimelineModelProxyPrivate::prepare()
{
    categorySpan.clear();
    for (int i = 0; i < QmlDebug::MaximumQmlEventType; i++) {
        CategorySpan newCategory = {false, 1, 1};
        categorySpan << newCategory;
    }
}

bool compareStartTimes(const QmlProfilerTimelineModelProxy::QmlRangeEventStartInstance&t1, const QmlProfilerTimelineModelProxy::QmlRangeEventStartInstance &t2)
{
    return t1.startTime < t2.startTime;
}

bool compareEndTimes(const QmlProfilerTimelineModelProxy::QmlRangeEventEndInstance &t1, const QmlProfilerTimelineModelProxy::QmlRangeEventEndInstance &t2)
{
    return t1.endTime < t2.endTime;
}

void QmlProfilerTimelineModelProxy::loadData()
{
    clear();
    QmlProfilerSimpleModel *simpleModel = d->modelManager->simpleModel();
    if (simpleModel->isEmpty())
        return;

    int lastEventId = 0;

    d->prepare();

    // collect events
    const QVector<QmlProfilerSimpleModel::QmlEventData> eventList = simpleModel->getEvents();
    foreach (const QmlProfilerSimpleModel::QmlEventData &event, eventList) {
        QString eventHash = QmlProfilerSimpleModel::getHashString(event);

        // store in dictionary
        if (!d->eventHashes.contains(eventHash)) {
            QmlRangeEventData rangeEventData = {
                event.displayName,
                event.data.join(QLatin1String(" ")),
                event.location,
                (QmlDebug::QmlEventType)event.eventType,
//                event.bindingType,
//                1,
                lastEventId++ // event id
            };
            d->eventDict << rangeEventData;
            d->eventHashes << eventHash;
        }

        // store starttime-based instance
        QmlRangeEventStartInstance eventStartInstance = {
            event.startTime,
            event.duration,
            QmlDebug::Constants::QML_MIN_LEVEL, // displayRowExpanded;
            QmlDebug::Constants::QML_MIN_LEVEL, // displayRowCollapsed;
            1,
            d->eventHashes.indexOf(eventHash), // event id
            -1  // bindingLoopHead
        };
        d->startTimeData.append(eventStartInstance);
    }

    qSort(d->startTimeData.begin(), d->startTimeData.end(), compareStartTimes);

    // compute nestingLevel - nonexpanded
    d->computeNestingContracted();

    // compute nestingLevel - expanded
    d->computeExpandedLevels();

    d->computeBaseEventIndexes();

    // populate endtimelist
    d->buildEndTimeList();

    d->findBindingLoops();

    emit countChanged();
}

void QmlProfilerTimelineModelProxy::QmlProfilerTimelineModelProxyPrivate::computeNestingContracted()
{
    int i;
    int eventCount = startTimeData.count();

    QHash<int, qint64> endtimesPerLevel;
    QList<int> nestingLevels;
    QList< QHash<int, qint64> > endtimesPerNestingLevel;
    int level = QmlDebug::Constants::QML_MIN_LEVEL;
    endtimesPerLevel[QmlDebug::Constants::QML_MIN_LEVEL] = 0;
    int lastBaseEventIndex = 0;
    qint64 lastBaseEventEndTime = modelManager->traceTime()->startTime();

    for (i = 0; i < QmlDebug::MaximumQmlEventType; i++) {
        nestingLevels << QmlDebug::Constants::QML_MIN_LEVEL;
        QHash<int, qint64> dummyHash;
        dummyHash[QmlDebug::Constants::QML_MIN_LEVEL] = 0;
        endtimesPerNestingLevel << dummyHash;
    }

    for (i = 0; i < eventCount; i++) {
        qint64 st = startTimeData[i].startTime;
        int type = q->getEventType(i);

        // general level
        if (endtimesPerLevel[level] > st) {
            level++;
        } else {
            while (level > QmlDebug::Constants::QML_MIN_LEVEL && endtimesPerLevel[level-1] <= st)
                level--;
        }
        endtimesPerLevel[level] = st + startTimeData[i].duration;

        // per type
        if (endtimesPerNestingLevel[type][nestingLevels[type]] > st) {
            nestingLevels[type]++;
        } else {
            while (nestingLevels[type] > QmlDebug::Constants::QML_MIN_LEVEL &&
                   endtimesPerNestingLevel[type][nestingLevels[type]-1] <= st)
                nestingLevels[type]--;
        }
        endtimesPerNestingLevel[type][nestingLevels[type]] =
                st + startTimeData[i].duration;

        startTimeData[i].displayRowCollapsed = nestingLevels[type];

        // todo: this should go to another method
        if (level == QmlDebug::Constants::QML_MIN_LEVEL) {
            if (lastBaseEventEndTime < startTimeData[i].startTime) {
                lastBaseEventIndex = i;
                lastBaseEventEndTime = startTimeData[i].startTime + startTimeData[i].duration;
            }
        }
        startTimeData[i].baseEventIndex = lastBaseEventIndex;
    }

    // nestingdepth
    for (i = 0; i < eventCount; i++) {
        int eventType = q->getEventType(i);
        if (categorySpan[eventType].contractedRows <= startTimeData[i].displayRowCollapsed)
            categorySpan[eventType].contractedRows = startTimeData[i].displayRowCollapsed + 1;
    }
}

void QmlProfilerTimelineModelProxy::QmlProfilerTimelineModelProxyPrivate::computeExpandedLevels()
{
    QHash<int, int> eventRow;
    int eventCount = startTimeData.count();
    for (int i = 0; i < eventCount; i++) {
        int eventId = startTimeData[i].eventId;
        int eventType = eventDict[eventId].eventType;
        if (!eventRow.contains(eventId)) {
            eventRow[eventId] = categorySpan[eventType].expandedRows++;
        }
        startTimeData[i].displayRowExpanded = eventRow[eventId];
    }
}

void QmlProfilerTimelineModelProxy::QmlProfilerTimelineModelProxyPrivate::computeBaseEventIndexes()
{
    // TODO
}

void QmlProfilerTimelineModelProxy::QmlProfilerTimelineModelProxyPrivate::buildEndTimeList()
{
    endTimeData.clear();

    int eventCount = startTimeData.count();
    for (int i = 0; i < eventCount; i++) {
        QmlProfilerTimelineModelProxy::QmlRangeEventEndInstance endInstance = {
            i,
            startTimeData[i].startTime + startTimeData[i].duration
        };

        endTimeData << endInstance;
    }

    qSort(endTimeData.begin(), endTimeData.end(), compareEndTimes);
}

void QmlProfilerTimelineModelProxy::QmlProfilerTimelineModelProxyPrivate::findBindingLoops()
{
    typedef QPair<QString, int> CallStackEntry;
    QStack<CallStackEntry> callStack;

    for (int i = 0; i < startTimeData.size(); ++i) {
        QmlRangeEventStartInstance *event = &startTimeData[i];

        QmlProfilerTimelineModelProxy::QmlRangeEventData data = eventDict.at(event->eventId);

        static QVector<QmlDebug::QmlEventType> acceptedTypes =
                QVector<QmlDebug::QmlEventType>() << QmlDebug::Compiling << QmlDebug::Creating
                                                  << QmlDebug::Binding << QmlDebug::HandlingSignal;

        if (!acceptedTypes.contains(data.eventType))
            continue;

        const QString eventHash = eventHashes.at(event->eventId);
        const QmlRangeEventStartInstance *potentialParent = callStack.isEmpty()
                ? 0 : &startTimeData[callStack.top().second];

        while (potentialParent
               && !(potentialParent->startTime + potentialParent->duration > event->startTime)) {
            callStack.pop();
            potentialParent = callStack.isEmpty() ? 0
                                                  : &startTimeData[callStack.top().second];
        }

        // check whether event is already in stack
        for (int ii = 0; ii < callStack.size(); ++ii) {
            if (callStack.at(ii).first == eventHash) {
                event->bindingLoopHead = callStack.at(ii).second;
                break;
            }
        }


        CallStackEntry newEntry(eventHash, i);
        callStack.push(newEntry);
    }

}

/////////////////// QML interface

bool QmlProfilerTimelineModelProxy::empty() const
{
    return count() == 0;
}

int QmlProfilerTimelineModelProxy::count() const
{
    return d->startTimeData.count();
}

qint64 QmlProfilerTimelineModelProxy::lastTimeMark() const
{
    return d->startTimeData.last().startTime + d->startTimeData.last().duration;
}

qint64 QmlProfilerTimelineModelProxy::traceStartTime() const
{
    return d->modelManager->traceTime()->startTime();
}

qint64 QmlProfilerTimelineModelProxy::traceEndTime() const
{
    return d->modelManager->traceTime()->endTime();
}

qint64 QmlProfilerTimelineModelProxy::traceDuration() const
{
    return d->modelManager->traceTime()->duration();
}

int QmlProfilerTimelineModelProxy::getState() const
{
    // TODO: connect statechanged
    return (int)d->modelManager->state();
}

void QmlProfilerTimelineModelProxy::setExpanded(int category, bool expanded)
{
    d->categorySpan[category].expanded = expanded;
    emit expandedChanged();
}

int QmlProfilerTimelineModelProxy::categoryDepth(int categoryIndex) const
{
    if (d->categorySpan.count() <= categoryIndex)
        return 1;
    if (d->categorySpan[categoryIndex].expanded)
        return d->categorySpan[categoryIndex].expandedRows;
    else
        return d->categorySpan[categoryIndex].contractedRows;
}

int QmlProfilerTimelineModelProxy::categoryCount() const
{
    return 5;
}

const QString QmlProfilerTimelineModelProxy::categoryLabel(int categoryIndex) const
{
    switch (categoryIndex) {
    case 0: return tr("Painting"); break;
    case 1: return tr("Compiling"); break;
    case 2: return tr("Creating"); break;
    case 3: return tr("Binding"); break;
    case 4: return tr("Handling Signal"); break;
    default: return QString();
    }
}


int QmlProfilerTimelineModelProxy::findFirstIndex(qint64 startTime) const
{
    int candidate = -1;
    // in the "endtime" list, find the first event that ends after startTime
    if (d->endTimeData.isEmpty())
        return 0; // -1
    if (d->endTimeData.count() == 1 || d->endTimeData.first().endTime >= startTime)
        candidate = 0;
    else
        if (d->endTimeData.last().endTime <= startTime)
            return 0; // -1

    if (candidate == -1)
    {
        int fromIndex = 0;
        int toIndex = d->endTimeData.count()-1;
        while (toIndex - fromIndex > 1) {
            int midIndex = (fromIndex + toIndex)/2;
            if (d->endTimeData[midIndex].endTime < startTime)
                fromIndex = midIndex;
            else
                toIndex = midIndex;
        }

        candidate = toIndex;
    }

    int eventIndex = d->endTimeData[candidate].startTimeIndex;
    return d->startTimeData[eventIndex].baseEventIndex;

}

int QmlProfilerTimelineModelProxy::findFirstIndexNoParents(qint64 startTime) const
{
    int candidate = -1;
    // in the "endtime" list, find the first event that ends after startTime
    if (d->endTimeData.isEmpty())
        return 0; // -1
    if (d->endTimeData.count() == 1 || d->endTimeData.first().endTime >= startTime)
        candidate = 0;
    else
        if (d->endTimeData.last().endTime <= startTime)
            return 0; // -1

    if (candidate == -1) {
        int fromIndex = 0;
        int toIndex = d->endTimeData.count()-1;
        while (toIndex - fromIndex > 1) {
            int midIndex = (fromIndex + toIndex)/2;
            if (d->endTimeData[midIndex].endTime < startTime)
                fromIndex = midIndex;
            else
                toIndex = midIndex;
        }

        candidate = toIndex;
    }

    int ndx = d->endTimeData[candidate].startTimeIndex;

    return ndx;
}

int QmlProfilerTimelineModelProxy::findLastIndex(qint64 endTime) const
{
        // in the "starttime" list, find the last event that starts before endtime
        if (d->startTimeData.isEmpty())
            return 0; // -1
        if (d->startTimeData.first().startTime >= endTime)
            return 0; // -1
        if (d->startTimeData.count() == 1)
            return 0;
        if (d->startTimeData.last().startTime <= endTime)
            return d->startTimeData.count()-1;

        int fromIndex = 0;
        int toIndex = d->startTimeData.count()-1;
        while (toIndex - fromIndex > 1) {
            int midIndex = (fromIndex + toIndex)/2;
            if (d->startTimeData[midIndex].startTime < endTime)
                fromIndex = midIndex;
            else
                toIndex = midIndex;
        }

        return fromIndex;
}

int QmlProfilerTimelineModelProxy::getEventType(int index) const
{

    return getRangeEventData(index).eventType;
}

const QmlProfilerTimelineModelProxy::QmlRangeEventData &QmlProfilerTimelineModelProxy::getRangeEventData(int index) const
{
    // TODO: remove -> use accessors
    return d->eventDict[d->startTimeData[index].eventId];
}

int QmlProfilerTimelineModelProxy::getEventRow(int index) const
{
    if (d->categorySpan[getEventType(index)].expanded)
        return d->startTimeData[index].displayRowExpanded;
    else
        return d->startTimeData[index].displayRowCollapsed;
}

qint64 QmlProfilerTimelineModelProxy::getDuration(int index) const
{
    return d->startTimeData[index].duration;
}

qint64 QmlProfilerTimelineModelProxy::getStartTime(int index) const
{
    return d->startTimeData[index].startTime;
}

qint64 QmlProfilerTimelineModelProxy::getEndTime(int index) const
{
    return d->startTimeData[index].startTime + d->startTimeData[index].duration;
}

int QmlProfilerTimelineModelProxy::getEventId(int index) const
{
    return d->startTimeData[index].eventId;
}

int QmlProfilerTimelineModelProxy::getBindingLoopDest(int index) const
{
    return d->startTimeData[index].bindingLoopHead;
}

const QVariantList QmlProfilerTimelineModelProxy::getLabelsForCategory(int category) const
{
    QVariantList result;

    if (d->categorySpan.count() > category && d->categorySpan[category].expanded) {
        int eventCount = d->eventDict.count();
        for (int i = 0; i < eventCount; i++) {
            if (d->eventDict[i].eventType == category) {
                QVariantMap element;
                element.insert(QLatin1String("displayName"), QVariant(d->eventDict[i].displayName));
                element.insert(QLatin1String("description"), QVariant(d->eventDict[i].details));
                element.insert(QLatin1String("id"), QVariant(d->eventDict[i].eventId));
                result << element;
            }
        }
    }

    return result;
}

QString QmlProfilerTimelineModelProxy::QmlProfilerTimelineModelProxyPrivate::displayTime(double time)
{
    if (time < 1e6)
        return QString::number(time/1e3,'f',3) + trUtf8(" \xc2\xb5s");
    if (time < 1e9)
        return QString::number(time/1e6,'f',3) + tr(" ms");

    return QString::number(time/1e9,'f',3) + tr(" s");
}

const QVariantList QmlProfilerTimelineModelProxy::getEventDetails(int index) const
{
    QVariantList result;
    int eventId = getEventId(index);

    {
        QVariantMap valuePair;
        valuePair.insert(tr("title"), QVariant(categoryLabel(d->eventDict[eventId].eventType)));
        result << valuePair;
    }

    // duration
    {
        QVariantMap valuePair;
        valuePair.insert(tr("Duration:"), QVariant(d->displayTime(d->startTimeData[index].duration)));
        result << valuePair;
    }

    // details
    {
        QVariantMap valuePair;
        QString detailsString = d->eventDict[eventId].details;
        if (detailsString.length() > 40)
            detailsString = detailsString.left(40) + QLatin1String("...");
        valuePair.insert(tr("Details:"), QVariant(detailsString));
        result << valuePair;
    }

    // location
    {
        QVariantMap valuePair;
        valuePair.insert(tr("Location:"), QVariant(d->eventDict[eventId].displayName));
        result << valuePair;
    }

    // isbindingloop
    {}


    return result;
}

}
}
