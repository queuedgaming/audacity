/*
* Audacity: A Digital Audio Editor
*/
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "qml/Audacity/Record/artistmodemodel.h"

#include "actions/tests/mocks/actionsdispatchermock.h"
#include "context/tests/mocks/globalcontextmock.h"
#include "trackedit/tests/mocks/selectioncontrollermock.h"
#include "trackedit/tests/mocks/trackeditprojectmock.h"
#include "mocks/recordconfigurationmock.h"
#include "mocks/recordcontrollermock.h"
#include "mocks/recordmock.h"

using namespace au;
using namespace au::record;
using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

namespace {
MATCHER_P(IsQuery, uri, "") {
    return arg.toString() == uri;
}
}

class ArtistModeModelTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_context = std::make_shared<NiceMock<context::GlobalContextMock> >();
        m_record = std::make_shared<NiceMock<RecordMock> >();
        m_controller = std::make_shared<NiceMock<RecordControllerMock> >();
        m_config = std::make_shared<NiceMock<RecordConfigurationMock> >();
        m_selection = std::make_shared<NiceMock<trackedit::SelectionControllerMock> >();
        m_project = std::make_shared<NiceMock<trackedit::TrackeditProjectMock> >();
        m_dispatcher = std::make_shared<NiceMock<muse::actions::ActionsDispatcherMock> >();

        ON_CALL(*m_context, currentTrackeditProject()).WillByDefault(Return(m_project));
        ON_CALL(*m_context, currentTrackeditProjectChanged()).WillByDefault(Return(m_projectChanged));
        ON_CALL(*m_record, recordingClipKeys()).WillByDefault(ReturnRef(m_recordedKeys));
        ON_CALL(*m_record, recordingFinished()).WillByDefault(Return(m_recordingFinished));
        ON_CALL(*m_record, projectKeyLabel(_)).WillByDefault(Return(std::string("G maj")));
        ON_CALL(*m_controller, isRecordingChanged()).WillByDefault(Return(m_isRecordingChanged));
        ON_CALL(*m_selection, tracksSelected()).WillByDefault(Return(m_tracksSelected));
        ON_CALL(*m_project, timeSignature()).WillByDefault(Return(trackedit::TimeSignature { 96.0, 3, 4 }));
        ON_CALL(*m_project, trackName(_)).WillByDefault(Return(std::optional<std::string>("Vocals")));
        ON_CALL(*m_config, leadInTimeDuration()).WillByDefault(Return(2.0));

        m_model = std::make_unique<ArtistModeModel>();
        m_model->globalContext.set(m_context);
        m_model->record.set(m_record);
        m_model->recordController.set(m_controller);
        m_model->configuration.set(m_config);
        m_model->selectionController.set(m_selection);
        m_model->dispatcher.set(m_dispatcher);
    }

    std::shared_ptr<NiceMock<context::GlobalContextMock> > m_context;
    std::shared_ptr<NiceMock<RecordMock> > m_record;
    std::shared_ptr<NiceMock<RecordControllerMock> > m_controller;
    std::shared_ptr<NiceMock<RecordConfigurationMock> > m_config;
    std::shared_ptr<NiceMock<trackedit::SelectionControllerMock> > m_selection;
    std::shared_ptr<NiceMock<trackedit::TrackeditProjectMock> > m_project;
    std::shared_ptr<NiceMock<muse::actions::ActionsDispatcherMock> > m_dispatcher;
    std::unique_ptr<ArtistModeModel> m_model;

    std::vector<trackedit::ClipKey> m_recordedKeys;
    muse::async::Notification m_projectChanged;
    muse::async::Notification m_recordingFinished;
    muse::async::Notification m_isRecordingChanged;
    muse::async::Channel<trackedit::TrackIdList> m_tracksSelected;
};

