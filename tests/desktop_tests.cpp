#include "pet_window.hpp"
#include "detection_preview.hpp"
#include "pointer_smoother.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace neko::desktop;

namespace {

QPointF normalizedHeadTarget(const PetWindow& pet)
{
    auto* screen = QGuiApplication::screenAt(pet.frameGeometry().center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect area = screen->availableGeometry();
    const QPointF point = QPointF(pet.pos()) +
        robotHeadGrabArea(QRectF(10, 24, 240, 256)).center();
    return QPointF((point.x() - area.left()) / std::max(1, area.width() - 1),
                   (point.y() - area.top()) / std::max(1, area.height() - 1));
}

} // namespace

class DesktopTests : public QObject {
    Q_OBJECT

private slots:
    void desktopPointerSmoothingAttenuatesSmallAlternatingJitter()
    {
        PointerSmoothingState state;
        QCOMPARE(smoothDesktopPointer(state, QPointF(0.5, 0.5), 0.0), QPointF(0.5, 0.5));
        double rawSquaredError = 0.0;
        double filteredSquaredError = 0.0;
        for (int sample = 1; sample <= 60; ++sample) {
            const double offset = sample % 2 == 0 ? -0.01 : 0.01;
            const QPointF raw(0.5 + offset, 0.5 - offset);
            const QPointF filtered = smoothDesktopPointer(state, raw, sample * 0.02);
            rawSquaredError += offset * offset;
            filteredSquaredError += std::pow(filtered.x() - 0.5, 2);
            QVERIFY(std::isfinite(filtered.x()));
            QVERIFY(std::isfinite(filtered.y()));
        }
        QVERIFY(filteredSquaredError < rawSquaredError * 0.5);
    }

    void desktopPointerSmoothingStillFollowsRealMotion()
    {
        PointerSmoothingState state;
        smoothDesktopPointer(state, QPointF(0.0, 0.0), 0.0);
        QPointF filtered;
        for (int sample = 1; sample <= 10; ++sample)
            filtered = smoothDesktopPointer(state, QPointF(0.8, 0.4), sample * 0.02);
        QVERIFY(std::abs(filtered.x() - 0.8) < 0.04);
        QVERIFY(std::abs(filtered.y() - 0.4) < 0.02);
        for (int sample = 11; sample <= 30; ++sample)
            filtered = smoothDesktopPointer(state, QPointF(0.8, 0.4), sample * 0.02);
        QVERIFY(std::abs(filtered.x() - 0.8) < 0.0001);
        QVERIFY(std::abs(filtered.y() - 0.4) < 0.0001);
    }

    void desktopPointerSmoothingResetsForLongGapsOrInvalidInput()
    {
        PointerSmoothingState state;
        smoothDesktopPointer(state, QPointF(0.2, 0.3), 1.0);
        // A long gap starts from the real new point, without extrapolating old motion.
        QCOMPARE(smoothDesktopPointer(state, QPointF(0.8, 0.9), 1.6), QPointF(0.8, 0.9));
        QCOMPARE(smoothDesktopPointer(state, QPointF(0.1, 0.2), 0.5), QPointF(0.1, 0.2));
        smoothDesktopPointer(state,
            QPointF(std::numeric_limits<double>::quiet_NaN(), 0.5), 0.6);
        QCOMPARE(smoothDesktopPointer(state, QPointF(0.6, 0.7), 0.7), QPointF(0.6, 0.7));
        state = PointerSmoothingState{};
        QCOMPARE(smoothDesktopPointer(state, QPointF(2.0, -1.0), 2.0), QPointF(1.0, 0.0));
    }

