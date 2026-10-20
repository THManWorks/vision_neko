#include "camera_controller.hpp"
#include "pet_window.hpp"

#include <QAction>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace neko::desktop;

namespace {

enum class Frame { Missing, Present, ReadFailure, Throw };

// Reads wait for explicit test input and always notice interruption. No test
// session constructs cv::VideoCapture or accesses a physical camera.
struct Script {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Frame> frames;
    bool holdRead = false;
    bool failOpen = false;
    std::atomic<int> opened{0};
    std::atomic<int> startedReads{0};
    std::atomic<int> completedReads{0};
    std::atomic<int> destroyed{0};
    std::atomic<quintptr> createdOn{0};
    std::atomic<quintptr> openedOn{0};
    std::atomic<quintptr> destroyedOn{0};

    void append(std::initializer_list<Frame> values)
    {
        std::lock_guard<std::mutex> lock(mutex);
        frames.insert(frames.end(), values);
        changed.notify_all();
    }

    void hold(bool value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        holdRead = value;
        changed.notify_all();
    }
};

quintptr currentThreadId()
{
    return reinterpret_cast<quintptr>(QThread::currentThreadId());
}

class ScriptedSession final : public CameraSession {
public:
    explicit ScriptedSession(std::shared_ptr<Script> script) : script_(std::move(script))
    {
        script_->createdOn = currentThreadId();
    }

    ~ScriptedSession() override
    {
        script_->destroyedOn = currentThreadId();
        ++script_->destroyed;
    }

    void open() override
    {
        script_->openedOn = currentThreadId();
        ++script_->opened;
        if (script_->failOpen) throw std::runtime_error("Scripted open failure");
    }

    bool readPresence(bool& hasHand) override
    {
        std::unique_lock<std::mutex> lock(script_->mutex);
        const auto interrupted = [] {
            return QThread::currentThread()->isInterruptionRequested();
        };
        while (script_->frames.empty() && !interrupted())
            script_->changed.wait_for(lock, std::chrono::milliseconds(5));
        if (interrupted()) return false;

        const Frame frame = script_->frames.front();
        script_->frames.pop_front();
        ++script_->startedReads;
        while (script_->holdRead && !interrupted())
            script_->changed.wait_for(lock, std::chrono::milliseconds(5));
        ++script_->completedReads;
        if (frame == Frame::Throw) throw std::runtime_error("Scripted inference failure");
        if (frame == Frame::ReadFailure) return false;
        hasHand = frame == Frame::Present;
        return true;
    }

private:
    std::shared_ptr<Script> script_;
};

CameraSessionFactory scriptedFactory(
    std::vector<std::shared_ptr<Script>> scripts,
    const std::shared_ptr<std::atomic<int>>& calls)
{
    return [scripts = std::move(scripts), calls] {
        const int index = calls->fetch_add(1);
        return std::make_unique<ScriptedSession>(scripts.at(index));
    };
}

} // namespace

class CameraTests : public QObject {
    Q_OBJECT

private slots:
    void defaultOffAndQueuedStableEvents()
    {
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        PetWindow pet;
        auto* cameraAction = pet.findChild<QAction*>("cameraAction");
        auto* cameraStatus = pet.findChild<QAction*>("cameraStatusAction");
        QVERIFY(cameraAction);
        QVERIFY(cameraStatus);
        connect(&pet, &PetWindow::cameraRequested,
                &controller, &CameraController::setEnabled);
        connect(&controller, &CameraController::controlsChanged,
                &pet, &PetWindow::setCameraControls);
        connect(&controller, &CameraController::statusChanged,
                &pet, &PetWindow::setCameraStatus);
        bool deliveredOnGuiThread = true;
        connect(&controller, &CameraController::visualEvent, &pet,
                [&](VisualEvent event) {
            deliveredOnGuiThread &= QThread::currentThread() == pet.thread();
            pet.reactTo(event);
        }, Qt::QueuedConnection);
        QSignalSpy events(&controller, &CameraController::visualEvent);

        QTest::qWait(10);
        QCOMPARE(calls->load(), 0);
        QVERIFY(!controller.active());
        QVERIFY(!controller.busy());
        QVERIFY(!cameraAction->isChecked());

        cameraAction->trigger();
        QTRY_COMPARE(script->opened.load(), 1);
        QVERIFY(cameraAction->isChecked());
        QTRY_VERIFY(cameraStatus->text().contains(QStringLiteral("正在检测手掌")));
        // Interrupted runs of fewer than three frames must not appear.
        script->append({Frame::Present, Frame::Present, Frame::Missing,
                        Frame::Present, Frame::Present});
        QTRY_COMPARE(script->completedReads.load(), 5);
        QCOMPARE(events.count(), 0);
        script->append({Frame::Present, Frame::Present});
        QTRY_COMPARE(events.count(), 1);
        QTRY_VERIFY(pet.pose() == PetPose::Wave);
        QCOMPARE(qvariant_cast<VisualEvent>(events.at(0).at(0)), VisualEvent::HandDetected);

        // Four misses are tolerated; one detection resets the missing count.
        script->append({Frame::Missing, Frame::Missing, Frame::Missing, Frame::Missing,
                        Frame::Present, Frame::Missing, Frame::Missing,
                        Frame::Missing, Frame::Missing});
        QTRY_COMPARE(script->completedReads.load(), 16);
        QCOMPARE(events.count(), 1);
        script->append({Frame::Missing, Frame::Missing});
        QTRY_COMPARE(events.count(), 2);
        QTRY_VERIFY(pet.pose() == PetPose::Surprise);
        QCOMPARE(qvariant_cast<VisualEvent>(events.at(1).at(0)), VisualEvent::HandLost);
        QVERIFY(deliveredOnGuiThread);

        cameraAction->trigger();
        QTRY_VERIFY(!controller.busy());
        QVERIFY(!cameraAction->isChecked());
        QVERIFY(cameraAction->isEnabled());
        QVERIFY(cameraStatus->text().contains(QStringLiteral("未开启")));
        QCOMPARE(script->destroyed.load(), 1);
        QVERIFY(script->createdOn.load() != currentThreadId());
        QCOMPARE(script->createdOn.load(), script->openedOn.load());
        QCOMPARE(script->createdOn.load(), script->destroyedOn.load());
    }

