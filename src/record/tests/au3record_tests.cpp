/*
 * Audacity: A Digital Audio Editor
 */
#include <cfloat>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "../internal/au3/au3record.h"
#include "../internal/au3/takelanes.h"

#include "au3-xml/XMLWriter.h"

#include "actions/tests/mocks/actionsdispatchermock.h"
#include "context/tests/mocks/globalcontextmock.h"
#include "context/tests/mocks/playbackstatemock.h"
#include "project/tests/mocks/audacityprojectmock.h"
#include "project/tests/testtools.h"
#include "trackedit/tests/mocks/selectioncontrollermock.h"
#include "trackedit/tests/mocks/trackeditprojectmock.h"

#include "au3audio/tests/mocks/audioenginemock.h"
#include "audio/tests/mocks/audiodrivercontrollermock.h"
#include "mocks/recordconfigurationmock.h"

#include "au3wrap/internal/au3project.h"
#include "au3wrap/au3types.h"

#include "au3-track/PendingTracks.h"
#include "au3-wave-track/WaveTrack.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::Truly;

using namespace au::au3;

namespace au::record {
constexpr static double TEST_SAMPLE_RATE = 44100.0;

class Au3RecordTests : public ::testing::Test
{
public:
    void SetUp() override
    {
        m_record = std::make_shared<Au3Record>(muse::modularity::globalCtx());

        m_globalContext = std::make_shared<NiceMock<context::GlobalContextMock> >();
        m_dispatcher = std::make_shared<NiceMock<muse::actions::ActionsDispatcherMock> >();
        m_selectionController = std::make_shared<NiceMock<trackedit::SelectionControllerMock> >();
        m_playbackState = std::make_shared<NiceMock<context::PlaybackStateMock> >();
        m_audioEngine = std::make_shared<NiceMock<audio::AudioEngineMock> >();
        m_recordConfiguration = std::make_shared<NiceMock<RecordConfigurationMock> >();
        m_audioDriverController = std::make_shared<NiceMock<audio::AudioDriverControllerMock> >();
        m_trackeditProject = std::make_shared<NiceMock<trackedit::TrackeditProjectMock> >();
        m_currentProject = std::make_shared<NiceMock<project::AudacityProjectMock> >();

        m_record->globalContext.set(m_globalContext);
        m_record->dispatcher.set(m_dispatcher);
        m_record->selectionController.set(m_selectionController);
        m_record->audioEngine.set(m_audioEngine);
        m_record->recordConfiguration.set(m_recordConfiguration);
        m_record->audioDriverController.set(m_audioDriverController);

        ON_CALL(*m_globalContext, currentProject())
        .WillByDefault(Return(m_currentProject));
        ON_CALL(*m_globalContext, playbackState())
        .WillByDefault(Return(m_playbackState));
        ON_CALL(*m_globalContext, currentTrackeditProject())
        .WillByDefault(Return(m_trackeditProject));

        ON_CALL(*m_audioEngine, isBusy())
        .WillByDefault(Invoke([this]() { return m_engineBusy; }));
        ON_CALL(*m_audioEngine, isCapturing())
        .WillByDefault(Return(false));
        ON_CALL(*m_audioEngine, stopStream())
        .WillByDefault(Invoke([this]() { m_engineBusy = false; }));
        ON_CALL(*m_audioEngine, recordingClipChanged())
        .WillByDefault(Return(m_recordingClipChanged));
        ON_CALL(*m_audioEngine, startStream(_, _, _, _, _, _))
        .WillByDefault(Return(1));

        ON_CALL(*m_audioDriverController, inputDevices())
        .WillByDefault(Return(std::vector<std::string> { "Test Input Device" }));
        ON_CALL(*m_audioDriverController, inputChannelsAvailable())
        .WillByDefault(Return(2));

        audio::AudioConfiguration audioConfiguration;
        audioConfiguration.inputChannels = 2;
        ON_CALL(*m_audioDriverController, configuration())
        .WillByDefault(Return(audioConfiguration));

        ON_CALL(*m_recordConfiguration, leadInTimeDuration())
        .WillByDefault(Return(2.0));
        ON_CALL(*m_recordConfiguration, crossfadeDuration())
        .WillByDefault(Return(0.0));
        ON_CALL(*m_recordConfiguration, punchOutAtSelectionEnd())
        .WillByDefault(Return(true));

        initTestProject();
    }

