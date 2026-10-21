/*
 * Audacity: A Digital Audio Editor
 */
#include "au3-project-history/ProjectHistory.h"
#include "au3-project-history/UndoManager.h"
#include <cfloat>
#include <cmath>
#include <set>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "../internal/au3/au3record.h"
#include "au3wrap/internal/takelanes.h"
#include "au3wrap/internal/autocomp.h"

#include "au3-xml/XMLWriter.h"

#include "actions/tests/mocks/actionsdispatchermock.h"
#include "context/tests/mocks/globalcontextmock.h"
#include "context/tests/mocks/playbackstatemock.h"
#include "project/tests/mocks/audacityprojectmock.h"
#include "project/tests/testtools.h"
#include "trackedit/tests/mocks/selectioncontrollermock.h"
#include "trackedit/tests/mocks/trackeditprojectmock.h"
#include "trackedit/tests/mocks/projecthistorymock.h"

#include "au3audio/tests/mocks/audioenginemock.h"
#include "audio/tests/mocks/audiodrivercontrollermock.h"
#include "mocks/recordconfigurationmock.h"

#include "au3wrap/internal/au3project.h"
#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/au3types.h"

#include "au3-track/PendingTracks.h"
#include "au3-project-file-io/ProjectFileIO.h"
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
        m_projectHistory = std::make_shared<NiceMock<trackedit::ProjectHistoryMock> >();
        m_record->projectHistory.set(m_projectHistory);

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
    std::shared_ptr<trackedit::ProjectHistoryMock> m_projectHistory;

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
using au::au3::TakeLaneInfo;
namespace takelanes = au::au3::takelanes;
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

//! main (value 1) with take1 (value 2) and take2 (value 3), all over [2s, 5s]
struct ThreeLanes {
    Au3WaveTrack* main;
    Au3WaveTrack* take1;
    Au3WaveTrack* take2;
};

static void fillRegion(Au3WaveTrack& lane, Au3WaveTrack& source)
{
    auto audio = std::static_pointer_cast<Au3WaveTrack>(source.Copy(0.0, 3.0, false));
    lane.ClearAndPaste(2.0, 5.0, *audio);
}

TEST_F(Au3RecordTests, TakeLanesCycleRotatesTheLanes)
{
    Au3WaveTrack* main = addFilledTrack(0.0, 10.0, 1.0f);
    auto snapshot = takelanes::snapshotFrom(*main, 2.0);
    Au3WaveTrack* take1 = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);
    Au3WaveTrack* take2 = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);
    fillRegion(*take1, *addFilledTrack(0.0, 3.0, 2.0f));
    fillRegion(*take2, *addFilledTrack(0.0, 3.0, 3.0f));

    //! [WHEN] Next take: main plays take 1, take 1 gets take 2, take 2 gets the old main
    ASSERT_TRUE(takelanes::cycleTakes(projectRef(), *main, true));
    EXPECT_FLOAT_EQ(sampleAt(*main, 3.0), 2.0f);
    EXPECT_FLOAT_EQ(sampleAt(*take1, 3.0), 3.0f);
    EXPECT_FLOAT_EQ(sampleAt(*take2, 3.0), 1.0f);
    EXPECT_FLOAT_EQ(sampleAt(*main, 7.0), 1.0f);

    //! [WHEN] Previous take undoes the rotation
    ASSERT_TRUE(takelanes::cycleTakes(projectRef(), *main, false));
    EXPECT_FLOAT_EQ(sampleAt(*main, 3.0), 1.0f);
    EXPECT_FLOAT_EQ(sampleAt(*take1, 3.0), 2.0f);
    EXPECT_FLOAT_EQ(sampleAt(*take2, 3.0), 3.0f);
}