TEST_F(ArtistModeModelTests, MapsProjectAndTrackState)
{
    ON_CALL(*m_selection, selectedTracks()).WillByDefault(Return(trackedit::TrackIdList { 7 }));
    ON_CALL(*m_record, takeCount(trackedit::TrackId(7))).WillByDefault(Return(2));
    ON_CALL(*m_config, artistModeEnabled()).WillByDefault(Return(true));
    m_model->init();

    EXPECT_TRUE(m_model->active());
    EXPECT_EQ(m_model->tempoText(), "96 BPM");
    EXPECT_EQ(m_model->timeSignatureText(), "3/4");
    EXPECT_EQ(m_model->keyText(), "G maj");
    EXPECT_EQ(m_model->trackName(), "Vocals");
    EXPECT_EQ(m_model->takeLaneCount(), 2);
    EXPECT_EQ(m_model->takeNumber(), 3);
    EXPECT_EQ(m_model->countInSeconds(), 2.0);

    //! position as mm:ss.t
    EXPECT_EQ(ArtistModeModel::formatPosition(0.0), "00:00.0");
    EXPECT_EQ(ArtistModeModel::formatPosition(83.47), "01:23.4");
    EXPECT_EQ(ArtistModeModel::formatPosition(-1.0), "00:00.0");
    m_model->setPosition(5.25);
    EXPECT_EQ(m_model->positionText(), "00:05.2");
}

TEST_F(ArtistModeModelTests, NoTrackSelectedMeansANewTrack)
{
    ON_CALL(*m_selection, selectedTracks()).WillByDefault(Return(trackedit::TrackIdList {}));
    m_model->init();
    EXPECT_TRUE(m_model->trackName().isEmpty());
    EXPECT_EQ(m_model->takeNumber(), 1);
}

TEST_F(ArtistModeModelTests, TogglesWriteTheSharedSettings)
{
    m_model->init();
    EXPECT_CALL(*m_config, setCountInEnabled(true));
    m_model->setCountIn(true);
    EXPECT_CALL(*m_config, setPunchOutAtSelectionEnd(false));
    m_model->setPunchOut(false);
    EXPECT_CALL(*m_config, setKeepTakesWhenRecordingOver(true));
    m_model->setKeepTakes(true);
    EXPECT_CALL(*m_config, setMetronomeEnabled(true));
    m_model->setMetronome(true);
    //! leaving the mode is the same per-user setting the View menu toggles
    EXPECT_CALL(*m_config, setArtistModeEnabled(false));
    m_model->exitMode();
}

TEST_F(ArtistModeModelTests, ButtonsDispatchTheExistingActions)
{
    m_model->init();
    ON_CALL(*m_config, countInEnabled()).WillByDefault(Return(false));
    EXPECT_CALL(*m_dispatcher, dispatch(::testing::Matcher<const muse::actions::ActionQuery&>(IsQuery("action://record/start"))));
    m_model->toggleRecord();

    ON_CALL(*m_config, countInEnabled()).WillByDefault(Return(true));
    EXPECT_CALL(*m_dispatcher, dispatch(::testing::Matcher<const muse::actions::ActionQuery&>(IsQuery("action://record/lead-in-recording"))));
    m_model->toggleRecord();

    ON_CALL(*m_controller, isRecording()).WillByDefault(Return(true));
    EXPECT_CALL(*m_dispatcher, dispatch(::testing::Matcher<const muse::actions::ActionQuery&>(IsQuery("action://record/stop")))).Times(2);
    m_model->toggleRecord();
    m_model->stop();

    ON_CALL(*m_controller, isRecording()).WillByDefault(Return(false));
    EXPECT_CALL(*m_dispatcher, dispatch(::testing::Matcher<const muse::actions::ActionQuery&>(IsQuery("action://playback/stop"))));
    m_model->stop();
    EXPECT_CALL(*m_dispatcher, dispatch(::testing::Matcher<const muse::actions::ActionQuery&>(IsQuery("action://record/next-take"))));
    m_model->nextTake();
    EXPECT_CALL(*m_dispatcher, dispatch(::testing::Matcher<const muse::actions::ActionQuery&>(IsQuery("action://record/activate-take"))));
    m_model->activateTake();
    EXPECT_CALL(*m_dispatcher, dispatch(::testing::Matcher<const muse::actions::ActionQuery&>(IsQuery("action://record/auto-comp-takes"))));
    m_model->autoComp();
    EXPECT_CALL(*m_dispatcher, dispatch(::testing::Matcher<const muse::actions::ActionQuery&>(IsQuery("action://playback/toggle-play-stop"))));
    m_model->togglePlay();
}