    void TearDown() override
    {
        // Guard against a fatal failure during setup leaving the project
        // uninitialized; projectRef() would otherwise dereference a null pointer
        if (m_au3ProjectAccessor && m_au3ProjectAccessor->au3ProjectPtr()) {
            PendingTracks::Get(projectRef()).ClearPendingTracks();
            Au3TrackList::Get(projectRef()).Clear();

            m_au3ProjectAccessor->clearSavedState();
            m_au3ProjectAccessor->close();
        }

        testtools::removeProjectIfExists(m_workingProjectPath);
    }

    void initTestProject()
    {
        m_au3ProjectAccessor = std::make_shared<Au3ProjectAccessor>(muse::modularity::globalCtx());

        // Load a working copy of the empty-project fixture shared with the trackedit tests
        const std::string source = std::string(record_tests_DATA_ROOT) + "/../../trackedit/tests/data/empty.aup4";
        m_workingProjectPath = std::string(record_tests_DATA_ROOT) + "/empty_working.aup4";
        testtools::removeProjectIfExists(m_workingProjectPath);
        ASSERT_TRUE(testtools::copyFile(source, m_workingProjectPath));

        constexpr auto discardAutosave = false;
        muse::Ret ret = m_au3ProjectAccessor->load(muse::io::path_t(m_workingProjectPath), discardAutosave);
        ASSERT_TRUE(ret);

        ON_CALL(*m_currentProject, au3ProjectPtr())
        .WillByDefault(Return(m_au3ProjectAccessor->au3ProjectPtr()));
    }

    Au3Project& projectRef() const
    {
        return *reinterpret_cast<Au3Project*>(m_au3ProjectAccessor->au3ProjectPtr());
    }

    Au3WaveTrack* addSelectedMonoTrack()
    {
        auto& trackFactory = Au3WaveTrackFactory::Get(projectRef());
        auto track = trackFactory.Create(sampleFormat::floatSample, TEST_SAMPLE_RATE);
        Au3TrackList::Get(projectRef()).Add(track, ::TrackList::DoAssignId::Yes,
                                            ::TrackList::EventPublicationSynchrony::Synchronous);
        track->SetSelected(true);
        return track.get();
    }

    //! Mono track holding `seconds` of constant `value` starting at `start`
    Au3WaveTrack* addFilledTrack(double start, double seconds, float value)
    {
        auto* track = addSelectedMonoTrack();
        std::vector<float> buf(size_t(seconds * TEST_SAMPLE_RATE), value);
        track->Append(0, reinterpret_cast<constSamplePtr>(buf.data()), floatSample, buf.size());
        track->Flush();
        if (start > 0.0) {
            track->MoveTo(start);
        }
        return track;
    }

    static float sampleAt(const Au3WaveTrack& track, double t)
    {
        float v = -100.0f;
        float* buffers[] { &v };
        track.GetFloats(0, 1, buffers, track.TimeToLongSamples(t), 1, false);
        return v;
    }

    std::shared_ptr<Au3Record> m_record;

    std::shared_ptr<context::GlobalContextMock> m_globalContext;
    std::shared_ptr<muse::actions::ActionsDispatcherMock> m_dispatcher;
    std::shared_ptr<trackedit::SelectionControllerMock> m_selectionController;
    std::shared_ptr<context::PlaybackStateMock> m_playbackState;
    std::shared_ptr<audio::AudioEngineMock> m_audioEngine;
    std::shared_ptr<RecordConfigurationMock> m_recordConfiguration;
    std::shared_ptr<audio::AudioDriverControllerMock> m_audioDriverController;
    std::shared_ptr<trackedit::TrackeditProjectMock> m_trackeditProject;
    std::shared_ptr<project::AudacityProjectMock> m_currentProject;

    std::shared_ptr<Au3ProjectAccessor> m_au3ProjectAccessor;
    std::string m_workingProjectPath;