TEST_F(Au3RecordTests, TakeLanesFollowMainTrackName)
{
    Au3WaveTrack* main = addFilledTrack(0.0, 10.0, 1.0f);
    auto snapshot = takelanes::snapshotFrom(*main, 2.0);
    Au3WaveTrack* take = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);
    main->SetName("Lead");
    auto renamed = takelanes::renameTakes(projectRef(), *main);
    ASSERT_EQ(renamed.size(), 1u);
    EXPECT_EQ(take->GetName(), wxString("Lead - Take 1"));
    EXPECT_EQ(takelanes::mainOf(projectRef(), *take), main);
}

TEST_F(Au3RecordTests, SwitchTakeIsUndoableAndChecksTheTarget)
{
    Au3WaveTrack* main = addFilledTrack(0.0, 10.0, 1.0f);
    Au3WaveTrack* plain = addFilledTrack(0.0, 10.0, 5.0f);
    auto snapshot = takelanes::snapshotFrom(*main, 2.0);
    Au3WaveTrack* take = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);
    fillRegion(*take, *addFilledTrack(0.0, 3.0, 2.0f));

    //! [THEN] Only tracks with takes can switch; only a take lane can be activated
    EXPECT_FALSE(m_record->canSwitchTake(plain->GetId(), TakeSwitch::Next));
    EXPECT_TRUE(m_record->canSwitchTake(main->GetId(), TakeSwitch::Next));
    EXPECT_TRUE(m_record->canSwitchTake(take->GetId(), TakeSwitch::Previous));
    EXPECT_FALSE(m_record->canSwitchTake(main->GetId(), TakeSwitch::Activate));
    EXPECT_TRUE(m_record->canSwitchTake(take->GetId(), TakeSwitch::Activate));

    //! [WHEN] The take is activated from its lane
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).Times(1);
    EXPECT_TRUE(m_record->switchTake(take->GetId(), TakeSwitch::Activate));

    //! [THEN] The main track plays it and an undo step is recorded
    EXPECT_FLOAT_EQ(sampleAt(*main, 3.0), 2.0f);
    EXPECT_FLOAT_EQ(sampleAt(*take, 3.0), 1.0f);

    //! [AND] An invalid request does nothing
    EXPECT_FALSE(m_record->switchTake(plain->GetId(), TakeSwitch::Next));
}

