#include "camera_controller.hpp"
#include "pet_window.hpp"
#include "hand_pose.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace neko::desktop;

namespace {

enum class Frame { Missing, Present, OpenPalm, Fist, Conflict, ReadFailure, Throw };

struct ScriptFrame {
    // Implicit conversion keeps the existing enum-only scripts usable, including
    // scripts that mix old presence frames with timed wrist observations.
    ScriptFrame(Frame value) : frame(value) {}

    ScriptFrame(double time, float x, bool valid = true, int hands = 1,
                neko::vision::HandGesture handGesture = neko::vision::HandGesture::OpenPalm,
                float palmSize = 250.0f)
        : frame(hands > 0 ? Frame::Present : Frame::Missing), observation(true),
          timeSeconds(time), wristX(x), poseValid(valid), handCount(hands),
          gesture(handGesture), palmLength(palmSize) {}

    Frame frame;
    bool observation = false;
    double timeSeconds = 0.0;
    float wristX = 0.0f;
    bool poseValid = false;
    int handCount = 0;
    neko::vision::HandGesture gesture = neko::vision::HandGesture::Unknown;
    float palmLength = 250.0f; // At 0.2 palm length, existing scripts still need 50 pixels.
    cv::Point3f indexBase{};
    cv::Size frameSize{640, 480};
};

ScriptFrame scaledPalm(double time, float wristX, float palmLength)
{
    return ScriptFrame(time, wristX, true, 1, neko::vision::HandGesture::OpenPalm, palmLength);
}

ScriptFrame pointingAt(float x, float y, double timeSeconds = 0.0)
{
    ScriptFrame frame(timeSeconds, 100.0f, true, 1, neko::vision::HandGesture::Pointing);
    frame.indexBase = cv::Point3f(x, y, 0.0f);
    return frame;
}

// Reads wait for explicit test input and always notice interruption. No test
// session constructs cv::VideoCapture or accesses a physical camera.
struct Script {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<ScriptFrame> frames;
    bool holdRead = false;
    bool failOpen = false;
    std::atomic<int> opened{0};
    std::atomic<int> requestedReads{0};
    std::atomic<int> startedReads{0};
    std::atomic<int> completedReads{0};
    std::atomic<int> destroyed{0};
    std::atomic<quintptr> createdOn{0};
    std::atomic<quintptr> openedOn{0};
    std::atomic<quintptr> destroyedOn{0};

    void append(std::initializer_list<ScriptFrame> values)
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