    void pinchDragUsesRelativeMotionAndFreezesWithoutMeasurement()
    {
        PetWindow pet;
        pet.show();
        const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
        if (area.width() < pet.width() + 150 || area.height() < pet.height() + 100)
            QSKIP("Screen too small for a movement assertion");
        pet.move(area.topLeft() + QPoint(30, 30));
        const QPoint start = pet.pos();
        const QPointF head = normalizedHeadTarget(pet);
        QSignalSpy modes(&pet, &PetWindow::pinchModeChanged);
        pet.setGazeTarget(true, QPointF(1.0, 1.0));
        pet.setPinchTarget(true, head);
        QVERIFY(pet.pinchActive());
        QVERIFY(!pet.gazeActive());
        QCOMPARE(pet.pos(), start); // Grabbing must not teleport to the pointer.
        auto* label = pet.findChild<QLabel*>("pinchStatusLabel");
        QVERIFY(label && label->isVisible());
        QVERIFY(label->text().contains(QStringLiteral("提")));
        pet.reactTo(VisualEvent::HandWave);
        pet.setGazeTarget(true, QPointF(0.0, 0.0));
        QCOMPARE(pet.pose(), PetPose::Idle);
        QVERIFY(!pet.gazeActive());

        pet.setPinchTarget(true, head + QPointF(0.2, 0.2));
        QTest::qWait(120);
        // Drag deltas use the full screen extent, not the smaller window-position range.
        QVERIFY(pet.x() > start.x() + 0.70 * 0.2 * (area.width() - 1));
        QVERIFY(pet.y() > start.y() + 0.70 * 0.2 * (area.height() - 1));
        pet.setPinchTarget(true, QPointF(1.0, 1.0), false);
        const QPoint frozen = pet.pos();
        QTest::qWait(80);
        QCOMPARE(pet.pos(), frozen);
        QVERIFY(pet.pinchActive());
        pet.setPinchTarget(true, QPointF(10.0, 10.0));
        QTest::qWait(120);
        QVERIFY(area.contains(pet.frameGeometry()));
        pet.setPinchTarget(false, QPointF(), false);
        const QPoint released = pet.pos();
        QTest::qWait(70);
        QCOMPARE(pet.pos(), released);
        QVERIFY(!pet.pinchActive());
        QVERIFY(!label->isVisible());
        QCOMPARE(modes.count(), 2);
    }

    void pinchExpiresAndRequiresReleaseAfterManualInterference()
    {
        PetWindow pet;
        pet.show();
        const QPointF head = normalizedHeadTarget(pet);
        pet.setPinchTarget(true, QPointF(0.5, 0.5), false);
        QVERIFY(!pet.pinchActive());
        pet.setPinchTarget(true, head);
        QVERIFY(!pet.pinchActive());
        pet.setPinchTarget(false, QPointF());
        pet.setPinchTarget(true, QPointF(std::numeric_limits<double>::quiet_NaN(), 0.5));
        QVERIFY(!pet.pinchActive());
        pet.setPinchTarget(true, head);
        QVERIFY(!pet.pinchActive());
        pet.setPinchTarget(false, QPointF());
        pet.setPinchTarget(true, head);
        QVERIFY(pet.pinchActive());
        QTest::qWait(300);
        pet.setPinchTarget(true, QPointF(0.7, 0.5), false);
        QTest::qWait(260);
        QVERIFY(!pet.pinchActive()); // Missing updates do not renew the 500ms deadline.
        pet.setPinchTarget(true, head);
        QVERIFY(!pet.pinchActive());
        pet.setPinchTarget(false, QPointF());
        pet.setPinchTarget(true, head);
        QVERIFY(pet.pinchActive());
        QTest::mousePress(&pet, Qt::LeftButton, Qt::NoModifier, QPoint(120, 140));
        QVERIFY(!pet.pinchActive());
        QTest::mouseRelease(&pet, Qt::LeftButton, Qt::NoModifier, QPoint(120, 140));
        pet.setPinchTarget(true, head);
        QVERIFY(!pet.pinchActive());
        pet.setPinchTarget(false, QPointF());
        pet.setPinchTarget(true, normalizedHeadTarget(pet));
        pet.hide();
        QVERIFY(!pet.pinchActive());
        pet.restore();
        pet.setPinchTarget(true, normalizedHeadTarget(pet));
        QVERIFY(!pet.pinchActive());
        pet.setPinchTarget(false, QPointF());
        pet.setPinchTarget(true, normalizedHeadTarget(pet));
        QVERIFY(pet.pinchActive());
        pet.setCameraControls(false, true);
        QVERIFY(!pet.pinchActive());
    }

    void pinchMustBeginOnTheHeadAndCannotCatchWhileAlreadyClosed()
    {
        PetWindow pet;
        pet.show();
        const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
        pet.move(area.topLeft() + QPoint(30, 30));
        const QPointF head = normalizedHeadTarget(pet);
        QSignalSpy modes(&pet, &PetWindow::pinchModeChanged);

        // Merely aiming at the head must not start a grab.
        pet.setPinchPointer(true, head);
        QVERIFY(!pet.pinchActive());
        pet.setPinchTarget(true, QPointF(1.0, 1.0));
        QVERIFY(!pet.pinchActive());
        // The same closed pinch cannot slide onto the head and acquire it.
        pet.setPinchTarget(true, head);
        QVERIFY(!pet.pinchActive());
        QCOMPARE(modes.count(), 0);
        pet.setPinchTarget(false, head);
        pet.setPinchTarget(true, head);
        QVERIFY(pet.pinchActive());
        QCOMPARE(modes.count(), 1);
        pet.setPinchTarget(false, head);
        QVERIFY(!pet.pinchActive());
        QCOMPARE(modes.count(), 2);
    }