TEST_F(Au3RecordTests, CompRangeSwapsOnlyTheRangeLosslesslyWithCrossfades)
{
    //! [GIVEN] Main track of 1.0, a take of 2.0 on [2s, 5s]
    Au3WaveTrack* main = addFilledTrack(0.0, 10.0, 1.0f);
    auto snapshot = takelanes::snapshotFrom(*main, 2.0);
    Au3WaveTrack* take = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);
    ASSERT_NE(take, nullptr);
    fillRegion(*take, *addFilledTrack(0.0, 3.0, 2.0f));

    //! [WHEN] [3s, 4s] of the take is comped with a 10 ms crossfade
    ASSERT_TRUE(takelanes::compRange(projectRef(), *main, *take, 3.0, 4.0, 0.010));

    //! [THEN] Only the range is replaced on the main track
    EXPECT_FLOAT_EQ(sampleAt(*main, 2.5), 1.0f);
    EXPECT_FLOAT_EQ(sampleAt(*main, 3.5), 2.0f);
    EXPECT_FLOAT_EQ(sampleAt(*main, 4.5), 1.0f);
    EXPECT_FLOAT_EQ(sampleAt(*main, 7.0), 1.0f);
    //! [AND] The take keeps the replaced main audio, bit-exact, and its own audio elsewhere
    EXPECT_FLOAT_EQ(sampleAt(*take, 2.5), 2.0f);
    EXPECT_FLOAT_EQ(sampleAt(*take, 3.0), 1.0f);
    EXPECT_FLOAT_EQ(sampleAt(*take, 3.5), 1.0f);
    EXPECT_FLOAT_EQ(sampleAt(*take, 3.9999), 1.0f);
    EXPECT_FLOAT_EQ(sampleAt(*take, 4.5), 2.0f);

    //! [AND] Linear crossfades of 441 samples at both boundaries
    const auto s0 = main->TimeToLongSamples(3.0);
    const auto s1 = main->TimeToLongSamples(4.0);
    auto at = [&](sampleCount s) { return sampleAt(*main, main->LongSamplesToTime(s)); };
    EXPECT_NEAR(at(s0), 1.0f, 0.01f);              // starts on the old audio
    EXPECT_NEAR(at(s0 + 220), 1.5f, 0.01f);        // halfway
    EXPECT_FLOAT_EQ(at(s0 + 441), 2.0f);           // fully the take after the fade
    EXPECT_NEAR(at(s1 - 1), 1.0f, 0.01f);          // back to the old audio at the end
    EXPECT_NEAR(at(s1 - 221), 1.5f, 0.01f);
    EXPECT_FLOAT_EQ(at(s1 - 442), 2.0f);

    //! [AND] The comped region is a clip named after its take
    int named = 0;
    for (const auto& clip : main->Intervals()) {
        if (clip->GetName() == wxString("Take 1")) {
            ++named;
            EXPECT_NEAR(clip->GetPlayStartTime(), 3.0, 1e-6);
            EXPECT_NEAR(clip->GetPlayEndTime(), 4.0, 1e-6);
        }
    }
    EXPECT_EQ(named, 1);

    //! [WHEN] The same range is comped back from the take
    ASSERT_TRUE(takelanes::compRange(projectRef(), *main, *take, 3.0, 4.0, 0.0));
    //! [THEN] The main track is restored exactly
    for (double t : { 2.5, 3.0, 3.001, 3.5, 3.999, 4.5 }) {
        EXPECT_FLOAT_EQ(sampleAt(*main, t), 1.0f) << t;
    }

    //! [AND] Invalid requests are refused
    EXPECT_FALSE(takelanes::compRange(projectRef(), *main, *take, 4.0, 4.0, 0.01));
    EXPECT_FALSE(takelanes::compRange(projectRef(), *main, *main, 3.0, 4.0, 0.01));
}

TEST_F(Au3RecordTests, CompRangeCrossfadeIsAtMostHalfTheRange)
{
    Au3WaveTrack* main = addFilledTrack(0.0, 10.0, 1.0f);
    auto snapshot = takelanes::snapshotFrom(*main, 2.0);
    Au3WaveTrack* take = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);
    fillRegion(*take, *addFilledTrack(0.0, 3.0, 2.0f));
    // 2 ms range, 10 ms crossfade: the fades meet in the middle and never overshoot
    ASSERT_TRUE(takelanes::compRange(projectRef(), *main, *take, 3.0, 3.002, 0.010));
    for (double t = 2.999; t < 3.003; t += 0.0001) {
        const float v = sampleAt(*main, t);
        EXPECT_GE(v, 1.0f - 1e-6f) << t;
        EXPECT_LE(v, 2.0f + 1e-6f) << t;
    }
    EXPECT_FLOAT_EQ(sampleAt(*main, 3.003), 1.0f);
}