    bool readFrame(CameraFrameResult& result) override
    {
        // Requesting the next frame means the worker finished processing the
        // previous one. Tests use this barrier before asserting no wave arrived.
        ++script_->requestedReads;
        std::unique_lock<std::mutex> lock(script_->mutex);
        const auto interrupted = [] {
            return QThread::currentThread()->isInterruptionRequested();
        };
        while (script_->frames.empty() && !interrupted())
            script_->changed.wait_for(lock, std::chrono::milliseconds(5));
        if (interrupted()) return false;

        const ScriptFrame input = script_->frames.front();
        const Frame frame = input.frame;
        script_->frames.pop_front();
        ++script_->startedReads;
        while (script_->holdRead && !interrupted())
            script_->changed.wait_for(lock, std::chrono::milliseconds(5));
        ++script_->completedReads;
        if (frame == Frame::Throw) throw std::runtime_error("Scripted inference failure");
        if (frame == Frame::ReadFailure) return false;
        result = CameraFrameResult{};
        result.hasHand = frame != Frame::Missing;
        result.handCount = result.hasHand ? 1 : 0;
        result.frameSize = input.frameSize;
        if (frame == Frame::OpenPalm) result.gesture = neko::vision::HandGesture::OpenPalm;
        else if (frame == Frame::Fist) result.gesture = neko::vision::HandGesture::Fist;
        if (input.observation) {
            result.hasHand = input.handCount > 0;
            result.handCount = input.handCount;
            result.gesture = input.gesture;
            result.singleHand.valid = input.handCount == 1 && input.poseValid;
            // Translate the wrist and middle-finger base together, keeping
            // the requested palm scale independent of horizontal movement.
            result.singleHand.landmarks[0] = cv::Point3f(input.wristX, 200.0f, 0.0f);
            result.singleHand.landmarks[9] = cv::Point3f(
                input.wristX, 200.0f - input.palmLength, 0.0f);
            result.singleHand.landmarks[5] = input.indexBase;
            result.timeSeconds = input.timeSeconds;
        }
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

int handWaveCount(const QSignalSpy& events)
{
    int count = 0;
    for (const auto& arguments : events) {
        if (qvariant_cast<VisualEvent>(arguments.at(0)) == VisualEvent::HandWave) ++count;
    }
    return count;
}

} // namespace

class CameraTests : public QObject {
    Q_OBJECT

private slots:
    void firstValidPointingFrameImmediatelyStreamsRootPositionsOnTheGuiThread()
    {
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        controller.setDiagnosticsEnabled(true);
        QSignalSpy diagnostics(&controller, &CameraController::frameDiagnostics);
        PetWindow pet;
        bool deliveredOnGuiThread = true;
        connect(&controller, &CameraController::gazeChanged, &pet,
                [&](bool active, const QPointF& position) {
            deliveredOnGuiThread &= QThread::currentThread() == pet.thread();
            pet.setGazeTarget(active, position);
        });
        QSignalSpy gaze(&controller, &CameraController::gazeChanged);
        controller.setEnabled(true);
        QTRY_COMPARE(script->opened.load(), 1);
        gaze.clear();

        // Match the user's position function: one valid frame is sufficient.
        script->append({pointingAt(480, 120)});
        QTRY_COMPARE(gaze.count(), 1);
        QVERIFY(gaze.at(0).at(0).toBool());
        QCOMPARE(gaze.at(0).at(1).toPointF(), QPointF(0.75, 0.25));
        QVERIFY(pet.gazeActive());
        QTRY_VERIFY(!diagnostics.empty());
        const QString record = diagnostics.last().at(0).toString();
        QVERIFY(record.contains("raw=Pointing"));
        QVERIFY(record.contains("size=640x480"));
        QVERIFY(record.contains("pointing_valid=1"));
        QVERIFY(record.contains("root=(480,120)"));
        QVERIFY(!record.contains('%')); // All numeric log placeholders must be substituted.

        script->append({pointingAt(160, 360, 0.1)});
        QTRY_COMPARE(gaze.count(), 2);
        QCOMPARE(gaze.at(1).at(1).toPointF(), QPointF(0.25, 0.75));
        script->append({pointingAt(-20, 520, 0.2)});
        QTRY_COMPARE(gaze.count(), 3);
        QCOMPARE(gaze.at(2).at(1).toPointF(), QPointF(0.0, 1.0));
        QVERIFY(deliveredOnGuiThread);
        controller.shutdown();
        QVERIFY(!pet.gazeActive());
    }

    void explicitExitOrCorruptPointingInputImmediatelyClearsGaze_data()
    {
        QTest::addColumn<int>("reason");
        QTest::newRow("open-palm") << 0;
        QTest::newRow("fist") << 1;
        QTest::newRow("multiple-hands") << 2;
        QTest::newRow("non-finite-root") << 3;
        QTest::newRow("missing-frame-size") << 4;
    }

    void explicitExitOrCorruptPointingInputImmediatelyClearsGaze()
    {
        QFETCH(int, reason);
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        PetWindow pet;
        connect(&controller, &CameraController::gazeChanged, &pet, &PetWindow::setGazeTarget);
        QSignalSpy gaze(&controller, &CameraController::gazeChanged);
        controller.setEnabled(true);
        script->append({pointingAt(480, 120)});
        QTRY_VERIFY(pet.gazeActive());
        gaze.clear();

        auto invalid = pointingAt(480, 120, 0.1);
        if (reason == 0) invalid.gesture = neko::vision::HandGesture::OpenPalm;
        else if (reason == 1) invalid.gesture = neko::vision::HandGesture::Fist;
        else if (reason == 2) invalid.handCount = 2;
        else if (reason == 3) invalid.indexBase.x = std::numeric_limits<float>::quiet_NaN();
        else invalid.frameSize = cv::Size();
        script->append({invalid});
        QTRY_COMPARE(gaze.count(), 1);
        QVERIFY(!gaze.last().at(0).toBool());
        QVERIFY(!pet.gazeActive());

        // A new valid observation is accepted without a second confirmation gate.
        script->append({pointingAt(160, 360, 0.2)});
        QTRY_COMPARE(gaze.count(), 2);
        QVERIFY(gaze.last().at(0).toBool());
        QCOMPARE(gaze.last().at(1).toPointF(), QPointF(0.25, 0.75));
        QVERIFY(pet.gazeActive());
        controller.shutdown();
    }

    void briefUncertainInputHoldsOnlyTheLastRealPosition_data()
    {
        QTest::addColumn<int>("reason");
        QTest::newRow("unknown") << 0;
        QTest::newRow("invalid-pose") << 1;
        QTest::newRow("no-hand") << 2;
    }