    void stopSuppressesInFlightFrameAndRestartHasFreshState()
    {
        auto first = std::make_shared<Script>();
        auto second = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({first, second}, calls));
        QSignalSpy events(&controller, &CameraController::visualEvent);
        QSignalSpy controls(&controller, &CameraController::controlsChanged);
        controller.setEnabled(true);
        first->append({Frame::Present, Frame::Present});
        QTRY_COMPARE(first->completedReads.load(), 2);
        first->hold(true);
        first->append({Frame::Present});
        QTRY_COMPARE(first->startedReads.load(), 3);

        controller.setEnabled(false);
        QVERIFY(controller.stopping());
        QVERIFY(!controller.active());
        QCOMPARE(controls.last().at(0).toBool(), false);
        QCOMPARE(controls.last().at(1).toBool(), false);
        controller.setEnabled(true); // The old worker still owns the session.
        QCOMPARE(calls->load(), 1);
        first->hold(false);
        QTRY_VERIFY(!controller.busy());
        QCOMPARE(events.count(), 0);
        QCOMPARE(first->destroyed.load(), 1);
        QCOMPARE(controls.last().at(1).toBool(), true);

        controller.setEnabled(true);
        second->append({Frame::Present, Frame::Present});
        QTRY_COMPARE(second->completedReads.load(), 2);
        QCOMPARE(events.count(), 0); // First session's two frames were discarded.
        second->append({Frame::Present});
        QTRY_COMPARE(events.count(), 1);
        controller.shutdown();
        QCOMPARE(second->destroyed.load(), 1);
        QVERIFY(!controller.busy());
    }

    void failuresResetControlsAndAllowRecovery()
    {
        auto badOpen = std::make_shared<Script>();
        badOpen->failOpen = true;
        auto badRead = std::make_shared<Script>();
        badRead->append({Frame::ReadFailure});
        auto badInference = std::make_shared<Script>();
        badInference->append({Frame::Throw});
        auto healthy = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory(
            {badOpen, badRead, badInference, healthy}, calls));
        QSignalSpy controls(&controller, &CameraController::controlsChanged);
        QSignalSpy status(&controller, &CameraController::statusChanged);
        QSignalSpy events(&controller, &CameraController::visualEvent);
        for (const auto& failed : {badOpen, badRead, badInference}) {
            controller.setEnabled(true);
            QTRY_VERIFY(!controller.busy());
            QVERIFY(!controller.active());
            QVERIFY(!controller.stopping());
            QCOMPARE(failed->destroyed.load(), 1);
            QCOMPARE(controls.last().at(0).toBool(), false);
            QCOMPARE(controls.last().at(1).toBool(), true);
            QVERIFY(!status.last().at(0).toString().isEmpty());
        }
        controller.setEnabled(true);
        healthy->append({Frame::Present, Frame::Present, Frame::Present});
        QTRY_COMPARE(events.count(), 1);
        controller.shutdown();
        QCOMPARE(calls->load(), 4);
    }

    void destructionJoinsWorkerAndCleansSessionThere()
    {
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        {
            CameraController controller(scriptedFactory({script}, calls));
            controller.setEnabled(true);
            QTRY_COMPARE(script->opened.load(), 1);
            // It is waiting for a frame when controller destruction interrupts it.
        }
        QCOMPARE(script->destroyed.load(), 1);
        QCOMPARE(script->createdOn.load(), script->destroyedOn.load());
        QVERIFY(script->destroyedOn.load() != currentThreadId());
    }

    void missingModelFailsBeforeCameraAccess()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString missingModel = directory.filePath("missing.onnx");
        QVERIFY(!QFileInfo::exists(missingModel));
        CameraController controller(CameraOptions{missingModel, 0});
        QSignalSpy status(&controller, &CameraController::statusChanged);
        controller.setEnabled(true);
        QTRY_VERIFY(!controller.busy());
        QVERIFY(!controller.active());
        QVERIFY(status.last().at(0).toString().contains(QStringLiteral("模型文件不存在")));
    }
};

QTEST_MAIN(CameraTests)
#include "camera_tests.moc"