TEST_F(Au3RecordTests, CompSelectionIsOneUndoStepAndSavedWithTheProject)
{
    Au3WaveTrack* main = addFilledTrack(0.0, 10.0, 1.0f);
    auto snapshot = takelanes::snapshotFrom(*main, 2.0);
    Au3WaveTrack* take = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);
    fillRegion(*take, *addFilledTrack(0.0, 3.0, 2.0f));

    //! [GIVEN] No time selection: nothing to comp
    ON_CALL(*m_selectionController, dataSelectedStartTime()).WillByDefault(Return(0.0));
    ON_CALL(*m_selectionController, dataSelectedEndTime()).WillByDefault(Return(0.0));
    EXPECT_FALSE(m_record->canSwitchTake(take->GetId(), TakeSwitch::CompSelection));

    //! [GIVEN] [3s, 4s] selected
    ON_CALL(*m_selectionController, dataSelectedStartTime()).WillByDefault(Return(3.0));
    ON_CALL(*m_selectionController, dataSelectedEndTime()).WillByDefault(Return(4.0));
    EXPECT_FALSE(m_record->canSwitchTake(main->GetId(), TakeSwitch::CompSelection));
    EXPECT_TRUE(m_record->canSwitchTake(take->GetId(), TakeSwitch::CompSelection));

    //! [WHEN] Comping from the take lane, with the real AU3 undo stack behind the history mock
    auto& history = ::ProjectHistory::Get(projectRef());
    history.InitialState();
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).Times(1).WillOnce([&history](auto, auto) {
        history.PushState(::TranslatableString::untranslatable(wxString("Comp")), ::TranslatableString::untranslatable(wxString("Comp")));
    });
    const auto mainId = main->GetId();
    const auto takeId = take->GetId();
    EXPECT_TRUE(m_record->switchTake(takeId, TakeSwitch::CompSelection));
    EXPECT_FLOAT_EQ(sampleAt(*main, 3.5), 2.0f);
    EXPECT_FLOAT_EQ(sampleAt(*take, 3.5), 1.0f);

    //! [THEN] The comp source is saved with the project as the clip name
    XMLStringWriter xml;
    main->WriteXML(xml);
    EXPECT_NE(xml.ToStdString().find("name=\"Take 1\""), std::string::npos);

    //! [WHEN] Undo
    ASSERT_TRUE(history.UndoAvailable());
    UndoManager::Get(projectRef()).Undo([&history](const UndoStackElem& elem) { history.PopState(elem.state); });

    //! [THEN] One step restores both lanes
    Au3WaveTrack* mainAfter = DomAccessor::findWaveTrack(projectRef(), mainId);
    Au3WaveTrack* takeAfter = DomAccessor::findWaveTrack(projectRef(), takeId);
    ASSERT_TRUE(mainAfter && takeAfter);
    EXPECT_FLOAT_EQ(sampleAt(*mainAfter, 3.5), 1.0f);
    EXPECT_FLOAT_EQ(sampleAt(*takeAfter, 3.5), 2.0f);
    EXPECT_FALSE(history.UndoAvailable());
}

namespace {
constexpr double AC_RATE = 8000.0;

//! 220 Hz "phrase" of `seconds`; kind 0 clean, 1 clipped (overdriven), 2 noisy, 3 very quiet, 4 dropout in the middle
std::vector<float> phrase(double seconds, int kind, unsigned seed = 1)
{
    std::vector<float> x(size_t(seconds * AC_RATE));
    unsigned state = seed * 2654435761u + 12345u;
    auto noise = [&state]() {
        state = state * 1664525u + 1013904223u;
        return float((state >> 8) & 0xFFFF) / 32768.0f - 1.0f;
    };
    for (size_t i = 0; i < x.size(); ++i) {
        float v = 0.4f * float(std::sin(2.0 * M_PI * 220.0 * double(i) / AC_RATE));
        switch (kind) {
        case 1: v = std::clamp(v * 4.0f, -1.0f, 1.0f);
            break;
        case 2: v += 0.15f * noise();
            break;
        case 3: v *= 0.003f;
            break;
        case 4: if (i > x.size() / 3 && i < 2 * x.size() / 3) {
                v = 0.0f;
        }
            break;
        default: v += 0.001f * noise();
        }
        x[i] = v;
    }
    return x;
}

std::vector<float> concat(std::initializer_list<std::vector<float> > parts)
{
    std::vector<float> out;
    for (const auto& p : parts) {
        out.insert(out.end(), p.begin(), p.end());
    }
    return out;
}

std::vector<float> gap(double seconds) { return std::vector<float>(size_t(seconds * AC_RATE), 0.0f); }
}