    void briefUncertainInputHoldsOnlyTheLastRealPosition()
    {
        QFETCH(int, reason);
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        QSignalSpy gaze(&controller, &CameraController::gazeChanged);
        controller.setEnabled(true);
        QTRY_COMPARE(script->opened.load(), 1);
        gaze.clear();
        script->append({pointingAt(480, 120)});
        QTRY_COMPARE(gaze.count(), 1);
        QVERIFY(gaze.last().at(0).toBool());

        auto gap = pointingAt(9000, 9000, 0.2);
        if (reason == 0) gap.gesture = neko::vision::HandGesture::Unknown;
        else if (reason == 1) gap.poseValid = false;
        else gap.handCount = 0;
        auto boundary = gap;
        boundary.timeSeconds = 0.5;
        script->append({gap, boundary});
        QTRY_VERIFY(script->requestedReads.load() >= 4);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(gaze.count(), 1); // No false event, no fabricated position refresh.
        QCOMPARE(gaze.last().at(1).toPointF(), QPointF(0.75, 0.25));

        script->append({pointingAt(160, 360, 0.51)});
        QTRY_COMPARE(gaze.count(), 2);
        QVERIFY(gaze.last().at(0).toBool());
        QCOMPARE(gaze.last().at(1).toPointF(), QPointF(0.25, 0.75));
        controller.shutdown();
    }

    void uncertainInputLongerThanHalfASecondClearsGazeWithoutInventingCoordinates()
    {
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        QSignalSpy gaze(&controller, &CameraController::gazeChanged);
        controller.setEnabled(true);
        QTRY_COMPARE(script->opened.load(), 1);
        gaze.clear();
        script->append({pointingAt(480, 120)});
        QTRY_COMPARE(gaze.count(), 1);
        auto gap = pointingAt(9000, 9000, 0.49);
        gap.poseValid = false;
        script->append({gap});
        QTRY_VERIFY(script->requestedReads.load() >= 3);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(gaze.count(), 1);
        gap.timeSeconds = 0.51;
        script->append({gap});
        QTRY_COMPARE(gaze.count(), 2);
        QVERIFY(!gaze.last().at(0).toBool());
        QCOMPARE(gaze.last().at(1).toPointF(), QPointF(0.5, 0.5));

        script->append({pointingAt(160, 360, 0.6)});
        QTRY_COMPARE(gaze.count(), 3);
        QVERIFY(gaze.last().at(0).toBool());
        QCOMPARE(gaze.last().at(1).toPointF(), QPointF(0.25, 0.75));
        controller.shutdown();
    }

    void gazeStopSuppressesAnInFlightPositionAndRestartWaitsForANewObservation()
    {
        auto first = std::make_shared<Script>();
        auto second = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({first, second}, calls));
        PetWindow pet;
        connect(&controller, &CameraController::gazeChanged, &pet, &PetWindow::setGazeTarget);
        QSignalSpy gaze(&controller, &CameraController::gazeChanged);
        controller.setEnabled(true);
        first->append({pointingAt(480, 120)});
        QTRY_VERIFY(pet.gazeActive());
        first->hold(true);
        first->append({pointingAt(160, 360, 0.1)});
        QTRY_COMPARE(first->startedReads.load(), 2);
        controller.setEnabled(false);
        QVERIFY(!pet.gazeActive());
        QTRY_VERIFY(!controller.busy());
        QVERIFY(!pet.gazeActive());

        controller.setEnabled(true);
        QTRY_COMPARE(second->opened.load(), 1);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QVERIFY(!pet.gazeActive()); // Old position cannot reactivate a fresh session.
        gaze.clear();
        second->append({pointingAt(160, 360)});
        QTRY_COMPARE(gaze.count(), 1);
        QVERIFY(gaze.last().at(0).toBool());
        QCOMPARE(gaze.last().at(1).toPointF(), QPointF(0.25, 0.75));
        QVERIFY(pet.gazeActive());
        second->append({Frame::ReadFailure});
        QTRY_VERIFY(!controller.busy());
        QVERIFY(!pet.gazeActive());
    }
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
        QTRY_VERIFY(cameraStatus->text().contains(QStringLiteral("正在识别手势")));
        // Interrupted runs of fewer than three frames must not appear.
        script->append({Frame::Present, Frame::Present, Frame::Missing,
                        Frame::Present, Frame::Present});
        QTRY_COMPARE(script->completedReads.load(), 5);
        QCOMPARE(events.count(), 0);
        script->append({Frame::Present, Frame::Present});
        QTRY_COMPARE(events.count(), 1);
        QCoreApplication::sendPostedEvents(&pet, QEvent::MetaCall);
        QCOMPARE(pet.pose(), PetPose::Happy);
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