TEST_F(ArtistModeModelTests, TakeCountFollowsSelectionAndRecording)
{
    ON_CALL(*m_selection, selectedTracks()).WillByDefault(Return(trackedit::TrackIdList { 3 }));
    int takes = 0;
    ON_CALL(*m_record, takeCount(_)).WillByDefault([&takes](const trackedit::TrackId&) { return takes; });
    m_model->init();
    EXPECT_EQ(m_model->takeLaneCount(), 0);

    int changes = 0;
    QObject::connect(m_model.get(), &ArtistModeModel::trackChanged, [&changes]() { ++changes; });
    //! recording over the track keeps the old audio as a take: count goes up when recording ends
    takes = 1;
    m_recordingFinished.notify();
    EXPECT_EQ(m_model->takeLaneCount(), 1);
    EXPECT_EQ(m_model->takeNumber(), 2);
    EXPECT_EQ(changes, 1);

    //! selecting another track shows its lanes
    ON_CALL(*m_selection, selectedTracks()).WillByDefault(Return(trackedit::TrackIdList { 4 }));
    takes = 4;
    m_tracksSelected.send({ 4 });
    EXPECT_EQ(m_model->takeLaneCount(), 4);
    EXPECT_EQ(changes, 2);
    //! no change, no signal
    m_tracksSelected.send({ 4 });
    EXPECT_EQ(changes, 2);
}

TEST_F(ArtistModeModelTests, LastTakeReportsDurationPeakAndClipping)
{
    m_model->init();
    EXPECT_FALSE(m_model->hasLastTake());

    m_recordedKeys = { trackedit::ClipKey { 1, 10 } };
    ON_CALL(*m_record, clipStats(_)).WillByDefault(Return(std::optional<TakeStats>(TakeStats { 12.34, -6.02, false, 0 })));
    m_recordingFinished.notify();
    ASSERT_TRUE(m_model->hasLastTake());
    EXPECT_FALSE(m_model->lastTakeClipped());
    EXPECT_EQ(m_model->lastTakeText(), "Last take: 12.3 s, peak -6.0 dBFS, no clipping");

    //! two tracks recorded at once: one clipped -> the take is reported as clipped
    m_recordedKeys = { trackedit::ClipKey { 1, 11 }, trackedit::ClipKey { 2, 12 } };
    EXPECT_CALL(*m_record, clipStats(trackedit::ClipKey { 1, 11 })).WillOnce(Return(std::optional<TakeStats>(TakeStats { 3.0, -3.0, false, 0 })));
    EXPECT_CALL(*m_record, clipStats(trackedit::ClipKey { 2, 12 })).WillOnce(Return(std::optional<TakeStats>(TakeStats { 3.5, 0.0, true, 17 })));
    m_recordingFinished.notify();
    EXPECT_TRUE(m_model->lastTakeClipped());
    EXPECT_EQ(m_model->lastTakeText(), "Last take: 3.5 s, peak 0.0 dBFS, CLIPPED (17 samples)");

    //! a cancelled recording (no clips) keeps the previous report
    m_recordedKeys.clear();
    m_recordingFinished.notify();
    EXPECT_TRUE(m_model->lastTakeClipped());
}

TEST_F(ArtistModeModelTests, InputMeterHoldsPeaksAndLatchesClipping)
{
    m_model->init();
    m_model->onMeterSignal(-12.0, 0.0);
    EXPECT_EQ(m_model->inputLevelDb(), -12.0);
    EXPECT_EQ(m_model->peakHoldDb(), -12.0);
    m_model->onMeterSignal(-20.0, 1.0);
    EXPECT_EQ(m_model->inputLevelDb(), -20.0);
    EXPECT_EQ(m_model->peakHoldDb(), -12.0); // held
    m_model->onMeterSignal(-25.0, 2.5);
    EXPECT_EQ(m_model->peakHoldDb(), -25.0); // hold expired
    EXPECT_FALSE(m_model->inputClipped());

    m_model->onMeterSignal(-0.05, 3.0);
    EXPECT_TRUE(m_model->inputClipped());
    m_model->onMeterSignal(-30.0, 3.1);
    EXPECT_TRUE(m_model->inputClipped()); // latched
    m_model->resetClipIndicator();
    EXPECT_FALSE(m_model->inputClipped());

    //! a new recording starts with a fresh clip lamp and peak hold
    m_model->onMeterSignal(0.0, 4.0);
    ON_CALL(*m_controller, isRecording()).WillByDefault(Return(true));
    m_isRecordingChanged.notify();
    EXPECT_FALSE(m_model->inputClipped());
    EXPECT_EQ(m_model->peakHoldDb(), -144.0);
}