TEST(AutoCompTests, ScoresPenaliseClippingNoiseLowLevelAndDropouts)
{
    using namespace au::au3::autocomp;
    const Settings s;
    const auto clean = analyse(phrase(2.0, 0), 0, 16000, AC_RATE, s);
    const auto clipped = analyse(phrase(2.0, 1), 0, 16000, AC_RATE, s);
    const auto noisy = analyse(phrase(2.0, 2), 0, 16000, AC_RATE, s);
    const auto quiet = analyse(phrase(2.0, 3), 0, 16000, AC_RATE, s);
    const auto dropout = analyse(phrase(2.0, 4), 0, 16000, AC_RATE, s);

    EXPECT_NEAR(clean.clipFraction, 0.0, 1e-9);
    EXPECT_GT(clipped.clipFraction, 0.3);
    EXPECT_GT(noisy.noisiness, clean.noisiness + 0.15);
    EXPECT_GT(dropout.dropoutFraction, 0.25);
    EXPECT_LT(quiet.loudDb, s.lowLevelDb);

    EXPECT_GT(clean.score, clipped.score);
    EXPECT_GT(clean.score, noisy.score);
    EXPECT_GT(clean.score, quiet.score);
    EXPECT_GT(clean.score, dropout.score);
}

TEST(AutoCompTests, BoundariesSitInSharedPhraseGaps)
{
    using namespace au::au3::autocomp;
    Settings s;
    s.minSegmentSec = 1.5;
    // Phrases of 2 s separated by 0.3 s gaps at [2.0, 2.3] and [4.3, 4.6]
    const auto a = concat({ phrase(2.0, 0), gap(0.3), phrase(2.0, 0), gap(0.3), phrase(2.0, 0) });
    const auto b = concat({ phrase(2.0, 2), gap(0.3), phrase(2.0, 1), gap(0.3), phrase(2.0, 2) });
    const auto bounds = findBoundaries({ a, b }, AC_RATE, s);
    ASSERT_EQ(bounds.size(), 2u);
    EXPECT_GE(bounds[0] / AC_RATE, 2.0);
    EXPECT_LE(bounds[0] / AC_RATE, 2.3);
    EXPECT_GE(bounds[1] / AC_RATE, 4.3);
    EXPECT_LE(bounds[1] / AC_RATE, 4.6);

    //! No segment shorter than the minimum, even without gaps
    const auto flat = phrase(5.0, 0);
    const auto b2 = findBoundaries({ flat }, AC_RATE, s);
    size_t prev = 0;
    for (size_t x : b2) {
        EXPECT_GE(x - prev, size_t(1.5 * AC_RATE));
        prev = x;
    }
    EXPECT_GE(flat.size() - prev, size_t(1.5 * AC_RATE));
}

TEST(AutoCompTests, PicksTheCleanTakePerSegment)
{
    using namespace au::au3::autocomp;
    Settings s;
    s.minSegmentSec = 1.5;
    // Three phrases; the clean one is in a different lane each time
    const auto lane0 = concat({ phrase(2.0, 1), gap(0.3), phrase(2.0, 0, 2), gap(0.3), phrase(2.0, 2, 3) });
    const auto lane1 = concat({ phrase(2.0, 2, 4), gap(0.3), phrase(2.0, 1), gap(0.3), phrase(2.0, 0, 5) });
    const auto lane2 = concat({ phrase(2.0, 0, 6), gap(0.3), phrase(2.0, 2, 7), gap(0.3), phrase(2.0, 1) });
    const auto segs = compute({ lane0, lane1, lane2 }, AC_RATE, s);
    ASSERT_EQ(segs.size(), 3u);
    EXPECT_EQ(segs[0].lane, 2);
    EXPECT_EQ(segs[1].lane, 0);
    EXPECT_EQ(segs[2].lane, 1);
    EXPECT_EQ(segs.front().start, 0u);
    EXPECT_EQ(segs.back().end, lane0.size());
}