    void stableGesturesDrivePetAndRearmAfterUnknownOrMissing()
    {
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        PetWindow pet;
        connect(&controller, &CameraController::controlsChanged,
                &pet, &PetWindow::setCameraControls);
        bool deliveredOnGuiThread = true;
        connect(&controller, &CameraController::visualEvent, &pet, [&](VisualEvent event) {
            deliveredOnGuiThread &= QThread::currentThread() == pet.thread();
            pet.reactTo(event);
        });
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);

        script->append({Frame::OpenPalm, Frame::OpenPalm});
        QTRY_COMPARE(script->completedReads.load(), 2);
        QCOMPARE(events.count(), 0);
        script->append({Frame::OpenPalm});
        QTRY_COMPARE(events.count(), 2); // Presence first, then the confirmed gesture.
        QCOMPARE(qvariant_cast<VisualEvent>(events.at(0).at(0)), VisualEvent::HandDetected);
        QCOMPARE(qvariant_cast<VisualEvent>(events.at(1).at(0)), VisualEvent::OpenPalmConfirmed);
        QCOMPARE(pet.pose(), PetPose::Happy);

        script->append({Frame::OpenPalm, Frame::OpenPalm, Frame::OpenPalm});
        QTRY_COMPARE(script->completedReads.load(), 6);
        QCOMPARE(events.count(), 2); // Holding a gesture does not restart the animation.
        pet.showPose(PetPose::Sleep);
        script->append({Frame::Fist, Frame::Fist});
        QTRY_COMPARE(script->completedReads.load(), 8);
        QCOMPARE(events.count(), 2);
        QCOMPARE(pet.pose(), PetPose::Sleep);
        script->append({Frame::Fist});
        QTRY_COMPARE(events.count(), 3);
        QCOMPARE(qvariant_cast<VisualEvent>(events.last().at(0)), VisualEvent::FistConfirmed);
        QCOMPARE(pet.pose(), PetPose::Sleep); // Fist is reported without playing an action.

        // Four unknown frames cannot clear Fist; a Fist frame interrupts that count.
        script->append({Frame::Present, Frame::Present, Frame::Present, Frame::Present,
                        Frame::Fist});
        QTRY_COMPARE(script->completedReads.load(), 14);
        QCOMPARE(events.count(), 3);
        // Conflicting gestures are supplied as an unknown frame with a hand present.
        script->append({Frame::Conflict, Frame::Conflict, Frame::Conflict,
                        Frame::Conflict, Frame::Conflict});
        QTRY_COMPARE(script->completedReads.load(), 19);
        QCOMPARE(events.count(), 3); // Clearing does not produce a desktop action.
        script->append({Frame::Fist, Frame::Fist, Frame::Fist});
        QTRY_COMPARE(events.count(), 4); // The same gesture can be confirmed again.
        QCOMPARE(qvariant_cast<VisualEvent>(events.last().at(0)), VisualEvent::FistConfirmed);
        QCOMPARE(pet.pose(), PetPose::Sleep);

        script->append({Frame::Missing, Frame::Missing, Frame::Missing,
                        Frame::Missing, Frame::Missing});
        QTRY_COMPARE(events.count(), 5);
        QCOMPARE(qvariant_cast<VisualEvent>(events.last().at(0)), VisualEvent::HandLost);
        QCOMPARE(pet.pose(), PetPose::Surprise); // Cleared must not overwrite this pose.
        script->append({Frame::OpenPalm, Frame::OpenPalm, Frame::OpenPalm});
        QTRY_COMPARE(events.count(), 7);
        QCOMPARE(qvariant_cast<VisualEvent>(events.last().at(0)), VisualEvent::OpenPalmConfirmed);
        QCOMPARE(pet.pose(), PetPose::Surprise); // Reappearance cannot greet twice in one session.
        QVERIFY(deliveredOnGuiThread);
        controller.shutdown();
    }

    void singleHandWaveReachesGuiAndDrivesPet()
    {
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        PetWindow pet;
        int guiWaves = 0;
        bool deliveredOnGuiThread = true;
        connect(&controller, &CameraController::visualEvent, &pet, [&](VisualEvent event) {
            if (event == VisualEvent::HandWave) {
                ++guiWaves;
                deliveredOnGuiThread &= QThread::currentThread() == pet.thread();
            }
            pet.reactTo(event);
        }, Qt::QueuedConnection);
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);