    muse::async::Channel<Au3TrackId, Au3ClipId> m_recordingClipChanged;
    bool m_engineBusy = false;
};

TEST_F(Au3RecordTests, LeadInRecordingStartsAtPlayheadPosition)
{
    //! [GIVEN] A selected track, the playhead at 8s and a stale selection start at 5s
    //! (e.g. left behind by a previous recording)
    addSelectedMonoTrack();
    ON_CALL(*m_playbackState, playbackPosition())
    .WillByDefault(Return(muse::secs_t(8.0)));
    ON_CALL(*m_selectionController, selectionStartTime())
    .WillByDefault(Return(trackedit::secs_t(5.0)));

    double startedAt = -1.0;
    audio::IAudioEngine::StartStreamOptions streamOptions;
    EXPECT_CALL(*m_audioEngine, startStream(_, _, _, _, _, _))
    .WillOnce(DoAll(SaveArg<1>(&startedAt), SaveArg<5>(&streamOptions), Return(1)));

    //! [THEN] The playhead is repositioned to the start of the lead-in (8s - 2s pre-roll)
    EXPECT_CALL(*m_dispatcher,
                dispatch(testing::Matcher<const muse::actions::ActionQuery&>(
                             Truly([](const muse::actions::ActionQuery& q) {
        return q.param("seekTime").toDouble() == 6.0;
    }))));

    //! [WHEN] Lead-in recording is initiated
    muse::Ret ret = m_record->leadInRecording();
    EXPECT_TRUE(ret);

    //! [THEN] The stream starts recording at the playhead position, not at the
    //! stale selection start
    EXPECT_DOUBLE_EQ(startedAt, 8.0);
    EXPECT_DOUBLE_EQ(streamOptions.leadInTime, 2.0);
}

TEST_F(Au3RecordTests, LeadInRecordingPunchesOutAtSelectionEnd)
{
    //! [GIVEN] A selected track, the playhead at 8s and a time selection 7s..12s
    addSelectedMonoTrack();
    ON_CALL(*m_playbackState, playbackPosition()).WillByDefault(Return(muse::secs_t(8.0)));
    ON_CALL(*m_selectionController, dataSelectedStartTime()).WillByDefault(Return(trackedit::secs_t(7.0)));
    ON_CALL(*m_selectionController, dataSelectedEndTime()).WillByDefault(Return(trackedit::secs_t(12.0)));

    double startedAt = -1.0, endAt = -1.0;
    EXPECT_CALL(*m_audioEngine, startStream(_, _, _, _, _, _))
    .WillOnce(DoAll(SaveArg<1>(&startedAt), SaveArg<2>(&endAt), Return(1)));

    //! [WHEN] Lead-in recording is initiated
    EXPECT_TRUE(m_record->leadInRecording());

    //! [THEN] Recording punches in at the playhead and out at the selection end
    EXPECT_DOUBLE_EQ(startedAt, 8.0);
    EXPECT_DOUBLE_EQ(endAt, 12.0);
}

TEST_F(Au3RecordTests, LeadInRecordingWithoutSelectionRecordsUntilStopped)
{
    addSelectedMonoTrack();
    ON_CALL(*m_playbackState, playbackPosition()).WillByDefault(Return(muse::secs_t(8.0)));
    ON_CALL(*m_selectionController, dataSelectedStartTime()).WillByDefault(Return(trackedit::secs_t(0.0)));
    ON_CALL(*m_selectionController, dataSelectedEndTime()).WillByDefault(Return(trackedit::secs_t(0.0)));

    double endAt = -1.0;
    EXPECT_CALL(*m_audioEngine, startStream(_, _, _, _, _, _))
    .WillOnce(DoAll(SaveArg<2>(&endAt), Return(1)));

    EXPECT_TRUE(m_record->leadInRecording());
    EXPECT_DOUBLE_EQ(endAt, DBL_MAX);
}

TEST_F(Au3RecordTests, PunchOutIgnoresSelectionEndingBeforePunchIn)
{
    ON_CALL(*m_selectionController, dataSelectedStartTime()).WillByDefault(Return(trackedit::secs_t(2.0)));
    ON_CALL(*m_selectionController, dataSelectedEndTime()).WillByDefault(Return(trackedit::secs_t(5.0)));
    EXPECT_DOUBLE_EQ(m_record->punchOutTime(8.0), DBL_MAX);
}

TEST_F(Au3RecordTests, PunchOutDisabledByPreference)
{
    ON_CALL(*m_recordConfiguration, punchOutAtSelectionEnd()).WillByDefault(Return(false));
    ON_CALL(*m_selectionController, dataSelectedStartTime()).WillByDefault(Return(trackedit::secs_t(7.0)));
    ON_CALL(*m_selectionController, dataSelectedEndTime()).WillByDefault(Return(trackedit::secs_t(12.0)));
    EXPECT_DOUBLE_EQ(m_record->punchOutTime(8.0), DBL_MAX);
}

TEST_F(Au3RecordTests, LeadInRecordingAtZeroPlayheadFails)
{
    //! [GIVEN] A selected track and the playhead at 0s — there is nothing to pre-roll
    addSelectedMonoTrack();
    ON_CALL(*m_playbackState, playbackPosition())
    .WillByDefault(Return(muse::secs_t(0.0)));

    EXPECT_CALL(*m_audioEngine, startStream(_, _, _, _, _, _))
    .Times(0);

    //! [WHEN] Lead-in recording is initiated
    muse::Ret ret = m_record->leadInRecording();

    //! [THEN] It fails and no stream is started
    EXPECT_FALSE(ret);
}

TEST_F(Au3RecordTests, RecordingToNewTrackUsesConfiguredTrackName)
{
    //! [GIVEN] No recordable track exists and a custom recording track name
    //! with the track number suffix is configured
    RecordingTrackNameOptions nameOptions;
    nameOptions.useCustomName = true;
    nameOptions.customName = "MyTake";
    nameOptions.addTrackNumber = true;
    ON_CALL(*m_recordConfiguration, recordingTrackNameOptions())
    .WillByDefault(Return(nameOptions));

    EXPECT_CALL(*m_audioEngine, startStream(_, _, _, _, _, _))
    .WillOnce(Return(1));

    //! [WHEN] Recording is initiated
    muse::Ret ret = m_record->start();
    EXPECT_TRUE(ret);

    //! [THEN] Recording goes to a new track named after the configured scheme
    const auto tracks = Au3TrackList::Get(projectRef()).Any<Au3WaveTrack>();
    ASSERT_EQ(tracks.size(), 1u);
    EXPECT_EQ((*tracks.begin())->NChannels(), 2u);
    EXPECT_EQ((*tracks.begin())->GetName(), wxString("MyTake_1"));
}

TEST_F(Au3RecordTests, RecordingWithBusyEngineStartsUnpausedAtPlayhead)
{
    //! [GIVEN] A selected track, the playhead at 8s and the engine still holding
    //! a (possibly paused) playback stream
    addSelectedMonoTrack();
    m_engineBusy = true;
    ON_CALL(*m_playbackState, playbackPosition())
    .WillByDefault(Return(muse::secs_t(8.0)));

    double startedAt = -1.0;

    //! [THEN] The old stream is stopped, the engine pause flag (which survives
    //! stream restarts) is cleared, and only then recording starts
    InSequence seq;
    EXPECT_CALL(*m_audioEngine, stopStream());
    EXPECT_CALL(*m_audioEngine, pauseStream(false));
    EXPECT_CALL(*m_audioEngine, startStream(_, _, _, _, _, _))
    .WillOnce(DoAll(SaveArg<1>(&startedAt), Return(1)));

    //! [WHEN] Recording is initiated
    muse::Ret ret = m_record->start();
    EXPECT_TRUE(ret);

    //! [THEN] Recording starts at the playhead position
    EXPECT_DOUBLE_EQ(startedAt, 8.0);
}
}