TEST(AutoCompTests, HysteresisKeepsThePreviousTakeForMarginalGains)
{
    using namespace au::au3::autocomp;
    Settings s;
    s.minSegmentSec = 1.5;
    // Two equally clean lanes (different noise seeds): the choice must not flip between segments
    const auto a = concat({ phrase(2.0, 0, 1), gap(0.3), phrase(2.0, 0, 2), gap(0.3), phrase(2.0, 0, 3) });
    const auto b = concat({ phrase(2.0, 0, 4), gap(0.3), phrase(2.0, 0, 5), gap(0.3), phrase(2.0, 0, 6) });
    const auto segs = compute({ a, b }, AC_RATE, s);
    ASSERT_EQ(segs.size(), 3u);
    EXPECT_EQ(segs[1].lane, segs[0].lane);
    EXPECT_EQ(segs[2].lane, segs[0].lane);

    //! With no hysteresis it is free to follow the raw scores
    s.hysteresis = 0.0;
    const auto raw = compute({ a, b }, AC_RATE, s);
    for (const auto& seg : raw) {
        EXPECT_GE(seg.metrics[seg.lane].score, seg.metrics[1 - seg.lane].score);
    }
}

TEST_F(Au3RecordTests, AutoCompAppliesChoicesAsOneUndoStep)
{
    //! [GIVEN] Main: noisy, take 1: clean in phrase 1, take 2: clean in phrase 2 (44.1 kHz project)
    auto makeTrack = [this](const std::vector<float>& data8k) {
        // upsample by repetition is fine for the heuristics; keep it simple: write at project rate
        std::vector<float> x(size_t(data8k.size() * TEST_SAMPLE_RATE / AC_RATE));
        for (size_t i = 0; i < x.size(); ++i) {
            x[i] = data8k[std::min(data8k.size() - 1, size_t(double(i) * AC_RATE / TEST_SAMPLE_RATE))];
        }
        auto* t = addSelectedMonoTrack();
        t->Append(0, reinterpret_cast<constSamplePtr>(x.data()), floatSample, x.size());
        t->Flush();
        return t;
    };
    Au3WaveTrack* main = makeTrack(concat({ phrase(2.0, 2, 1), gap(0.4), phrase(2.0, 2, 2) }));
    auto* t1src = makeTrack(concat({ phrase(2.0, 0, 3), gap(0.4), phrase(2.0, 1) }));
    auto* t2src = makeTrack(concat({ phrase(2.0, 1), gap(0.4), phrase(2.0, 0, 4) }));
    auto snapshot = takelanes::snapshotFrom(*main, 0.0);
    Au3WaveTrack* take1 = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 0.0, 4.4);
    Au3WaveTrack* take2 = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 0.0, 4.4);
    ASSERT_TRUE(take1 && take2);
    auto put = [](Au3WaveTrack& lane, Au3WaveTrack& src) {
        auto audio = std::static_pointer_cast<Au3WaveTrack>(src.Copy(0.0, 4.4, false));
        lane.ClearAndPaste(0.0, 4.4, *audio);
    };
    put(*take1, *t1src);
    put(*take2, *t2src);
    ON_CALL(*m_recordConfiguration, autoCompSegmentLength()).WillByDefault(Return(1.5));
    EXPECT_TRUE(m_record->canSwitchTake(main->GetId(), TakeSwitch::AutoComp));

    auto& history = ::ProjectHistory::Get(projectRef());
    history.InitialState();
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).Times(1).WillOnce([&history](auto, auto) {
        history.PushState(::TranslatableString::untranslatable(wxString("Auto")),
                          ::TranslatableString::untranslatable(wxString("Auto")));
    });
    const auto mainId = main->GetId();
    const float noisyAt1 = sampleAt(*main, 1.0);

    //! [WHEN] Auto-comp
    ASSERT_TRUE(m_record->switchTake(mainId, TakeSwitch::AutoComp));

    //! [THEN] Phrase 1 comes from take 1, phrase 2 from take 2, clips named after them
    EXPECT_FLOAT_EQ(sampleAt(*main, 1.0), sampleAt(*t1src, 1.0));
    EXPECT_FLOAT_EQ(sampleAt(*main, 3.4), sampleAt(*t2src, 3.4));
    std::set<wxString> names;
    for (const auto& clip : main->Intervals()) {
        names.insert(clip->GetName());
    }
    EXPECT_TRUE(names.count("Take 1"));
    EXPECT_TRUE(names.count("Take 2"));

    //! [AND] One undo restores the main track
    UndoManager::Get(projectRef()).Undo([&history](const UndoStackElem& elem) { history.PopState(elem.state); });
    Au3WaveTrack* restored = DomAccessor::findWaveTrack(projectRef(), mainId);
    ASSERT_NE(restored, nullptr);
    EXPECT_FLOAT_EQ(sampleAt(*restored, 1.0), noisyAt1);
    EXPECT_FALSE(history.UndoAvailable());
}