    void optionalHeadGrabVisualSnapshots()
    {
        const QString directory = qEnvironmentVariable("NEKO_VISUAL_QA_DIR");
        if (directory.isEmpty()) QSKIP("Set NEKO_VISUAL_QA_DIR to export visual QA frames");
        QVERIFY(QDir().mkpath(directory));
        PetWindow pet;
        pet.show();
        const auto save = [&pet, &directory](const QString& name) {
            QImage image(pet.size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            pet.render(&image);
            return image.save(QDir(directory).filePath(name));
        };
        QVERIFY(save(QStringLiteral("idle.png")));
        const QPointF head = normalizedHeadTarget(pet);
        pet.setPinchPointer(true, head);
        pet.setPinchTarget(true, head);
        QVERIFY(pet.pinchActive());
        QTest::qWait(220);
        pet.setPinchTarget(true, head);
        QTest::qWait(160);
        QVERIFY(save(QStringLiteral("held.png")));
        pet.setPinchTarget(false, head);
        QTest::qWait(450);
        QVERIFY(save(QStringLiteral("released.png")));
    }

    void pinchPointerIsClickThroughAndClearsAfterStaleInputOrHide()
    {
        PetWindow pet;
        pet.show();
        auto* pointer = pet.findChild<QWidget*>("pinchPointerWindow");
        QVERIFY(pointer);
        QVERIFY(pointer->windowFlags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(pointer->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QVERIFY(pointer->testAttribute(Qt::WA_ShowWithoutActivating));
        pet.setPinchPointer(true, normalizedHeadTarget(pet));
        QVERIFY(pointer->isVisible());
        QVERIFY(!pet.pinchActive());
        QTest::qWait(300);
        QVERIFY(!pointer->isVisible());
        pet.setPinchPointer(true, normalizedHeadTarget(pet));
        pet.setCameraControls(false, true);
        QVERIFY(!pointer->isVisible());
        pet.setPinchPointer(true, normalizedHeadTarget(pet));
        pet.hide();
        QVERIFY(!pointer->isVisible());
    }

    void previewMenuIsIndependentAndSynchronizesWithoutSignals()
    {
        PetWindow pet;
        auto* previewAction = pet.findChild<QAction*>("previewAction");
        auto* cameraAction = pet.findChild<QAction*>("cameraAction");
        QVERIFY(previewAction);
        QVERIFY(cameraAction);
        QCOMPARE(previewAction->text(), QStringLiteral("显示检测画面"));
        QVERIFY(previewAction->isCheckable());
        QVERIFY(!previewAction->isChecked());
        QSignalSpy previewRequests(&pet, &PetWindow::previewRequested);
        QSignalSpy cameraRequests(&pet, &PetWindow::cameraRequested);

        previewAction->trigger();
        QCOMPARE(previewRequests.count(), 1);
        QCOMPARE(previewRequests.at(0).at(0).toBool(), true);
        QCOMPARE(cameraRequests.count(), 0);
        QVERIFY(!cameraAction->isChecked());

        pet.setPreviewChecked(false);
        QVERIFY(!previewAction->isChecked());
        QCOMPARE(previewRequests.count(), 1);
        pet.setCameraControls(true, false);
        QVERIFY(previewAction->isEnabled());
        QVERIFY(!previewAction->isChecked());
        pet.setPreviewChecked(true);
        QVERIFY(previewAction->isChecked());
        QCOMPARE(previewRequests.count(), 1);
        pet.setCameraControls(false, true);
        QVERIFY(previewAction->isChecked());
        QCOMPARE(cameraRequests.count(), 0);
    }

    void previewOwnsTheImageAndFitsItWithoutStretching_data()
    {
        QTest::addColumn<QSize>("windowSize");
        QTest::addColumn<QSize>("imageSize");
        QTest::newRow("wide-image") << QSize(640, 480) << QSize(160, 80);
        QTest::newRow("tall-image") << QSize(800, 400) << QSize(80, 160);
    }

    void previewOwnsTheImageAndFitsItWithoutStretching()
    {
        QFETCH(QSize, windowSize);
        QFETCH(QSize, imageSize);
        DetectionPreviewWindow preview;
        preview.resize(windowSize);
        QImage source(imageSize, QImage::Format_RGB32);
        source.fill(Qt::red);
        auto* sourcePixels = reinterpret_cast<QRgb*>(source.bits());
        preview.setFrame(source);
        // Bypass implicit-sharing detachment, just as a reused camera buffer does.
        std::fill_n(sourcePixels, source.width() * source.height(), qRgb(0, 255, 0));
        QImage rendered(windowSize, QImage::Format_RGB32);
        preview.render(&rendered);

        QRect imageBounds;
        int redPixels = 0;
        for (int y = 0; y < rendered.height(); ++y) {
            for (int x = 0; x < rendered.width(); ++x) {
                if (rendered.pixelColor(x, y) != QColor(Qt::red)) continue;
                imageBounds |= QRect(x, y, 1, 1);
                ++redPixels;
            }
        }
        QVERIFY(redPixels > 1000);
        QCOMPARE(redPixels, imageBounds.width() * imageBounds.height());
        const double renderedAspect = static_cast<double>(imageBounds.width()) /
                                      imageBounds.height();
        const double sourceAspect = static_cast<double>(imageSize.width()) /
                                    imageSize.height();
        QVERIFY(std::abs(renderedAspect - sourceAspect) < 0.01);
        QVERIFY(std::abs(imageBounds.center().x() - rendered.rect().center().x()) <= 1);
        QVERIFY(imageBounds.top() > 35);
        QVERIFY(imageBounds.bottom() < rendered.height() - 8);
        QCOMPARE(rendered.pixelColor(0, rendered.height() - 1), QColor("#161c26"));

        preview.setCameraStatus(QStringLiteral("已关闭"));
        preview.setFrame(QImage());
        QImage waiting(windowSize, QImage::Format_RGB32);
        preview.render(&waiting);
        QVERIFY(preview.windowTitle().contains(QStringLiteral("已关闭")));
        QVERIFY(waiting != rendered);
        int remainingRedPixels = 0;
        for (int y = 0; y < waiting.height(); ++y)
            for (int x = 0; x < waiting.width(); ++x)
                remainingRedPixels += waiting.pixelColor(x, y) == QColor(Qt::red);
        QCOMPARE(remainingRedPixels, 0);
    }

    void closingPreviewOnlyHidesItAndUnchecksTheMenu()
    {
        PetWindow pet;
        DetectionPreviewWindow preview;
        pet.setCameraControls(true, true);
        pet.setPreviewChecked(true);
        auto* previewAction = pet.findChild<QAction*>("previewAction");
        auto* cameraAction = pet.findChild<QAction*>("cameraAction");
        QVERIFY(previewAction);
        QVERIFY(cameraAction);
        QSignalSpy closed(&preview, &DetectionPreviewWindow::closed);
        QSignalSpy lastWindowClosed(qApp, &QGuiApplication::lastWindowClosed);
        QSignalSpy cameraRequests(&pet, &PetWindow::cameraRequested);
        connect(&preview, &DetectionPreviewWindow::closed, &pet,
                [&pet] { pet.setPreviewChecked(false); });
        QVERIFY(!preview.testAttribute(Qt::WA_QuitOnClose));
        preview.show();
        QVERIFY(preview.isVisible());
        QVERIFY(preview.close());
        QCoreApplication::processEvents();
        QCOMPARE(closed.count(), 1);
        QVERIFY(!preview.isVisible());
        QVERIFY(!previewAction->isChecked());
        QVERIFY(cameraAction->isChecked());
        QCOMPARE(cameraRequests.count(), 0);
        QCOMPARE(lastWindowClosed.count(), 0);
        preview.show();
        QVERIFY(preview.isVisible());
    }

    void fingerChaseMovesGraduallyAndStaysOnTheScreen()
    {
        PetWindow pet;
        pet.show();
        const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
        if (area.width() < pet.width() + 120 || area.height() < pet.height() + 120)
            QSKIP("The test needs room to move the robot");
        const QPoint target(area.right() - pet.width() + 1,
                            area.bottom() - pet.height() + 1);
        const QPoint start = target - QPoint(100, 100);
        pet.move(start);
        QTimer measurements;
        measurements.setInterval(40);
        bool stayedInside = true;
        connect(&measurements, &QTimer::timeout, &pet, [&] {
            pet.setGazeTarget(true, QPointF(1, 1));
            stayedInside &= area.contains(pet.frameGeometry());
        });
        pet.setGazeTarget(true, QPointF(1, 1));
        QCOMPARE(pet.pos(), start); // Receiving a target must not teleport the window.
        measurements.start();
        QTRY_VERIFY_WITH_TIMEOUT(pet.pos() != start, 500);
        QTRY_VERIFY_WITH_TIMEOUT(std::hypot(pet.x() - target.x(),
                                          pet.y() - target.y()) <= 12.0, 1800);
        QVERIFY(stayedInside);
        QVERIFY(area.contains(pet.frameGeometry()));
        measurements.stop();
        pet.setGazeTarget(false, QPointF(0.5, 0.5));
        const QPoint stopped = pet.pos();
        QTest::qWait(100);
        QCOMPARE(pet.pos(), stopped);
    }

    void staleFingerCoordinatesStopMovementBeforeFollowingExpires()
    {
        PetWindow pet;
        pet.show();
        const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
        if (area.width() < pet.width() + 200 || area.height() < pet.height() + 200)
            QSKIP("The test needs room to move the robot");
        pet.move(area.topLeft() + QPoint(20, 20));
        const QPoint start = pet.pos();
        pet.setGazeTarget(true, QPointF(1, 1));
        QTest::qWait(260);
        QVERIFY(pet.pos() != start);
        QVERIFY(pet.gazeActive());
        const QPoint stopped = pet.pos();
        QTest::qWait(100);
        QCOMPARE(pet.pos(), stopped); // No new observation: do not keep chasing stale data.
        QTest::qWait(290); // Following stays active beyond the old 500 ms cap.
        QVERIFY(pet.gazeActive());
        QCOMPARE(pet.pos(), stopped); // The 180 ms motion guard still applies.
        QTRY_VERIFY_WITH_TIMEOUT(!pet.gazeActive(), 400);
        QCOMPARE(pet.pos(), stopped);
    }

    void predictedTargetsCannotStartOrReviveFollowing()
    {
        PetWindow pet;
        pet.show();
        pet.setGazeTarget(true, QPointF(0.9, 0.1), true);
        QVERIFY(!pet.gazeActive());

        pet.setGazeTarget(true, QPointF(0.9, 0.1));
        QVERIFY(pet.gazeActive());
        pet.setGazeTarget(false, QPointF(0.5, 0.5));
        pet.setGazeTarget(true, QPointF(0.1, 0.9), true);
        QVERIFY(!pet.gazeActive());

        // The setter must enforce expiry even when the animation timer cannot tick.
        pet.setGazeTarget(true, QPointF(0.9, 0.1));
        auto* timer = pet.findChild<QTimer*>("animationTimer");
        QVERIFY(timer);
        timer->stop();
        QTest::qWait(650);
        pet.setGazeTarget(true, QPointF(0.1, 0.9), true);
        QVERIFY(pet.gazeActive());
        QTest::qWait(370); // A prediction at 650 ms must not renew the real deadline.
        pet.setGazeTarget(true, QPointF(0.1, 0.9), true);
        QVERIFY(!pet.gazeActive());
        pet.setGazeTarget(true, QPointF(0.1, 0.9), true);
        QVERIFY(!pet.gazeActive());

        // Existing two-argument callers still supply a real measurement.
        pet.setGazeTarget(true, QPointF(0.1, 0.9));
        QVERIFY(pet.gazeActive());
    }

    void predictedTargetsCannotExtendTheRealMeasurementDeadline()
    {
        PetWindow pet;
        pet.show();
        pet.setGazeTarget(true, QPointF(0.9, 0.1));
        pet.reactTo(VisualEvent::HandLost);
        QTimer predictions;
        predictions.setInterval(40);
        connect(&predictions, &QTimer::timeout, &pet,
                [&] { pet.setGazeTarget(true, QPointF(0.1, 0.9), true); });
        predictions.start();
        QTest::qWait(650);
        QVERIFY(pet.gazeActive());
        QCOMPARE(pet.pose(), PetPose::Idle); // HandLost remains pending during prediction.
        QTRY_VERIFY_WITH_TIMEOUT(!pet.gazeActive(), 400);
        QTest::qWait(100);
        QVERIFY(!pet.gazeActive());
        QCOMPARE(pet.pose(), PetPose::Surprise);
    }

    void predictionsRefreshTheTrackingTargetForEyesAndChase()
    {
        PetWindow pet;
        pet.show();
        const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
        if (area.width() < pet.width() + 200 || area.height() < pet.height() + 200)
            QSKIP("The test needs room to move the robot");
        pet.move(area.topLeft() + QPoint(20, 20));
        pet.setGazeTarget(true, QPointF(0, 0));
        QTest::qWait(230); // Real coordinates are now too old for the 180 ms motion guard.
        const QPoint stopped = pet.pos();

        QTimer predictions;
        predictions.setInterval(40);
        connect(&predictions, &QTimer::timeout, &pet,
                [&] { pet.setGazeTarget(true, QPointF(1, 1), true); });
        pet.setGazeTarget(true, QPointF(1, 1), true);
        predictions.start();
        QTest::qWait(120);
        QVERIFY(pet.gazeActive());
        QVERIFY(pet.gazePosition().x() > 0.6);
        QVERIFY(pet.gazePosition().y() > 0.6);
        QVERIFY(pet.pos() != stopped);
        const QPoint resumed = pet.pos();
        QTest::qWait(300); // Fresh predictions still drive motion at about 650 ms.
        QVERIFY(pet.gazeActive());
        QVERIFY(pet.pos() != resumed);
    }

    void twoArgumentTargetsRefreshTheRealMeasurementDeadline()
    {
        PetWindow pet;
        pet.show();
        pet.setGazeTarget(true, QPointF(0.1, 0.2));
        QTest::qWait(650);
        QVERIFY(pet.gazeActive());
        pet.setGazeTarget(true, QPointF(0.9, 0.8));
        QTest::qWait(650); // Beyond both the original deadline and the old 500 ms cap.
        QVERIFY(pet.gazeActive());
        QVERIFY(pet.gazePosition().x() > 0.8);
        QVERIFY(pet.gazePosition().y() > 0.7);
        QTRY_VERIFY_WITH_TIMEOUT(!pet.gazeActive(), 400);
    }

    void fingerChasePausesForUserControlAndWave_data()
    {
        QTest::addColumn<int>("pauseMode");
        QTest::newRow("wave") << 0;
        QTest::newRow("hidden") << 1;
        QTest::newRow("mouse-drag") << 2;
    }

    void fingerChasePausesForUserControlAndWave()
    {
        QFETCH(int, pauseMode);
        PetWindow pet;
        pet.show();
        const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
        if (area.width() < pet.width() + 200 || area.height() < pet.height() + 200)
            QSKIP("The test needs room to move the robot");
        pet.move(area.topLeft() + QPoint(20, 20));
        const QPointF local(120, 140);
        const QPointF global = pet.mapToGlobal(local.toPoint());
        if (pauseMode == 0) {
            pet.reactTo(VisualEvent::HandWave);
        } else if (pauseMode == 1) {
            pet.hide();
        } else {
            QMouseEvent press(QEvent::MouseButtonPress, local, global,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&pet, &press);
            QMouseEvent drag(QEvent::MouseMove, local + QPointF(60, 40),
                             global + QPointF(60, 40), Qt::NoButton,
                             Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&pet, &drag);
        }
        const QPoint paused = pet.pos();
        QTimer measurements;
        measurements.setInterval(40);
        connect(&measurements, &QTimer::timeout, &pet,
                [&] { pet.setGazeTarget(true, QPointF(1, 1)); });
        pet.setGazeTarget(true, QPointF(1, 1));
        measurements.start();
        QTest::qWait(160);
        QCOMPARE(pet.pos(), paused);
        QVERIFY(pet.gazeActive());
        if (pauseMode == 0) {
            QCOMPARE(pet.pose(), PetPose::Wave);
            pet.showPose(PetPose::Idle);
        } else if (pauseMode == 1) {
            pet.restore();
        } else {
            QMouseEvent release(QEvent::MouseButtonRelease, local,
                                global + QPointF(60, 40), Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&pet, &release);
        }
        QTRY_VERIFY_WITH_TIMEOUT(pet.pos() != paused, 500);
    }

    void runningFeedbackAnimatesTheWholeRobot()
    {
        const auto render = [](const FollowMotion& motion) {
            QImage image(240, 256, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            paintRobot(painter, QRectF(0, 0, 240, 256), PetPose::Idle,
                       0.4, QPointF(0.5, 0.5), true, motion);
            return image;
        };
        const auto stationary = render(FollowMotion{});
        const auto stepOne = render(FollowMotion{true, 1.0, 0.7});
        const auto stepTwo = render(FollowMotion{true, 1.0, 2.2});
        QVERIFY(stationary != stepOne);
        QVERIFY(stepOne != stepTwo);
        QCOMPARE(stepOne.pixelColor(0, 0).alpha(), 0);
    }

    void onlyTheFirstHandAppearanceInEachCameraSessionGreets()
    {
        PetWindow pet;
        pet.setCameraControls(true, true);
        pet.reactTo(VisualEvent::HandDetected);
        QCOMPARE(pet.pose(), PetPose::Happy);
        pet.showPose(PetPose::Idle);
        for (auto event : {VisualEvent::OpenPalmConfirmed, VisualEvent::HandDetected,
                           VisualEvent::FistConfirmed}) {
            pet.reactTo(event);
            QCOMPARE(pet.pose(), PetPose::Idle);
        }
        pet.reactTo(VisualEvent::HandLost);
        pet.reactTo(VisualEvent::HandDetected);
        QCOMPARE(pet.pose(), PetPose::Surprise);

        // Refreshing controls while still enabled must not rearm the greeting.
        pet.setCameraControls(true, true);
        pet.showPose(PetPose::Idle);
        pet.reactTo(VisualEvent::HandDetected);
        QCOMPARE(pet.pose(), PetPose::Idle);
        pet.setCameraControls(false, true);
        pet.reactTo(VisualEvent::HandDetected);
        QCOMPARE(pet.pose(), PetPose::Idle);
        pet.setCameraControls(true, true);
        pet.reactTo(VisualEvent::HandDetected);
        QCOMPARE(pet.pose(), PetPose::Happy);
    }

    void gazeHasAnExplicitVisibleStatusAndClearsItOnExit()
    {
        PetWindow pet;
        pet.show();
        auto* status = pet.findChild<QLabel*>("gazeStatusLabel");
        QVERIFY(status);
        QVERIFY(status->isHidden());
        pet.setGazeTarget(true, QPointF(0.5, 0.5));
        QVERIFY(status->isVisible());
        QCOMPARE(status->text(), QStringLiteral("食指带路中"));
        pet.setCameraControls(false, true);
        QVERIFY(status->isHidden());
        QVERIFY(!pet.gazeActive());
    }

    void followingIsVisibleEvenWhenTheTargetIsAtTheCenter()
    {
        const auto render = [](bool following) {
            QImage image(240, 256, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            paintRobot(painter, QRectF(0, 0, 240, 256), PetPose::Idle,
                       0.4, QPointF(0.5, 0.5), following);
            return image;
        };
        const auto idle = render(false);
        const auto following = render(true);
        int changedPixels = 0;
        for (int y = 0; y < idle.height(); ++y) {
            for (int x = 0; x < idle.width(); ++x) {
                if (idle.pixel(x, y) == following.pixel(x, y)) continue;
                ++changedPixels;
                QVERIFY(QRect(60, 79, 120, 62).contains(x, y));
            }
        }
        QVERIFY(changedPixels > 400);
    }

    void gazeMovesOnlyTheEyesWithinTheFace()
    {
        const auto render = [](const QPointF& gaze) {
            QImage canvas(240, 256, QImage::Format_ARGB32_Premultiplied);
            canvas.fill(Qt::transparent);
            QPainter painter(&canvas);
            paintRobot(painter, QRectF(0, 0, 240, 256), PetPose::Idle, 0.4, gaze);
            return canvas;
        };
        const auto left = render(QPointF(0, 0));
        const auto right = render(QPointF(1, 1));
        const QRect eyeArea(70, 90, 105, 45);
        int changedPixels = 0;
        for (int y = 0; y < left.height(); ++y) {
            for (int x = 0; x < left.width(); ++x) {
                if (left.pixel(x, y) == right.pixel(x, y)) continue;
                ++changedPixels;
                QVERIFY2(eyeArea.contains(x, y), "Gaze must not move the mouth or body");
            }
        }
        QVERIFY(changedPixels > 200);
        QCOMPARE(left.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(right.pixelColor(0, 0).alpha(), 0);
    }

    void gazeSmoothlyFollowsAndReturnsWhenUpdatesStop()
    {
        PetWindow pet;
        pet.show();
        pet.showPose(PetPose::Happy);
        pet.setGazeTarget(true, QPointF(0.9, 0.1));
        QVERIFY(pet.gazeActive());
        QCOMPARE(pet.pose(), PetPose::Idle);
        QCOMPARE(pet.gazePosition(), QPointF(0.5, 0.5));
        QTRY_VERIFY_WITH_TIMEOUT(pet.gazePosition().x() > 0.7 &&
                                 pet.gazePosition().y() < 0.3, 350);
        QVERIFY(pet.gazeActive());
        QTRY_VERIFY_WITH_TIMEOUT(!pet.gazeActive(), 1100);
        QTRY_VERIFY_WITH_TIMEOUT(std::hypot(pet.gazePosition().x() - 0.5,
                                          pet.gazePosition().y() - 0.5) < 0.01, 700);
    }

    void briefHandLossCannotOverwriteWaveAndGazeThenExpires()
    {
        PetWindow pet;
        pet.show();
        pet.reactTo(VisualEvent::HandWave);
        pet.setGazeTarget(true, QPointF(0.9, 0.1));
        QCOMPARE(pet.pose(), PetPose::Wave);
        QVERIFY(pet.gazeActive());
        pet.setGazeTarget(true, QPointF(std::numeric_limits<double>::quiet_NaN(), 0.2));
        QVERIFY(!pet.gazeActive());
        pet.setGazeTarget(true, QPointF(0.1, 0.8));
        pet.reactTo(VisualEvent::HandLost);
        QVERIFY(pet.gazeActive());
        QCOMPARE(pet.pose(), PetPose::Wave);
        QTest::qWait(650);
        QVERIFY(pet.gazeActive());
        QCOMPARE(pet.pose(), PetPose::Wave);
        QTRY_VERIFY_WITH_TIMEOUT(!pet.gazeActive(), 400);
        QCOMPARE(pet.pose(), PetPose::Wave);
        QTRY_VERIFY_WITH_TIMEOUT(pet.pose() == PetPose::Surprise, 3000);
    }

    void rendersTransparentPoses()
    {
        QImage idle;
        for (auto pose : {PetPose::Idle, PetPose::Happy, PetPose::Wave,
                          PetPose::Sleep, PetPose::Surprise}) {
            QImage canvas(240, 256, QImage::Format_ARGB32_Premultiplied);
            canvas.fill(Qt::transparent);
            {
                QPainter painter(&canvas);
                paintRobot(painter, QRectF(0, 0, 240, 256), pose, 0.4);
            }
            QCOMPARE(canvas.pixelColor(0, 0).alpha(), 0);
            int visible = 0;
            for (int y = 0; y < canvas.height(); ++y)
                for (int x = 0; x < canvas.width(); ++x)
                    visible += canvas.pixelColor(x, y).alpha() > 0;
            QVERIFY(visible > 3000);
            QVERIFY(visible < canvas.width() * canvas.height() * 0.8);
            if (pose == PetPose::Idle) idle = canvas;
            else QVERIFY(canvas != idle);
        }
    }

    void clickAndTemporaryPose()
    {
        PetWindow pet;
        pet.show();
        QTest::mouseClick(&pet, Qt::LeftButton, Qt::NoModifier, QPoint(130, 150));
        QCOMPARE(pet.pose(), PetPose::Happy);
        QTRY_VERIFY_WITH_TIMEOUT(pet.pose() == PetPose::Idle, 3000);
        pet.showPose(PetPose::Sleep);
        pet.reactTo(VisualEvent::HandDetected);
        QCOMPARE(pet.pose(), PetPose::Sleep);
        pet.reactTo(VisualEvent::HandLost);
        QCOMPARE(pet.pose(), PetPose::Surprise);
        pet.reactTo(VisualEvent::OpenPalmConfirmed);
        QCOMPARE(pet.pose(), PetPose::Surprise);
        pet.showPose(PetPose::Sleep);
        pet.reactTo(VisualEvent::FistConfirmed);
        QCOMPARE(pet.pose(), PetPose::Sleep);
    }

    void openPalmCannotTurnAWaveIntoHappyEvenAfterTheAnimationFinishes()
    {
        PetWindow pet;
        pet.show();
        pet.reactTo(VisualEvent::HandWave);
        QCOMPARE(pet.pose(), PetPose::Wave);
        for (auto event : {VisualEvent::OpenPalmConfirmed,
                           VisualEvent::FistConfirmed, VisualEvent::HandLost,
                           VisualEvent::HandDetected}) {
            pet.reactTo(event);
            QCOMPARE(pet.pose(), PetPose::Wave);
        }

        QTRY_VERIFY_WITH_TIMEOUT(pet.pose() == PetPose::Idle, 3000);
        pet.reactTo(VisualEvent::OpenPalmConfirmed);
        QCOMPARE(pet.pose(), PetPose::Idle);
    }

    void dragDoesNotClickAndStaysOnScreen()
    {
        PetWindow pet;
        pet.show();
        const auto area = QGuiApplication::primaryScreen()->availableGeometry();
        pet.move(area.topLeft() + QPoint(20, 20));
        const QPoint start = pet.pos();
        const QPointF local(120, 140);
        const QPointF global = pet.mapToGlobal(local.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, local, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&pet, &press);
        QMouseEvent move(QEvent::MouseMove, local + QPointF(60, 40),
                         global + QPointF(60, 40), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&pet, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, local,
                            global + QPointF(60, 40), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&pet, &release);
        QCOMPARE(pet.pos(), start + QPoint(60, 40));
        QCOMPARE(pet.pose(), PetPose::Idle);
        pet.move(area.right() + 10000, area.bottom() + 10000);
        pet.constrainToScreen();
        QVERIFY(area.contains(pet.frameGeometry()));
    }

    void hidePausesAnimationAndRestoreResumes()
    {
        PetWindow pet;
        pet.show();
        auto* timer = pet.findChild<QTimer*>("animationTimer");
        QVERIFY(timer);
        QVERIFY(timer->isActive());
        pet.hide();
        QVERIFY(!timer->isActive());
        pet.restore();
        QVERIFY(pet.isVisible());
        QVERIFY(timer->isActive());

        // A worker can queue an event without calling QWidget from its thread.
        QMetaObject::invokeMethod(&pet, [&pet] {
            pet.reactTo(VisualEvent::HandWave);
        }, Qt::QueuedConnection);
        QTRY_VERIFY(pet.pose() == PetPose::Wave);
    }
};

QTEST_MAIN(DesktopTests)
#include "desktop_tests.moc"
