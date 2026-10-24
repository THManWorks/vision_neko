#include "pet_window.hpp"

#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTest>
#include <QTimer>

#include <cmath>
#include <limits>

using namespace neko::desktop;

class DesktopTests : public QObject {
    Q_OBJECT

private slots:
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
        QTRY_VERIFY_WITH_TIMEOUT(!pet.gazeActive(), 400);
        QCOMPARE(pet.pos(), stopped);
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
        QTRY_VERIFY_WITH_TIMEOUT(!pet.gazeActive(), 800);
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
        QTRY_VERIFY_WITH_TIMEOUT(!pet.gazeActive(), 800);
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