//! Not a test: writes /workspace/autocomp_demo.aup4 with visibly different takes for screenshots
TEST_F(Au3RecordTests, DISABLED_MakeAutoCompDemoProject)
{
    auto makeTrack = [this](const std::vector<float>& data8k) {
        std::vector<float> x(size_t(data8k.size() * TEST_SAMPLE_RATE / AC_RATE));
        for (size_t i = 0; i < x.size(); ++i) {
            x[i] = data8k[std::min(data8k.size() - 1, size_t(double(i) * AC_RATE / TEST_SAMPLE_RATE))];
        }
        auto* t = addSelectedMonoTrack();
        t->Append(0, reinterpret_cast<constSamplePtr>(x.data()), floatSample, x.size());
        t->Flush();
        return t;
    };
    // Main (current take): noisy / clipped / quiet; take 1 and take 2 are clean in different phrases
    Au3WaveTrack* main = makeTrack(concat({ phrase(2.0, 2, 1), gap(0.5), phrase(2.0, 1), gap(0.5), phrase(2.0, 3) }));
    main->SetName("Lead Vocal");
    auto* t1src = makeTrack(concat({ phrase(2.0, 0, 3), gap(0.5), phrase(2.0, 2, 4), gap(0.5), phrase(2.0, 0, 5) }));
    auto* t2src = makeTrack(concat({ phrase(2.0, 1), gap(0.5), phrase(2.0, 0, 6), gap(0.5), phrase(2.0, 4) }));
    auto snapshot = takelanes::snapshotFrom(*main, 0.0);
    Au3WaveTrack* take1 = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 0.0, 7.0);
    Au3WaveTrack* take2 = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 0.0, 7.0);
    for (auto [lane, src] : { std::pair { take1, t1src }, std::pair { take2, t2src } }) {
        auto audio = std::static_pointer_cast<Au3WaveTrack>(src->Copy(0.0, 7.0, false));
        lane->ClearAndPaste(0.0, 7.0, *audio);
    }
    takelanes::renameTakes(projectRef(), *main);
    TakeLaneInfo::Get(*main).lanesExpanded = true;
    auto& tracks = Au3TrackList::Get(projectRef());
    tracks.Remove(*t1src);
    tracks.Remove(*t2src);
    auto lastSaved = TrackList::Create(nullptr);
    ASSERT_TRUE(ProjectFileIO::Get(projectRef()).SaveProject(wxString(m_workingProjectPath), lastSaved.get()));
    m_au3ProjectAccessor->clearSavedState();
    m_au3ProjectAccessor->close();
    ASSERT_TRUE(testtools::copyFile(m_workingProjectPath, "/workspace/autocomp_demo.aup4"));
    m_au3ProjectAccessor.reset();
}
}
