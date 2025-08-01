// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <QColor>
#include <QSignalSpy>
#include <QTest>
#include <tracing/timelinezoomcontrol.h>

using namespace Timeline;

class tst_TimelineZoomControl : public QObject
{
    Q_OBJECT
private:
    void verifyWindow(const TimelineZoomControl &zoomControl);

private slots:
    void trace();
    void window();
    void range();
    void selection();
};

void tst_TimelineZoomControl::verifyWindow(const TimelineZoomControl &zoomControl)
{
    QVERIFY(zoomControl.windowStart() <= zoomControl.rangeStart());
    QVERIFY(zoomControl.windowEnd() >= zoomControl.rangeEnd());
    QVERIFY(zoomControl.traceStart() <= zoomControl.windowStart());
    QVERIFY(zoomControl.traceEnd() >= zoomControl.windowEnd());
}

void tst_TimelineZoomControl::trace()
{
    TimelineZoomControl zoomControl;
    QSignalSpy spy(&zoomControl, &TimelineZoomControl::traceChanged);
    QCOMPARE(zoomControl.traceStart(), -1);
    QCOMPARE(zoomControl.traceEnd(), -1);
    QCOMPARE(zoomControl.traceDuration(), 0);

    zoomControl.setTrace(100, 200);
    QCOMPARE(zoomControl.traceStart(), 100);
    QCOMPARE(zoomControl.traceEnd(), 200);
    QCOMPARE(zoomControl.traceDuration(), 100);
    QCOMPARE(spy.count(), 1);

    zoomControl.clear();
    QCOMPARE(zoomControl.traceStart(), -1);
    QCOMPARE(zoomControl.traceEnd(), -1);
    QCOMPARE(zoomControl.traceDuration(), 0);
    QCOMPARE(spy.count(), 2);
}

void tst_TimelineZoomControl::window()
{
    TimelineZoomControl zoomControl;

    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, this, [&zoomControl] {
        QVERIFY(zoomControl.windowLocked());
        zoomControl.setWindowLocked(false);
    });

    int numWindowChanges = 0;

    connect(&zoomControl, &TimelineZoomControl::windowChanged, this, [&](qint64, qint64) {
        verifyWindow(zoomControl);

        QVERIFY(!timer.isActive());
        if (++numWindowChanges == 9) { // delay a bit during a move
            zoomControl.setWindowLocked(true);
            timer.start(300);
        }
    });

    QCOMPARE(zoomControl.windowStart(), -1);
    QCOMPARE(zoomControl.windowEnd(), -1);
    QCOMPARE(zoomControl.windowDuration(), 0);

    zoomControl.setTrace(100, 200);
    QCOMPARE(zoomControl.windowStart(), 100);
    QCOMPARE(zoomControl.windowEnd(), 200);
    QCOMPARE(zoomControl.windowDuration(), 100);

    zoomControl.clear();
    QCOMPARE(zoomControl.windowStart(), -1);
    QCOMPARE(zoomControl.windowEnd(), -1);
    QCOMPARE(zoomControl.windowDuration(), 0);

    zoomControl.setTrace(100000, 200000);
    QCOMPARE(zoomControl.windowStart(), 100000);
    QVERIFY(zoomControl.windowEnd() < 110000);

    zoomControl.setRange(199995, 200000); // jump to end
    verifyWindow(zoomControl);
    zoomControl.setRange(100000, 100005); // jump to start
    verifyWindow(zoomControl);

    zoomControl.setRange(150000, 150005);
    zoomControl.setRange(152000, 152005); // move right

    QMetaObject::Connection connection = connect(
                &zoomControl, &TimelineZoomControl::windowMovingChanged, this, [&](bool moving) {
        if (moving)
            return;

        verifyWindow(zoomControl);
        if (zoomControl.windowEnd() == zoomControl.traceEnd()) {
            zoomControl.setRange(104005, 104010); // jump left
            verifyWindow(zoomControl);
            zoomControl.setRange(102005, 102010); // make sure it doesn't overrun trace start
        } else if (zoomControl.windowStart() == zoomControl.traceStart()) {
            QCoreApplication::exit();
        } else {
            QVERIFY(zoomControl.rangeStart() - zoomControl.windowStart() ==
                    zoomControl.windowEnd() - zoomControl.rangeEnd());
            if (zoomControl.rangeStart() == 152000) {
                zoomControl.setRange(150000, 150005); // move left
            } else if (zoomControl.rangeStart() == 150000) {
                zoomControl.setRange(196990, 196995); // jump right
                verifyWindow(zoomControl);
                zoomControl.setRange(198990, 198995); // make sure it doesn't overrun trace end
            }
        }
        verifyWindow(zoomControl);
    });

    QGuiApplication::exec();

    disconnect(connection);

    bool stopDetected = false;
    connect(&zoomControl, &TimelineZoomControl::windowMovingChanged, [&](bool moving) {
        if (!moving) {
            QCOMPARE(stopDetected, false);
            stopDetected = true;
        } else {
            zoomControl.clear();
            QCOMPARE(zoomControl.windowStart(), -1);
            QCOMPARE(zoomControl.windowEnd(), -1);
            QCOMPARE(zoomControl.rangeStart(), -1);
            QCOMPARE(zoomControl.rangeEnd(), -1);
            zoomControl.clear(); // no second windowMovingChanged(false), please
        }
    });

    zoomControl.setRange(180010, 180015);
}