        // Valid wrist positions are 100 -> 150 -> 100 -> 150. Brief invalid
        // pose/missing frames must neither move the wrist nor lose the first leg.
        script->append({Frame::Missing, ScriptFrame(0.0, 100), ScriptFrame(0.1, 150),
                        ScriptFrame(0.15, 900, false), ScriptFrame(0.2, 900, false, 0),
                        ScriptFrame(0.3, 100)});
        QTRY_COMPARE(script->requestedReads.load(), 7);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCoreApplication::sendPostedEvents(&pet, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);

        // Start from Happy so the dynamic event must choose the distinct Wave
        // action after the static open-palm observations have been processed.
        pet.showPose(PetPose::Happy);
        QCOMPARE(pet.pose(), PetPose::Happy);
        script->append({ScriptFrame(0.4, 150)});
        QTRY_COMPARE(script->requestedReads.load(), 8);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 1);
        QTRY_COMPARE(guiWaves, 1);
        QCOMPARE(pet.pose(), PetPose::Wave);
        QVERIFY(deliveredOnGuiThread);
        controller.shutdown();
    }

    void waveMovementAdaptsToPalmScale_data()
    {
        QTest::addColumn<float>("palmLength");
        QTest::addColumn<float>("movement");
        QTest::addColumn<int>("expectedWaves");
        QTest::newRow("small-palm-small-motion") << 80.0f << 16.0f << 1;
        QTest::newRow("large-palm-same-small-motion") << 110.0f << 16.0f << 0;
        QTest::newRow("large-palm-larger-motion") << 110.0f << 22.0f << 1;
        QTest::newRow("just-below-small-palm-threshold") << 80.0f << 15.0f << 0;
    }

    void waveMovementAdaptsToPalmScale()
    {
        QFETCH(float, palmLength);
        QFETCH(float, movement);
        QFETCH(int, expectedWaves);
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);

        script->append({scaledPalm(0.0, 100, palmLength),
                        scaledPalm(0.1, 100 + movement, palmLength),
                        scaledPalm(0.2, 100, palmLength)});
        QTRY_COMPARE(script->requestedReads.load(), 4);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0); // One reversal never completes a wave.
        script->append({scaledPalm(0.3, 100 + movement, palmLength)});
        QTRY_COMPARE(script->requestedReads.load(), 5);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), expectedWaves);
        controller.shutdown();
    }

    void palmScaleChangesDoNotAlterAnActiveRound_data()
    {
        QTest::addColumn<float>("startingPalm");
        QTest::addColumn<float>("laterPalm");
        QTest::addColumn<int>("smallMotionWaves");
        QTest::newRow("palm-grows") << 80.0f << 110.0f << 1;
        QTest::newRow("palm-shrinks") << 110.0f << 80.0f << 0;
    }

    void palmScaleChangesDoNotAlterAnActiveRound()
    {
        QFETCH(float, startingPalm);
        QFETCH(float, laterPalm);
        QFETCH(int, smallMotionWaves);
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);

        // Only the initial scale determines whether this 16-pixel wave is large
        // enough, even though every subsequent pose reports the other scale.
        script->append({scaledPalm(0.0, 100, startingPalm), scaledPalm(0.1, 116, laterPalm),
                        scaledPalm(0.2, 100, laterPalm), scaledPalm(0.3, 116, laterPalm)});
        QTRY_COMPARE(script->requestedReads.load(), 5);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), smallMotionWaves);
        if (smallMotionWaves == 0) {
            // The large starting palm still needs a larger movement after shrinking.
            script->append({scaledPalm(0.4, 122, laterPalm), scaledPalm(0.5, 100, laterPalm),
                            scaledPalm(0.6, 122, laterPalm)});
            QTRY_COMPARE(script->requestedReads.load(), 8);
            QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
            QCOMPARE(handWaveCount(events), 1);
        }
        controller.shutdown();
    }

    void aNewWaveRoundUsesTheCurrentPalmScale_data()
    {
        QTest::addColumn<bool>("longLoss");
        QTest::newRow("overall-timeout") << false;
        QTest::newRow("long-hand-loss") << true;
    }

    void aNewWaveRoundUsesTheCurrentPalmScale()
    {
        QFETCH(bool, longLoss);
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);

        // Leave one turn pending at the original, larger palm scale.
        script->append({scaledPalm(0.0, 100, 110), scaledPalm(0.1, 122, 110),
                        scaledPalm(0.2, 100, 110)});
        if (longLoss) {
            script->append({ScriptFrame(0.8, 100, false, 0)});
        } else {
            // Valid inputs keep all gaps under 0.5 seconds while the round expires.
            script->append({scaledPalm(0.6, 100, 110), scaledPalm(1.0, 100, 110),
                            scaledPalm(1.4, 100, 110)});
        }
        // Direction was first established at 0.1, so its 1.5-second window
        // expires at 1.6, rather than 1.5 seconds after initialization.
        const double restartedAt = longLoss ? 0.9 : 1.7;
        script->append({scaledPalm(restartedAt, 100, 80),
                        scaledPalm(restartedAt + 0.1, 116, 80),
                        scaledPalm(restartedAt + 0.2, 100, 80)});
        QTRY_COMPARE(script->requestedReads.load(), longLoss ? 8 : 10);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0); // The old turn cannot survive either reset.
        script->append({scaledPalm(restartedAt + 0.3, 116, 80)});
        QTRY_COMPARE(script->requestedReads.load(), longLoss ? 9 : 11);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 1); // The new smaller palm permits this motion.
        controller.shutdown();
    }

    void invalidStartingPalmScaleWaitsForAValidNewRound_data()
    {
        QTest::addColumn<float>("invalidPalm");
        QTest::newRow("zero") << 0.0f;
        QTest::newRow("nan") << std::numeric_limits<float>::quiet_NaN();
        QTest::newRow("infinity") << std::numeric_limits<float>::infinity();
    }

    void invalidStartingPalmScaleWaitsForAValidNewRound()
    {
        QFETCH(float, invalidPalm);
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);

        // A moving wrist with unusable scale must never create a zero-threshold wave.
        script->append({scaledPalm(0.0, 100, invalidPalm), scaledPalm(0.1, 150, invalidPalm),
                        scaledPalm(0.2, 100, invalidPalm), scaledPalm(0.3, 150, invalidPalm)});
        QTRY_COMPARE(script->requestedReads.load(), 5);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);

        script->append({scaledPalm(0.4, 100, 80), scaledPalm(0.5, 116, 80),
                        scaledPalm(0.6, 100, 80)});
        QTRY_COMPARE(script->requestedReads.load(), 8);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);
        script->append({scaledPalm(0.7, 116, 80)});
        QTRY_COMPARE(script->requestedReads.load(), 9);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 1);
        controller.shutdown();
    }

    void subthresholdReversalsDoNotAccumulateIntoAWave()
    {
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);

        // Establish a direction at 16 pixels, then repeatedly reverse by only
        // 15 pixels. Small oscillations must not accumulate into real turns.
        script->append({scaledPalm(0.0, 100, 80), scaledPalm(0.1, 116, 80),
                        scaledPalm(0.2, 101, 80), scaledPalm(0.3, 116, 80),
                        scaledPalm(0.4, 101, 80), scaledPalm(0.5, 116, 80),
                        scaledPalm(0.6, 101, 80), scaledPalm(0.7, 116, 80)});
        QTRY_COMPARE(script->requestedReads.load(), 9);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);

        script->append({scaledPalm(0.8, 100, 80)});
        QTRY_COMPARE(script->requestedReads.load(), 10);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0); // The first full reversal still needs another.
        script->append({scaledPalm(0.9, 116, 80)});
        QTRY_COMPARE(script->requestedReads.load(), 11);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 1);
        controller.shutdown();
    }

    void waveIgnoresStationaryAndOneWayMotionAndHonorsCooldown()
    {
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);

        script->append({ScriptFrame(0.0, 100), ScriptFrame(0.1, 100),
                        ScriptFrame(0.2, 100), ScriptFrame(0.3, 100),
                        ScriptFrame(0.4, 150), ScriptFrame(0.5, 200),
                        ScriptFrame(0.6, 250), ScriptFrame(0.7, 300)});
        QTRY_COMPARE(script->requestedReads.load(), 9);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);

        script->append({ScriptFrame(0.8, 250), ScriptFrame(0.9, 300)});
        QTRY_COMPARE(handWaveCount(events), 1);
        // These would form another full wave without the one-second cooldown.
        script->append({ScriptFrame(1.0, 100), ScriptFrame(1.1, 150),
                        ScriptFrame(1.2, 100), ScriptFrame(1.3, 150)});
        QTRY_COMPARE(script->requestedReads.load(), 15);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 1);

        // The cooldown expires at 1.9. A fresh complete wave is still required.
        script->append({ScriptFrame(1.9, 100), ScriptFrame(2.0, 150),
                        ScriptFrame(2.1, 100)});
        QTRY_COMPARE(script->requestedReads.load(), 18);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 1);
        script->append({ScriptFrame(2.2, 150)});
        QTRY_COMPARE(handWaveCount(events), 2);
        controller.shutdown();
    }

    void waveStateResetsAfterLongInputGap_data()
    {
        QTest::addColumn<int>("interruption");
        QTest::newRow("missing-hand") << 0;
        QTest::newRow("invalid-pose") << 1;
        QTest::newRow("closed-hand") << 2;
    }

    void waveStateResetsAfterLongInputGap()
    {
        QFETCH(int, interruption);
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);

        const ScriptFrame gap(0.8, 150, interruption != 1, interruption == 0 ? 0 : 1,
                              interruption == 2 ? neko::vision::HandGesture::Fist
                                                : neko::vision::HandGesture::OpenPalm);
        // The first three frames leave one turn pending. The unavailable input
        // is 0.6 seconds after the last valid palm, so it must erase that turn.
        script->append({ScriptFrame(0.0, 100), ScriptFrame(0.1, 150),
                        ScriptFrame(0.2, 100), gap, ScriptFrame(0.9, 150)});
        QTRY_COMPARE(script->requestedReads.load(), 6);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);

        script->append({ScriptFrame(1.0, 100), ScriptFrame(1.1, 150)});
        QTRY_COMPARE(script->requestedReads.load(), 8);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);
        script->append({ScriptFrame(1.2, 100)});
        QTRY_COMPARE(handWaveCount(events), 1);
        controller.shutdown();
    }

    void multipleHandsImmediatelyResetPendingWave()
    {
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);

        // Even a brief two-hand frame erases the pending turn. Both hands can
        // report OpenPalm; the static summary must not continue a single wrist.
        script->append({ScriptFrame(0.0, 100), ScriptFrame(0.1, 150),
                        ScriptFrame(0.2, 100), ScriptFrame(0.25, 100, true, 2),
                        ScriptFrame(0.3, 150)});
        QTRY_COMPARE(script->requestedReads.load(), 6);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);

        script->append({ScriptFrame(0.4, 100), ScriptFrame(0.5, 150)});
        QTRY_COMPARE(script->requestedReads.load(), 8);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);
        script->append({ScriptFrame(0.6, 100)});
        QTRY_COMPARE(handWaveCount(events), 1);
        controller.shutdown();
    }

    void stationaryWaitingDoesNotConsumeTheMovementWindow()
    {
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);

        // Keep supplying a valid stationary palm for over three seconds, so
        // neither an input gap nor the waiting time can reset an active wave.
        script->append({ScriptFrame(0.0, 100), ScriptFrame(0.4, 100),
                        ScriptFrame(0.8, 100), ScriptFrame(1.2, 100),
                        ScriptFrame(1.6, 100), ScriptFrame(2.0, 100),
                        ScriptFrame(2.4, 100), ScriptFrame(2.8, 100),
                        ScriptFrame(3.2, 100), ScriptFrame(3.5, 150),
                        ScriptFrame(3.9, 125), ScriptFrame(4.3, 100),
                        ScriptFrame(4.7, 100)});
        QTRY_COMPARE(script->requestedReads.load(), 14);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);

        // The second turn is 1.4 seconds after the first effective movement.
        // Starting the window while waiting would incorrectly discard it.
        script->append({ScriptFrame(4.9, 150)});
        QTRY_COMPARE(handWaveCount(events), 1);
        controller.shutdown();
    }

    void waveMustCompleteBothTurnsWithinOneAndAHalfSecondsOfMovement()
    {
        auto script = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({script}, calls));
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);

        // Every valid gap is below 0.5 seconds, isolating the overall timeout.
        // Movement starts at 0.25; exactly 1.5 seconds later, at 1.75, the old
        // first turn must no longer count. Quarter-second endpoints are exact.
        script->append({ScriptFrame(0.0, 100), ScriptFrame(0.25, 150),
                        ScriptFrame(0.65, 125), ScriptFrame(1.05, 100),
                        ScriptFrame(1.45, 100), ScriptFrame(1.75, 150)});
        QTRY_COMPARE(script->requestedReads.load(), 7);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);

        script->append({ScriptFrame(1.85, 100), ScriptFrame(1.95, 150)});
        QTRY_COMPARE(script->requestedReads.load(), 9);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);
        script->append({ScriptFrame(2.05, 100)});
        QTRY_COMPARE(handWaveCount(events), 1);
        controller.shutdown();
    }

    void stopSuppressesPendingWaveAndRestartRequiresTwoNewTurns()
    {
        auto first = std::make_shared<Script>();
        auto second = std::make_shared<Script>();
        auto calls = std::make_shared<std::atomic<int>>(0);
        CameraController controller(scriptedFactory({first, second}, calls));
        QSignalSpy events(&controller, &CameraController::visualEvent);
        controller.setEnabled(true);
        first->append({ScriptFrame(0.0, 100), ScriptFrame(0.1, 150),
                       ScriptFrame(0.2, 100)});
        QTRY_COMPARE(first->requestedReads.load(), 4);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);

        first->hold(true);
        first->append({ScriptFrame(0.3, 150)}); // This would complete the wave.
        QTRY_COMPARE(first->startedReads.load(), 4);
        controller.setEnabled(false);
        QVERIFY(!controller.active());
        first->hold(false);
        QTRY_VERIFY(!controller.busy());
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);
        QCOMPARE(first->destroyed.load(), 1);

        controller.setEnabled(true);
        // A new session starts at zero seconds and cannot inherit the old turn.
        second->append({ScriptFrame(0.0, 100), ScriptFrame(0.1, 150),
                        ScriptFrame(0.2, 100)});
        QTRY_COMPARE(second->requestedReads.load(), 4);
        QCoreApplication::sendPostedEvents(&controller, QEvent::MetaCall);
        QCOMPARE(handWaveCount(events), 0);
        second->append({ScriptFrame(0.3, 150)});
        QTRY_COMPARE(handWaveCount(events), 1);
        controller.shutdown();
        QCOMPARE(calls->load(), 2);
        QCOMPARE(second->destroyed.load(), 1);
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
        first->append({Frame::OpenPalm, Frame::OpenPalm});
        QTRY_COMPARE(first->completedReads.load(), 2);
        first->hold(true);
        first->append({Frame::OpenPalm});
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
        second->append({Frame::OpenPalm, Frame::OpenPalm});
        QTRY_COMPARE(second->completedReads.load(), 2);
        QCOMPARE(events.count(), 0); // First session's two frames were discarded.
        second->append({Frame::OpenPalm});
        QTRY_COMPARE(events.count(), 2);
        QCOMPARE(qvariant_cast<VisualEvent>(events.last().at(0)), VisualEvent::OpenPalmConfirmed);
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

    void missingHandPoseModelIsCheckedBeforeLoadingOrCameraAccess()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString palmPath = directory.filePath("palm.onnx");
        QFile placeholder(palmPath);
        QVERIFY(placeholder.open(QIODevice::WriteOnly));
        placeholder.write("not an ONNX model");
        placeholder.close();
        CameraController controller(CameraOptions{
            palmPath, 0, directory.filePath("missing-hand-pose.onnx")});
        QSignalSpy status(&controller, &CameraController::statusChanged);
        controller.setEnabled(true);
        QTRY_VERIFY(!controller.busy());
        QVERIFY(!controller.active());
        QVERIFY(status.last().at(0).toString().contains(QStringLiteral("手部姿态模型文件不存在")));
    }

    void localModelsAcceptInputsWithoutCamera()
    {
        // Model files are intentionally not in Git. This extra check is optional elsewhere.
        const QString palmPath = QFINDTESTDATA("../models/palm_detection_mediapipe_2023feb.onnx");
        const QString handPath = QFINDTESTDATA("../models/handpose_estimation_mediapipe_2023feb.onnx");
        if (palmPath.isEmpty() || handPath.isEmpty()) QSKIP("Local ONNX models not installed");

        auto palmModel = neko::vision::loadPalmModel(palmPath.toStdString());
        const cv::Mat frame(480, 640, CV_8UC3, cv::Scalar(0, 0, 0));
        const auto input = neko::vision::preprocessPalmFrame(frame);
        const auto raw = neko::vision::runPalmInference(palmModel, input.inputBlob);
        QCOMPARE(raw.rawScores.total(), std::size_t(2016));
        QCOMPARE(raw.rawBoxes.total(), std::size_t(2016 * 18));

        auto handModel = neko::vision::loadHandPoseModel(handPath.toStdString());
        const int shape[] = {1, 224, 224, 3};
        const cv::Mat blob(4, shape, CV_32F, cv::Scalar(0));
        const auto handRaw = neko::vision::runHandPoseInference(handModel, blob);
        QCOMPARE(handRaw.rawLandmarks.total(), std::size_t(63));
        QCOMPARE(handRaw.rawConfidence.total(), std::size_t(1));
        QCOMPARE(handRaw.rawHandedness.total(), std::size_t(1));
        QCOMPARE(handRaw.rawWorldLandmarks.total(), std::size_t(63));
    }
};

QTEST_MAIN(CameraTests)
#include "camera_tests.moc"