namespace au::record {
TEST_F(Au3RecordTests, TakeLanesPreserveOverwrittenRegionAsMutedTake)
{
    //! [GIVEN] A track with 10s of audio (value 1) and a snapshot taken before recording at 2s
    Au3WaveTrack* main = addFilledTrack(0.0, 10.0, 1.0f);
    auto snapshot = takelanes::snapshotFrom(*main, 2.0);
    ASSERT_TRUE(snapshot);

    //! [WHEN] A recording over [2s, 5s] is committed and the overwritten part preserved
    Au3WaveTrack* take = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);

    //! [THEN] A muted take lane holds exactly the overwritten region
    ASSERT_NE(take, nullptr);
    EXPECT_TRUE(take->GetMute());
    EXPECT_NEAR(take->GetStartTime(), 2.0, 1e-6);
    EXPECT_NEAR(take->GetEndTime(), 5.0, 1e-6);
    EXPECT_FLOAT_EQ(sampleAt(*take, 3.0), 1.0f);
    EXPECT_EQ(TakeLaneInfo::Get(*take).takeIndex, 1);
    EXPECT_EQ(TakeLaneInfo::Get(*take).groupId, TakeLaneInfo::Get(*main).groupId);
    EXPECT_EQ(TakeLaneInfo::Get(*main).takeIndex, 0);

    auto takes = takelanes::takesOf(projectRef(), *main);
    ASSERT_EQ(takes.size(), 1u);
    EXPECT_EQ(takes[0], take);
}

