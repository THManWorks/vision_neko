#include "pet_window.hpp"

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTest>
#include <QTimer>

using namespace neko::desktop;

class DesktopTests : public QObject {
    Q_OBJECT

private slots:
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
        QCOMPARE(pet.pose(), PetPose::Wave);
        pet.reactTo(VisualEvent::HandLost);
        QCOMPARE(pet.pose(), PetPose::Surprise);
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