void tst_TimelineZoomControl::range()
{
    TimelineZoomControl zoomControl;
    QSignalSpy spy(&zoomControl, &TimelineZoomControl::rangeChanged);
    QCOMPARE(zoomControl.rangeStart(), -1);
    QCOMPARE(zoomControl.rangeEnd(), -1);
    QCOMPARE(zoomControl.rangeDuration(), 0);

    zoomControl.setTrace(10, 500);
    QCOMPARE(zoomControl.rangeStart(), 10);
    QCOMPARE(zoomControl.rangeEnd(), 10);
    QCOMPARE(zoomControl.rangeDuration(), 0);
    QCOMPARE(spy.count(), 1);

    zoomControl.setRange(100, 200);
    QCOMPARE(zoomControl.rangeStart(), 100);
    QCOMPARE(zoomControl.rangeEnd(), 200);
    QCOMPARE(zoomControl.rangeDuration(), 100);
    QCOMPARE(spy.count(), 2);

    zoomControl.clear();
    QCOMPARE(zoomControl.rangeStart(), -1);
    QCOMPARE(zoomControl.rangeEnd(), -1);
    QCOMPARE(zoomControl.rangeDuration(), 0);
    QCOMPARE(spy.count(), 3);
}

void tst_TimelineZoomControl::selection()
{
    TimelineZoomControl zoomControl;
    QSignalSpy spy(&zoomControl, &TimelineZoomControl::selectionChanged);
    QCOMPARE(zoomControl.selectionStart(), -1);
    QCOMPARE(zoomControl.selectionEnd(), -1);
    QCOMPARE(zoomControl.selectionDuration(), 0);

    zoomControl.setTrace(10, 500);
    zoomControl.setSelection(100, 200);
    QCOMPARE(zoomControl.selectionStart(), 100);
    QCOMPARE(zoomControl.selectionEnd(), 200);
    QCOMPARE(zoomControl.selectionDuration(), 100);
    QCOMPARE(spy.count(), 1);

    zoomControl.clear();
    QCOMPARE(zoomControl.selectionStart(), -1);
    QCOMPARE(zoomControl.selectionEnd(), -1);
    QCOMPARE(zoomControl.selectionDuration(), 0);
    QCOMPARE(spy.count(), 2);
}

QTEST_GUILESS_MAIN(tst_TimelineZoomControl)

#include "tst_timelinezoomcontrol.moc"