TEST_F(Au3RecordTests, TakeLanesNumberSuccessivePasses)
{
    Au3WaveTrack* main = addFilledTrack(0.0, 10.0, 1.0f);
    auto snapshot = takelanes::snapshotFrom(*main, 2.0);
    takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);
    takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 4.0);

    auto takes = takelanes::takesOf(projectRef(), *main);
    ASSERT_EQ(takes.size(), 2u);
    EXPECT_EQ(TakeLaneInfo::Get(*takes[0]).takeIndex, 1);
    EXPECT_EQ(TakeLaneInfo::Get(*takes[1]).takeIndex, 2);
}

TEST_F(Au3RecordTests, TakeLanesNothingPreservedWhenRecordingPastTheEnd)
{
    //! [GIVEN] Audio only up to 3s; recording starts at 5s
    Au3WaveTrack* main = addFilledTrack(0.0, 3.0, 1.0f);
    EXPECT_FALSE(takelanes::snapshotFrom(*main, 5.0));
    EXPECT_TRUE(takelanes::takesOf(projectRef(), *main).empty());
}

TEST_F(Au3RecordTests, TakeLanesActivateTakeSwapsAudio)
{
    //! [GIVEN] The original audio (1) preserved as a take, and a new recording (2) on [2s, 5s]
    Au3WaveTrack* main = addFilledTrack(0.0, 10.0, 1.0f);
    auto snapshot = takelanes::snapshotFrom(*main, 2.0);
    Au3WaveTrack* take = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);
    ASSERT_NE(take, nullptr);

    Au3WaveTrack* newPass = addFilledTrack(0.0, 3.0, 2.0f);
    auto newAudio = std::static_pointer_cast<Au3WaveTrack>(newPass->Copy(0.0, 3.0, false));
    main->ClearAndPaste(2.0, 5.0, *newAudio);
    ASSERT_FLOAT_EQ(sampleAt(*main, 3.0), 2.0f);

    //! [WHEN] The take is activated
    EXPECT_TRUE(takelanes::activateTake(projectRef(), *main, *take));

    //! [THEN] The main track plays the take's audio and the take lane holds the new pass
    EXPECT_FLOAT_EQ(sampleAt(*main, 3.0), 1.0f);
    EXPECT_FLOAT_EQ(sampleAt(*take, 3.0), 2.0f);
    EXPECT_FLOAT_EQ(sampleAt(*main, 7.0), 1.0f); // outside the take region is untouched

    //! [AND] Activating a track that is not one of its takes is refused
    EXPECT_FALSE(takelanes::activateTake(projectRef(), *main, *newPass));
}

TEST_F(Au3RecordTests, TakeLaneInfoIsSavedWithTheTrackAndSurvivesCopies)
{
    Au3WaveTrack* main = addFilledTrack(0.0, 10.0, 1.0f);
    auto snapshot = takelanes::snapshotFrom(*main, 2.0);
    Au3WaveTrack* take = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);
    ASSERT_NE(take, nullptr);
    const std::string group = TakeLaneInfo::Get(*take).groupId;

    //! [THEN] The project XML of the take track carries its take-lane element
    XMLStringWriter xml;
    take->WriteXML(xml);
    const std::string written = xml.ToStdString();
    EXPECT_NE(written.find("<takelane group=\"" + group + "\" index=\"1\""), std::string::npos) << written;

    //! [AND] An ordinary track writes nothing extra
    Au3WaveTrack* plain = addSelectedMonoTrack();
    XMLStringWriter plainXml;
    plain->WriteXML(plainXml);
    EXPECT_EQ(plainXml.ToStdString().find("takelane"), std::string::npos);

    //! [AND] Reading the element back restores it (project load path)
    Au3WaveTrack* loaded = addSelectedMonoTrack();
    XMLTagHandler* handler = loaded->HandleXMLChild("takelane");
    ASSERT_NE(handler, nullptr);
    AttributesList attrs {
        { "group", XMLAttributeValueView(std::string_view(group)) },
        { "index", XMLAttributeValueView(3) },
    };
    EXPECT_TRUE(handler->HandleXMLTag("takelane", attrs));
    EXPECT_EQ(TakeLaneInfo::Get(*loaded).groupId, group);
    EXPECT_EQ(TakeLaneInfo::Get(*loaded).takeIndex, 3);

    //! [AND] Duplicating the track (as undo/redo does) keeps the membership
    auto copyHolder = std::static_pointer_cast<Au3WaveTrack>(take->Duplicate());
    auto* copy = copyHolder.get();
    EXPECT_EQ(TakeLaneInfo::Get(*copy).groupId, group);
    EXPECT_EQ(TakeLaneInfo::Get(*copy).takeIndex, 1);
}
}
