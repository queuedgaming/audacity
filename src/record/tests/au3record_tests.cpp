/*
 * Audacity: A Digital Audio Editor
 */
#include "au3-project-history/ProjectHistory.h"
#include "au3-project-history/UndoManager.h"
#include <cfloat>
#include <chrono>
#include <iostream>
#include <cmath>
#include <set>
#include <sstream>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "../internal/au3/au3record.h"
#include "au3wrap/internal/takelanes.h"
#include "au3wrap/internal/autocomp.h"
#include "au3wrap/internal/keydetect.h"
#include "au3wrap/internal/transients.h"
#include "au3-numeric-formats/ProjectTimeSignature.h"
#include "au3-label-track/LabelTrack.h"
#include "drumsynth.h"
#include "au3wrap/internal/projectkey.h"

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
#include "internal/au3/au3monitorchain.h"
#include "internal/recordconfiguration.h"
#include "au3-realtime-effects/RealtimeEffectList.h"
#include "au3-realtime-effects/RealtimeEffectState.h"
#include "effects/builtin_collection/denoise/denoiseeffects.h"
#include "effects/builtin_collection/dynamics/compressor/compressoreffect.h"


#include "au3wrap/internal/au3project.h"
#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/au3types.h"

#include "au3-track/PendingTracks.h"
#include "au3-project-file-io/ProjectFileIO.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-wave-track/WaveClip.h"
#include "au3-stretching-sequence/StretchingSequence.h"
#include "au3-xml/XMLAttributeValueView.h"

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

    //! Mono track holding `samples` from 0
    Au3WaveTrack* addSamplesTrack(const std::vector<float>& samples)
    {
        auto* track = addSelectedMonoTrack();
        track->Append(0, reinterpret_cast<constSamplePtr>(samples.data()), floatSample, samples.size());
        track->Flush();
        return track;
    }

    //! Stored samples (no fades)
    static std::vector<float> raw(const Au3WaveTrack& track, sampleCount start, size_t len)
    {
        std::vector<float> v(len);
        float* buffers[] { v.data() };
        track.GetFloats(0, 1, buffers, start, len, false, FillFormat::fillZero, false);
        return v;
    }

    //! What playback, export and mixdown hear: the same StretchingSequence they read
    static std::vector<float> rendered(const Au3WaveTrack& track, sampleCount start, size_t len)
    {
        std::vector<float> v(len);
        float* buffers[] { v.data() };
        auto seq = StretchingSequence::Create(track, track.GetClipInterfaces());
        seq->GetFloats(buffers, start, len, false);
        return v;
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
    //! the count the Artist mode dashboard shows, from the main track or any of its takes
    EXPECT_EQ(m_record->takeCount(main->GetId()), 2);
    EXPECT_EQ(m_record->takeCount(takes[1]->GetId()), 2);
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

//! Mono noise track (deterministic), `seconds` long from 0
static std::vector<float> noise(size_t n, unsigned seed)
{
    std::vector<float> x(n);
    unsigned state = seed * 2654435761u + 11u;
    for (auto& v : x) {
        state = state * 1664525u + 1013904223u;
        v = float(int((state >> 8) & 0xFFFF) - 32768) / 65536.0f;
    }
    return x;
}

TEST_F(Au3RecordTests, CompRangeMovesRawAudioAndSetsCrossfadesOnTheBoundaries)
{
    //! [GIVEN] Main track of 1.0, a take of 2.0 on [2s, 5s]
    Au3WaveTrack* main = addFilledTrack(0.0, 10.0, 1.0f);
    auto snapshot = takelanes::snapshotFrom(*main, 2.0);
    Au3WaveTrack* take = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);
    ASSERT_NE(take, nullptr);
    fillRegion(*take, *addFilledTrack(0.0, 3.0, 2.0f));

    //! [WHEN] [3s, 4s] of the take is comped with a 10 ms crossfade
    ASSERT_TRUE(takelanes::compRange(projectRef(), *main, *take, 3.0, 4.0, 0.010));

    //! [THEN] The stored samples are swapped exactly, nothing is faded into them
    for (double t : { 2.5, 3.0, 3.0001, 3.005, 3.5, 3.9999, 4.0, 4.005, 4.5, 7.0 }) {
        const bool inside = t >= 3.0 && t < 4.0;
        EXPECT_EQ(sampleAt(*main, t), inside ? 2.0f : 1.0f) << t;
        EXPECT_EQ(sampleAt(*take, t), t >= 5.0 ? 0.0f : inside ? 1.0f : 2.0f) << t;
    }

    //! [AND] The comp clip and the clip after it fade in over 441 samples; the take has no fades
    const double fade = 441.0 / TEST_SAMPLE_RATE;
    int named = 0;
    for (const auto& clip : main->Intervals()) {
        if (std::abs(clip->GetPlayStartTime() - 3.0) < 1e-6 || std::abs(clip->GetPlayStartTime() - 4.0) < 1e-6) {
            EXPECT_NEAR(clip->GetFadeInDuration(), fade, 1e-9);
        } else {
            EXPECT_EQ(clip->GetFadeInDuration(), 0.0);
        }
        EXPECT_EQ(clip->GetFadeOutDuration(), 0.0);
        if (clip->GetName() == wxString("Take 1")) {
            ++named;
            EXPECT_NEAR(clip->GetPlayStartTime(), 3.0, 1e-6);
            EXPECT_NEAR(clip->GetPlayEndTime(), 4.0, 1e-6);
        }
    }
    EXPECT_EQ(named, 1);
    for (const auto& clip : take->Intervals()) {
        EXPECT_EQ(clip->GetFadeInDuration(), 0.0);
        EXPECT_EQ(clip->GetFadeOutDuration(), 0.0);
    }

    //! [AND] Rendering (playback/export/mix path) crossfades with the clip handles, never dipping to silence
    const auto s0 = main->TimeToLongSamples(3.0);
    const auto s1 = main->TimeToLongSamples(4.0);
    const auto out = rendered(*main, s0 - 10, size_t((s1 - s0).as_long_long()) + 1000);
    auto at = [&](sampleCount s) { return out[size_t((s - (s0 - 10)).as_long_long())]; };
    EXPECT_FLOAT_EQ(at(s0 - 1), 1.0f);
    EXPECT_NEAR(at(s0), 1.0f, 0.01f);          // starts on the old main audio (left clip's handle)
    EXPECT_NEAR(at(s0 + 220), 1.5f, 0.01f);    // halfway
    EXPECT_FLOAT_EQ(at(s0 + 441), 2.0f);       // fully the take
    EXPECT_FLOAT_EQ(at(s1 - 1), 2.0f);         // take up to the boundary
    EXPECT_NEAR(at(s1), 2.0f, 0.01f);          // then the take's handle fades out under the main
    EXPECT_NEAR(at(s1 + 220), 1.5f, 0.01f);
    EXPECT_FLOAT_EQ(at(s1 + 441), 1.0f);
    for (float v : out) {
        EXPECT_GE(v, 1.0f - 1e-5f);
        EXPECT_LE(v, 2.0f + 1e-5f);
    }

    //! [AND] Invalid requests are refused
    EXPECT_FALSE(takelanes::compRange(projectRef(), *main, *take, 4.0, 4.0, 0.01));
    EXPECT_FALSE(takelanes::compRange(projectRef(), *main, *main, 3.0, 4.0, 0.01));
}

TEST_F(Au3RecordTests, CompRangeAndBackIsBitExactForMainAndTake)
{
    //! [GIVEN] Noise on the main track and different noise on a take over [2s, 5s]
    const size_t n = size_t(10 * TEST_SAMPLE_RATE);
    Au3WaveTrack* main = addSamplesTrack(noise(n, 1));
    auto snapshot = takelanes::snapshotFrom(*main, 2.0);
    Au3WaveTrack* take = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 2.0, 5.0);
    fillRegion(*take, *addSamplesTrack(noise(size_t(3 * TEST_SAMPLE_RATE), 2)));
    const auto mainRaw = raw(*main, 0, n);
    const auto takeRaw = raw(*take, main->TimeToLongSamples(2.0), size_t(3 * TEST_SAMPLE_RATE));
    const auto mainRendered = rendered(*main, 0, n);
    ASSERT_EQ(mainRendered, mainRaw);

    //! [WHEN] A range is comped (with crossfades) and then comped back
    ASSERT_TRUE(takelanes::compRange(projectRef(), *main, *take, 3.1234, 4.2, 0.010));
    EXPECT_NE(rendered(*main, 0, n), mainRaw);
    ASSERT_TRUE(takelanes::compRange(projectRef(), *main, *take, 3.1234, 4.2, 0.010));

    //! [THEN] Both lanes hold their original samples, bit for bit, and render them unchanged
    EXPECT_EQ(raw(*main, 0, n), mainRaw);
    EXPECT_EQ(raw(*take, main->TimeToLongSamples(2.0), size_t(3 * TEST_SAMPLE_RATE)), takeRaw);
    EXPECT_EQ(rendered(*main, 0, n), mainRaw);
    for (const auto& clip : main->Intervals()) {
        EXPECT_EQ(clip->GetFadeInDuration(), 0.0);
    }
}

TEST_F(Au3RecordTests, ClipFadesAreAppliedAtRenderTime)
{
    //! [GIVEN] A lone clip of 1.0 with a 100-sample linear fade-in and a 200-sample equal-power fade-out
    Au3WaveTrack* track = addFilledTrack(0.0, 1.0, 1.0f);
    auto clip = *track->Intervals().begin();
    clip->SetFadeInDuration(100.0 / TEST_SAMPLE_RATE);
    clip->SetFadeOutDuration(200.0 / TEST_SAMPLE_RATE);
    clip->SetFadeCurve(1);
    const size_t len = size_t(TEST_SAMPLE_RATE);

    //! [THEN] Stored samples are untouched
    EXPECT_EQ(sampleAt(*track, 0.0), 1.0f);

    //! [AND] Rendering applies the gain curves (fade to silence, no neighbour)
    const auto out = rendered(*track, 0, len);
    EXPECT_NEAR(out[0], std::sin(0.005 * M_PI / 2), 1e-5); // equal-power applies to both fades
    EXPECT_NEAR(out[49], std::sin(0.495 * M_PI / 2), 1e-4);
    EXPECT_FLOAT_EQ(out[100], 1.0f);
    EXPECT_FLOAT_EQ(out[len - 201], 1.0f);
    EXPECT_NEAR(out[len - 100], std::sin(99.5 / 200 * M_PI / 2), 1e-4);
    EXPECT_NEAR(out[len - 1], std::sin(0.5 / 200 * M_PI / 2), 1e-4);

    //! [AND] Rendering from the middle of a fade (playback started mid-clip) applies the same gain
    const auto mid = rendered(*track, 50, 10);
    EXPECT_FLOAT_EQ(mid[0], out[50]);
}

TEST_F(Au3RecordTests, ClipFadesAreSavedWithTheProjectAndUndoable)
{
    Au3WaveTrack* track = addFilledTrack(0.0, 2.0, 1.0f);
    auto clip = *track->Intervals().begin();
    const trackedit::ClipKey key { track->GetId(), clip->GetId() };
    const auto trackId = track->GetId();

    //! [WHEN] Fades are set through the record API with the real AU3 undo stack behind the history mock
    auto& history = ::ProjectHistory::Get(projectRef());
    history.InitialState();
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).Times(1).WillOnce([&history](auto, auto) {
        history.PushState(::TranslatableString::untranslatable(wxString("Fades")), ::TranslatableString::untranslatable(wxString("Fades")));
    });
    ASSERT_TRUE(m_record->setClipFades(key, 0.05, 0.2, 1));
    EXPECT_DOUBLE_EQ(clip->GetFadeInDuration(), 0.05);
    EXPECT_DOUBLE_EQ(clip->GetFadeOutDuration(), 0.2);
    EXPECT_EQ(clip->GetFadeCurve(), 1);
    //! [AND] Setting the same values again is not another undo step
    ASSERT_TRUE(m_record->setClipFades(key, 0.05, 0.2, 1));

    //! [THEN] They are written to the project and read back
    XMLStringWriter xml;
    track->WriteXML(xml);
    const std::string s = xml.ToStdString();
    EXPECT_NE(s.find("fadeIn=\"0.05000000\""), std::string::npos) << s.substr(0, 600);
    EXPECT_NE(s.find("fadeOut=\"0.20000000\""), std::string::npos);
    EXPECT_NE(s.find("fadeCurve=\"1\""), std::string::npos);
    auto restored = track->CopyClip(*clip, true); // copies (undo states, clipboard)
    EXPECT_DOUBLE_EQ(restored->GetFadeInDuration(), 0.05);
    EXPECT_DOUBLE_EQ(restored->GetFadeOutDuration(), 0.2);
    {
        auto fresh = track->CopyClip(*clip, true);
        fresh->SetFadeInDuration(0.0);
        fresh->SetFadeOutDuration(0.0);
        fresh->SetFadeCurve(0);
        AttributesList attrs;
        attrs.emplace_back("fadeIn", XMLAttributeValueView(std::string_view("0.25")));
        attrs.emplace_back("fadeOut", XMLAttributeValueView(std::string_view("0.5")));
        attrs.emplace_back("fadeCurve", XMLAttributeValueView(std::string_view("1")));
        EXPECT_TRUE(fresh->HandleXMLTag("waveclip", attrs));
        EXPECT_DOUBLE_EQ(fresh->GetFadeInDuration(), 0.25);
        EXPECT_DOUBLE_EQ(fresh->GetFadeOutDuration(), 0.5);
        EXPECT_EQ(fresh->GetFadeCurve(), 1);
    }

    //! [WHEN] Undo
    UndoManager::Get(projectRef()).Undo([&history](const UndoStackElem& elem) { history.PopState(elem.state); });
    //! [THEN] The fades are gone
    Au3WaveTrack* after = DomAccessor::findWaveTrack(projectRef(), trackId);
    ASSERT_TRUE(after);
    EXPECT_EQ((*after->Intervals().begin())->GetFadeInDuration(), 0.0);
    EXPECT_EQ((*after->Intervals().begin())->GetFadeOutDuration(), 0.0);
    EXPECT_FALSE(history.UndoAvailable());
}

TEST_F(Au3RecordTests, SetClipFadesClampsToTheClipWithoutOverlap)
{
    Au3WaveTrack* track = addFilledTrack(0.0, 2.0, 1.0f);
    auto clip = *track->Intervals().begin();
    const trackedit::ClipKey key { track->GetId(), clip->GetId() };
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).Times(2);
    ASSERT_TRUE(m_record->setClipFades(key, 5.0, 0.0, 0));
    EXPECT_DOUBLE_EQ(clip->GetFadeInDuration(), 2.0);
    ASSERT_TRUE(m_record->setClipFades(key, 1.5, 1.5, 0));
    EXPECT_DOUBLE_EQ(clip->GetFadeInDuration(), 1.5);
    EXPECT_DOUBLE_EQ(clip->GetFadeOutDuration(), 0.5);
    EXPECT_FALSE(m_record->setClipFades(key, -1.0, 0.0, 0));
}

TEST_F(Au3RecordTests, SplittingAClipKeepsFadesOnTheOuterEdges)
{
    Au3WaveTrack* track = addFilledTrack(0.0, 2.0, 1.0f);
    auto clip = *track->Intervals().begin();
    clip->SetFadeInDuration(0.1);
    clip->SetFadeOutDuration(0.2);
    track->SplitAt(1.0);
    ASSERT_EQ(track->NIntervals(), 2u);
    const auto sorted = track->SortedIntervalArray();
    EXPECT_DOUBLE_EQ(sorted[0]->GetFadeInDuration(), 0.1);
    EXPECT_EQ(sorted[0]->GetFadeOutDuration(), 0.0);
    EXPECT_EQ(sorted[1]->GetFadeInDuration(), 0.0);
    EXPECT_DOUBLE_EQ(sorted[1]->GetFadeOutDuration(), 0.2);
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

//! Sine at `hz` shifted by `cents`; motion 0 steady, 1 wobbly/jittery (random +-40 cent steps every 12 ms,
//! linearly interpolated), 2 vibrato (6 Hz, +-50 cents)
std::vector<float> tone(double seconds, double hz, double cents, int motion, float amp = 0.4f, double rate = AC_RATE,
                        unsigned seed = 1, float noiseAmp = 0.0f)
{
    std::vector<float> x(size_t(seconds * rate));
    unsigned state = seed * 2654435761u + 7u;
    auto rnd = [&state]() {
        state = state * 1664525u + 1013904223u;
        return double((state >> 8) & 0xFFFF) / 32768.0 - 1.0;
    };
    const size_t step = size_t(0.012 * rate);
    double prevOff = 0.0, nextOff = motion == 1 ? 40.0 * rnd() : 0.0, phase = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        double off = cents;
        if (motion == 1) {
            if (i % step == 0) {
                prevOff = nextOff;
                nextOff = 40.0 * rnd();
            }
            const double a = double(i % step) / double(step);
            off += prevOff + (nextOff - prevOff) * a;
        } else if (motion == 2) {
            off += 50.0 * std::sin(2.0 * M_PI * 6.0 * double(i) / rate);
        }
        phase += 2.0 * M_PI * hz * std::pow(2.0, off / 1200.0) / rate;
        x[i] = amp * float(std::sin(phase)) + noiseAmp * float(rnd());
    }
    return x;
}
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
    ON_CALL(*m_recordConfiguration, autoCompPitchScoring()).WillByDefault(Return(true));
    ON_CALL(*m_recordConfiguration, crossfadeDuration()).WillByDefault(Return(10.0));
    EXPECT_TRUE(m_record->canSwitchTake(main->GetId(), TakeSwitch::AutoComp));

    auto& history = ::ProjectHistory::Get(projectRef());
    history.InitialState();
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).Times(1).WillOnce([&history](auto, auto) {
        history.PushState(::TranslatableString::untranslatable(wxString("Auto")),
                          ::TranslatableString::untranslatable(wxString("Auto")));
    });
    const auto mainId = main->GetId();
    const float noisyAt1 = sampleAt(*main, 1.0);
    const size_t len = size_t(4.4 * TEST_SAMPLE_RATE);
    const auto lanesBefore = raw(*main, 0, len), take1Before = raw(*take1, 0, len), take2Before = raw(*take2, 0, len);

    //! [WHEN] Auto-comp
    ASSERT_TRUE(m_record->switchTake(mainId, TakeSwitch::AutoComp));

    //! [THEN] No sample was rewritten: every stored sample of every lane is one of the originals at that time
    {
        const auto m = raw(*main, 0, len), a = raw(*take1, 0, len), b = raw(*take2, 0, len);
        for (size_t i = 0; i < len; i += 7) {
            const std::set<float> orig { lanesBefore[i], take1Before[i], take2Before[i] };
            EXPECT_TRUE(orig.count(m[i]) && orig.count(a[i]) && orig.count(b[i])) << i;
        }
        int faded = 0;
        for (const auto& clip : main->Intervals()) {
            faded += clip->GetFadeInDuration() > 0 ? 1 : 0;
        }
        EXPECT_GT(faded, 0); // crossfades live on the clips instead
    }

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
    auto makeTrack = [this](const std::vector<float>& x) {
        auto* t = addSelectedMonoTrack();
        t->Append(0, reinterpret_cast<constSamplePtr>(x.data()), floatSample, x.size());
        t->Flush();
        return t;
    };
    // All clean (no noise or clipping): only pitch tells the takes apart. Levels differ so lanes look different.
    // Phrases: A3, B3, G3.  0 = in tune, 1 = 40 cents sharp, 2 = wobbly, 3 = in tune with vibrato
    auto take = [](float amp, int k1, int k2, int k3) {
        auto ph = [amp](double hz, int k) {
            return tone(2.0, hz, k == 1 ? 40.0 : 0.0, k == 2 ? 1 : k == 3 ? 2 : 0, amp, TEST_SAMPLE_RATE, 7 + k);
        };
        const auto g = std::vector<float>(size_t(0.5 * TEST_SAMPLE_RATE), 0.0f);
        return concat({ ph(220.0, k1), g, ph(246.94, k2), g, ph(196.0, k3) });
    };
    Au3WaveTrack* main = makeTrack(take(0.5f, 1, 2, 1));
    main->SetName("Lead Vocal");
    auto* t1src = makeTrack(take(0.35f, 0, 1, 3));
    auto* t2src = makeTrack(take(0.22f, 2, 0, 2));
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

//! Not a test: writes /workspace/fade_demo.aup4 (a comped track with crossfades and a faded clip) for screenshots
TEST_F(Au3RecordTests, DISABLED_MakeFadeDemoProject)
{
    auto makeTrack = [this](const std::vector<float>& x) {
        auto* t = addSelectedMonoTrack();
        t->Append(0, reinterpret_cast<constSamplePtr>(x.data()), floatSample, x.size());
        t->Flush();
        return t;
    };
    Au3WaveTrack* main = makeTrack(tone(6.0, 220.0, 0.0, 0, 0.6f, TEST_SAMPLE_RATE));
    main->SetName("Lead Vocal");
    auto* src = makeTrack(tone(6.0, 330.0, 0.0, 2, 0.3f, TEST_SAMPLE_RATE));
    auto snapshot = takelanes::snapshotFrom(*main, 0.0);
    Au3WaveTrack* take1 = takelanes::preserveAsTake(projectRef(), *main, *snapshot, 0.0, 6.0);
    auto audio = std::static_pointer_cast<Au3WaveTrack>(src->Copy(0.0, 6.0, false));
    take1->ClearAndPaste(0.0, 6.0, *audio);
    takelanes::renameTakes(projectRef(), *main);
    TakeLaneInfo::Get(*main).lanesExpanded = true;
    ASSERT_TRUE(takelanes::compRange(projectRef(), *main, *take1, 2.0, 4.0, 0.3));
    Au3WaveTrack* pad = makeTrack(tone(5.0, 165.0, 0.0, 0, 0.5f, TEST_SAMPLE_RATE));
    pad->SetName("Pad");
    auto clip = *pad->Intervals().begin();
    clip->SetFadeInDuration(0.8);
    clip->SetFadeOutDuration(1.5);
    clip->SetFadeCurve(1);
    Au3TrackList::Get(projectRef()).Remove(*src);
    auto lastSaved = TrackList::Create(nullptr);
    ASSERT_TRUE(ProjectFileIO::Get(projectRef()).SaveProject(wxString(m_workingProjectPath), lastSaved.get()));
    m_au3ProjectAccessor->clearSavedState();
    m_au3ProjectAccessor->close();
    ASSERT_TRUE(testtools::copyFile(m_workingProjectPath, "/workspace/fade_demo.aup4"));
    m_au3ProjectAccessor.reset();
}

TEST(PitchTests, YinTracksASineAndIgnoresSilence)
{
    using namespace au::au3::pitch;
    for (double rate : { 8000.0, 44100.0 }) {
        const auto x = concat({ tone(1.0, 220.0, 0.0, 0, 0.4f, rate), std::vector<float>(size_t(0.5 * rate), 0.0f) });
        const auto frames = yin(x, rate);
        ASSERT_GT(frames.size(), 100u);
        size_t voiced = 0;
        for (const auto& f : frames) {
            if (f.timeSec > 0.1 && f.timeSec < 0.9) {
                ASSERT_TRUE(f.voiced()) << f.timeSec;
                EXPECT_NEAR(f.f0, 220.0, 1.0);
                EXPECT_GT(f.confidence, 0.8);
            }
            if (f.timeSec > 1.1) {
                EXPECT_FALSE(f.voiced()) << f.timeSec;
            }
            voiced += f.voiced();
        }
        EXPECT_GT(voiced, 80u);
    }
}

namespace {
au::au3::autocomp::Settings oneSegment()
{
    au::au3::autocomp::Settings s;
    s.minSegmentSec = 10.0; // the whole input is one segment
    return s;
}
}

TEST(AutoCompPitchTests, InTuneBeatsFortyCentsSharp)
{
    using namespace au::au3::autocomp;
    const auto inTune = tone(3.0, 220.0, 0.0, 0);
    const auto sharp = tone(3.0, 220.0, 40.0, 0);
    const auto segs = compute({ sharp, inTune }, AC_RATE, oneSegment());
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].lane, 1);
    EXPECT_NEAR(segs[0].metrics[0].intonationCents, 40.0, 5.0);
    EXPECT_LT(segs[0].metrics[1].intonationCents, 5.0);
    EXPECT_GT(segs[0].metrics[0].pitchPenalty, 0.5);
}

TEST(AutoCompPitchTests, SteadyBeatsWobbly)
{
    using namespace au::au3::autocomp;
    const auto steady = tone(3.0, 220.0, 0.0, 0);
    const auto wobbly = tone(3.0, 220.0, 0.0, 1);
    const auto segs = compute({ wobbly, steady }, AC_RATE, oneSegment());
    ASSERT_EQ(segs.size(), 1u);
    EXPECT_EQ(segs[0].lane, 1);
    EXPECT_GT(segs[0].metrics[0].jitterCents, segs[0].metrics[1].jitterCents + 10.0);
}

TEST(AutoCompPitchTests, VibratoIsNotPenalised)
{
    using namespace au::au3::autocomp;
    const auto vibrato = tone(3.0, 220.0, 0.0, 2);
    const auto steady = tone(3.0, 220.0, 0.0, 0, 0.4f, AC_RATE, 3);
    const auto segs = compute({ vibrato, steady }, AC_RATE, oneSegment());
    ASSERT_EQ(segs.size(), 1u);
    const auto& m = segs[0].metrics[0];
    EXPECT_LT(m.jitterCents, oneSegment().jitterToleranceCents);
    EXPECT_LT(m.intonationCents, oneSegment().intonationToleranceCents);
    EXPECT_LT(m.pitchPenalty, 0.05);
}

TEST(AutoCompPitchTests, CleanButOffPitchLosesToInTune)
{
    using namespace au::au3::autocomp;
    const auto offPitch = tone(3.0, 220.0, 40.0, 0);
    const auto inTuneNoisy = tone(3.0, 220.0, 0.0, 0, 0.4f, AC_RATE, 5, 0.05f);
    auto s = oneSegment();
    //! Without pitch, the cleaner (off-pitch) take wins
    s.pitchEnabled = false;
    EXPECT_EQ(compute({ offPitch, inTuneNoisy }, AC_RATE, s)[0].lane, 0);
    //! With pitch, the in-tune take wins
    s.pitchEnabled = true;
    EXPECT_EQ(compute({ offPitch, inTuneNoisy }, AC_RATE, s)[0].lane, 1);
}

TEST(AutoCompPitchTests, MissedNotesArePenalised)
{
    using namespace au::au3::autocomp;
    const auto full = tone(3.0, 220.0, 0.0, 0);
    auto missing = tone(3.0, 220.0, 0.0, 0, 0.4f, AC_RATE, 2);
    std::fill(missing.begin() + missing.size() / 3, missing.begin() + 2 * missing.size() / 3, 0.0f);
    const auto full2 = tone(3.0, 220.0, 0.0, 0, 0.4f, AC_RATE, 3);
    const auto segs = compute({ missing, full, full2 }, AC_RATE, oneSegment());
    EXPECT_NEAR(segs[0].metrics[0].missedFraction, 1.0 / 3.0, 0.05);
    EXPECT_NE(segs[0].lane, 0);
}

TEST(AutoCompPitchTests, ThreeMinutesFourTakesPerformance)
{
    using namespace au::au3::autocomp;
    const double rate = 44100.0;
    std::vector<std::vector<float> > lanes;
    for (int l = 0; l < 4; ++l) {
        std::vector<float> lane;
        for (int p = 0; p < 36; ++p) { // 36 x (4.5 s phrase + 0.5 s gap) = 180 s
            const auto ph = tone(4.5, 196.0 * std::pow(2.0, (p % 5) / 12.0), (l == p % 4) ? 40.0 : 0.0, l == 1 ? 2 : 0,
                                 0.4f, rate, unsigned(l * 100 + p));
            lane.insert(lane.end(), ph.begin(), ph.end());
            lane.insert(lane.end(), size_t(0.5 * rate), 0.0f);
        }
        lanes.push_back(std::move(lane));
    }
    Settings s;
    const auto t0 = std::chrono::steady_clock::now();
    const auto segs = compute(lanes, rate, s);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    s.pitchEnabled = false;
    const auto t1 = std::chrono::steady_clock::now();
    (void)compute(lanes, rate, s);
    const double secsNoPitch = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    std::cout << "[ PERF     ] auto-comp 4 x 180 s @ 44.1 kHz: " << secs << " s with pitch, "
              << secsNoPitch << " s without, " << segs.size() << " segments" << std::endl;
    EXPECT_GT(segs.size(), 20u);
    EXPECT_LT(secs, 30.0);
}

TEST(AutoCompPitchTests, VibratoBeatsJitterEvenAfterAJitteryNeighbour)
{
    using namespace au::au3::autocomp;
    // 44.1 kHz, previous segment chose the jittery lane: the vibrato lane must still win the next one
    const double rate = 44100.0;
    const auto g = std::vector<float>(size_t(0.5 * rate), 0.0f);
    const auto a = concat({ tone(2.0, 220.0, 40.0, 0, 0.4f, rate), g, tone(2.0, 196.0, 0.0, 2, 0.4f, rate) });
    const auto b = concat({ tone(2.0, 220.0, 0.0, 1, 0.4f, rate, 3), g, tone(2.0, 196.0, 0.0, 1, 0.4f, rate, 4) });
    Settings s;
    s.minSegmentSec = 1.5;
    const auto segs = compute({ a, b }, rate, s);
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0].lane, 1);
    EXPECT_EQ(segs[1].lane, 0) << "jitter " << segs[1].metrics[1].jitterCents << " vs vibrato "
                               << segs[1].metrics[0].jitterCents;
}


// ---------------------------------------------------------------------------------------------
// Key / scale and tuning detection
namespace {
namespace kd = au::au3::keydetect;
constexpr double KD_RATE = 16000.0;

double midiHz(double midi, double tuningCents = 0.0)
{
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0 + tuningCents / 1200.0);
}

//! Notes (midi, seconds) with 3 decaying harmonics and short fades; midi < 0 is a rest
std::vector<float> melody(const std::vector<std::pair<double, double> >& notes, double tuningCents = 0.0,
                          double rate = KD_RATE, float amp = 0.3f)
{
    std::vector<float> x;
    for (const auto& [m, sec] : notes) {
        const size_t n = size_t(sec * rate);
        const size_t fade = size_t(0.01 * rate);
        for (size_t i = 0; i < n; ++i) {
            float v = 0.0f;
            if (m >= 0) {
                const double f = midiHz(m, tuningCents);
                const double t = double(i) / rate;
                v = amp * float(std::sin(2 * M_PI * f * t) + 0.4 * std::sin(4 * M_PI * f * t) + 0.2 * std::sin(6 * M_PI * f * t));
                const double g = std::min({ 1.0, double(i) / double(fade), double(n - i) / double(fade) });
                v *= float(g);
            }
            x.push_back(v);
        }
    }
    return x;
}

//! Block chords (each a list of midi notes) of `sec` each
std::vector<float> chords(const std::vector<std::vector<int> >& seq, double sec, double tuningCents = 0.0,
                          double rate = KD_RATE)
{
    std::vector<float> x;
    for (const auto& ch : seq) {
        const size_t n = size_t(sec * rate);
        for (size_t i = 0; i < n; ++i) {
            const double t = double(i) / rate;
            double v = 0.0;
            for (int m : ch) {
                const double f = midiHz(m, tuningCents);
                v += std::sin(2 * M_PI * f * t) + 0.3 * std::sin(4 * M_PI * f * t) + 0.15 * std::sin(6 * M_PI * f * t);
            }
            x.push_back(float(0.12 * v));
        }
    }
    return x;
}

//! A tonal melody in a major key (tonic midi `t`): tonic emphasis, scale run, arpeggios, cadence
std::vector<std::pair<double, double> > majorTune(int t)
{
    const int sc[] = { 0, 2, 4, 5, 7, 9, 11, 12 };
    std::vector<std::pair<double, double> > n { { t, 0.6 } };
    for (int d : sc) {
        n.push_back({ t + d, 0.2 });
    }
    for (int d : { 12, 7, 4, 0, 4, 7, 5, 2, 11 - 12, 0 }) {
        n.push_back({ t + d, 0.3 });
    }
    n.push_back({ t + 7, 0.4 });
    n.push_back({ t, 0.8 });
    return n;
}

//! Same for harmonic minor (raised 7th as leading tone)
std::vector<std::pair<double, double> > minorTune(int t)
{
    const int sc[] = { 0, 2, 3, 5, 7, 8, 11, 12 };
    std::vector<std::pair<double, double> > n { { t, 0.6 } };
    for (int d : sc) {
        n.push_back({ t + d, 0.2 });
    }
    for (int d : { 12, 7, 3, 0, 3, 7, 5, 2, 11 - 12, 0 }) {
        n.push_back({ t + d, 0.3 });
    }
    n.push_back({ t + 7, 0.4 });
    n.push_back({ t, 0.8 });
    return n;
}

std::string describe(const kd::Result& r)
{
    std::ostringstream ss;
    ss << r.keyName() << " r=" << r.key.correlation << " conf=" << r.confidence << " scale=" << r.scaleConfidence
       << " tuning=" << r.tuningCents << " pitchTrack=" << r.usedPitchTrack << " chroma:";
    for (double c : r.chroma) {
        ss << " " << int(std::lround(c * 1000));
    }
    ss << " alts:";
    for (const auto& a : r.alternatives) {
        ss << " " << kd::pitchClassName(a.tonic) << (a.minor ? "m" : "") << "(" << a.correlation << ")";
    }
    return ss.str();
}
}

TEST(KeyDetectTests, MajorMelodiesInSeveralKeys)
{
    for (int tonic : { 60, 62, 66, 58, 64 }) { // C, D, F#, Bb, E
        const auto r = kd::detect(melody(majorTune(tonic)), KD_RATE);
        ASSERT_TRUE(r.valid);
        EXPECT_TRUE(r.usedPitchTrack);
        EXPECT_EQ(r.key.tonic, tonic % 12) << describe(r);
        EXPECT_FALSE(r.key.minor) << describe(r);
        EXPECT_GT(r.scaleConfidence, 0.3) << describe(r);
    }
}

TEST(KeyDetectTests, MinorMelodiesInSeveralKeys)
{
    for (int tonic : { 57, 64, 60, 66, 55 }) { // A, E, C, F#, G minor
        const auto r = kd::detect(melody(minorTune(tonic)), KD_RATE);
        ASSERT_TRUE(r.valid);
        EXPECT_EQ(r.key.tonic, tonic % 12) << describe(r);
        EXPECT_TRUE(r.key.minor) << describe(r);
    }
}

TEST(KeyDetectTests, RelativeMajorAndMinorAreToldApart)
{
    //! [GIVEN] Two melodies on the same seven notes (no G#): one centred on C, one on A
    const std::vector<std::pair<double, double> > cMajor {
        { 60, 0.8 }, { 64, 0.3 }, { 67, 0.3 }, { 72, 0.6 }, { 67, 0.3 }, { 64, 0.3 }, { 65, 0.2 }, { 62, 0.3 },
        { 59, 0.3 }, { 60, 0.8 }, { 64, 0.3 }, { 62, 0.3 }, { 60, 1.0 } };
    const std::vector<std::pair<double, double> > aMinor {
        { 57, 0.8 }, { 60, 0.3 }, { 64, 0.3 }, { 69, 0.6 }, { 64, 0.3 }, { 60, 0.3 }, { 62, 0.2 }, { 59, 0.3 },
        { 64, 0.3 }, { 57, 0.8 }, { 60, 0.3 }, { 59, 0.3 }, { 57, 1.0 } };
    //! [WHEN] Detected
    const auto rc = kd::detect(melody(cMajor), KD_RATE);
    const auto ra = kd::detect(melody(aMinor), KD_RATE);
    //! [THEN] C major and A minor respectively; both share the note set, so the scale is certain either way
    EXPECT_EQ(rc.key.tonic, 0) << describe(rc);
    EXPECT_FALSE(rc.key.minor) << describe(rc);
    EXPECT_EQ(ra.key.tonic, 9) << describe(ra);
    EXPECT_TRUE(ra.key.minor) << describe(ra);
    EXPECT_GE(rc.scaleConfidence, rc.confidence);
    EXPECT_GE(ra.scaleConfidence, ra.confidence);
}

TEST(KeyDetectTests, ChordProgressionsUseTheSpectrum)
{
    //! [GIVEN] I-IV-V-I in G major and i-iv-V-i in D minor (with C#), three-note block chords
    const auto g = chords({ { 55, 59, 62 }, { 60, 64, 67 }, { 62, 66, 69 }, { 55, 59, 62 },
                            { 52, 55, 59 }, { 60, 64, 67 }, { 62, 66, 69, 72 }, { 55, 59, 62, 67 } }, 0.8);
    const auto d = chords({ { 50, 53, 57 }, { 55, 58, 62 }, { 57, 61, 64 }, { 50, 53, 57 },
                            { 58, 62, 65 }, { 55, 58, 62 }, { 57, 61, 64, 67 }, { 50, 53, 57, 62 } }, 0.8);
    const auto c = chords({ { 48, 52, 55 }, { 53, 57, 60 }, { 55, 59, 62 }, { 48, 52, 55 },
                            { 45, 48, 52 }, { 50, 53, 57 }, { 55, 59, 62, 65 }, { 48, 52, 55, 60 } }, 0.8);
    //! [WHEN] Detected automatically
    const auto rg = kd::detect(g, KD_RATE);
    const auto rd = kd::detect(d, KD_RATE);
    const auto rc = kd::detect(c, KD_RATE);
    //! [THEN] Polyphony is analysed spectrally and the keys are right
    EXPECT_FALSE(rg.usedPitchTrack);
    EXPECT_FALSE(rd.usedPitchTrack);
    EXPECT_EQ(rg.key.tonic, 7) << describe(rg);
    EXPECT_FALSE(rg.key.minor) << describe(rg);
    EXPECT_EQ(rd.key.tonic, 2) << describe(rd);
    EXPECT_TRUE(rd.key.minor) << describe(rd);
    EXPECT_EQ(rc.key.tonic, 0) << describe(rc);
    EXPECT_FALSE(rc.key.minor) << describe(rc);
    //! [AND] Temperley profiles agree
    kd::Settings t;
    t.profile = kd::Profile::Temperley;
    EXPECT_EQ(kd::detect(g, KD_RATE, t).key.tonic, 7);
    EXPECT_EQ(kd::detect(d, KD_RATE, t).key.tonic, 2);
}

TEST(KeyDetectTests, EstimatesTheTuningReference)
{
    const double a442 = 1200.0 * std::log2(442.0 / 440.0); // +7.85 cents
    //! [GIVEN] The same tune at A = 440, A = 442 and A = 435, sung (YIN) and as chords (spectrum)
    const auto r440 = kd::detect(melody(majorTune(57)), KD_RATE);
    const auto r442 = kd::detect(melody(majorTune(57), a442), KD_RATE);
    const auto r435 = kd::detect(melody(majorTune(57), 1200.0 * std::log2(435.0 / 440.0)), KD_RATE);
    const auto c442 = kd::detect(chords({ { 57, 61, 64 }, { 62, 66, 69 }, { 64, 68, 71 }, { 57, 61, 64 } }, 0.8, a442), KD_RATE);
    //! [THEN] Offsets within ~2 cents (~0.5 Hz) and the key is still found
    EXPECT_NEAR(r440.tuningCents, 0.0, 2.0) << describe(r440);
    EXPECT_NEAR(r442.tuningCents, a442, 2.0) << describe(r442);
    EXPECT_NEAR(r442.tuningHz, 442.0, 0.6);
    EXPECT_NEAR(r435.tuningHz, 435.0, 0.6) << describe(r435);
    EXPECT_NEAR(c442.tuningHz, 442.0, 0.6) << describe(c442);
    EXPECT_EQ(r442.key.tonic, 9);
    EXPECT_FALSE(r442.key.minor);
    //! [AND] The histogram stays at A440 when another offset is not clearly more common
    EXPECT_DOUBLE_EQ(kd::estimateTuning({ { 0.0, 1.0 }, { 40.0, 1.0 } }), 0.0);
}

TEST(KeyDetectTests, SnapsToScaleNotes)
{
    const unsigned cMajor = kd::scaleMask({ 0, false, 1.0 });
    // E4 = -500 cents from A4, Eb4 = -600
    EXPECT_DOUBLE_EQ(kd::snapCents(-555.0, 0xFFFu, 0.0), -600.0);  // chromatic: Eb
    EXPECT_DOUBLE_EQ(kd::snapCents(-555.0, cMajor, 0.0), -500.0);  // C major: E (Eb is not in the scale)
    EXPECT_DOUBLE_EQ(kd::snapCents(-497.0, cMajor, 8.0), -492.0);  // tuning offset moves the grid
    // A minor keeps the leading tone G#
    const unsigned aMinor = kd::scaleMask({ 9, true, 1.0 });
    EXPECT_TRUE(aMinor & (1u << 8));
    EXPECT_TRUE(aMinor & (1u << 7));
    EXPECT_FALSE(aMinor & (1u << 1));
}

TEST(KeyDetectTests, AtonalMaterialHasLowConfidence)
{
    //! [GIVEN] A chromatic scale: all twelve notes equally
    std::vector<std::pair<double, double> > n;
    for (int k = 0; k < 24; ++k) {
        n.push_back({ 60 + (k % 12), 0.25 });
    }
    const auto r = kd::detect(melody(n), KD_RATE);
    EXPECT_LT(r.confidence, 0.3) << describe(r);
    EXPECT_LT(r.scaleConfidence, 0.3) << describe(r);
    //! [AND] Silence is not a key
    EXPECT_FALSE(kd::detect(std::vector<float>(size_t(KD_RATE), 0.0f), KD_RATE).valid);
}

namespace {
//! Phrases of `notes` (midi) with 0.5 s gaps; `last` is the final phrase's sustained note (cents from A440 grid)
std::vector<float> keyContext(const std::vector<std::vector<int> >& phrases, double lastMidi, double lastCents)
{
    std::vector<std::pair<double, double> > n;
    for (const auto& ph : phrases) {
        for (int m : ph) {
            n.push_back({ m, 2.0 / double(ph.size()) });
        }
        n.push_back({ -1, 0.5 });
    }
    n.push_back({ lastMidi + lastCents / 100.0, 2.0 });
    return melody(n);
}
}

TEST(AutoCompKeyTests, InScaleTakeBeatsChromaticallyFineOutOfKeyTake)
{
    //! [GIVEN] Three phrases establishing C major, identical on both lanes, then a held note:
    //! main sings Eb4 10 cents flat (in tune chromatically, out of key), the take sings E4 in tune
    const std::vector<std::vector<int> > ctx { { 60, 62, 64, 65, 67, 65, 64, 62 }, { 60, 64, 67, 72, 67, 64, 60, 60 },
                                               { 65, 69, 67, 64, 62, 59, 60, 60 } };
    const std::vector<std::vector<float> > lanes { keyContext(ctx, 63, -10.0), keyContext(ctx, 64, 0.0) };
    auto s = oneSegment();
    s.minSegmentSec = 2.0;

    //! [WHEN] Auto-comp with scale snapping
    au::au3::autocomp::Analysis a;
    const auto segs = au::au3::autocomp::compute(lanes, KD_RATE, s, &a);

    //! [THEN] C major is detected and the in-key take wins the last phrase
    ASSERT_TRUE(a.key.valid);
    EXPECT_EQ(a.key.key.tonic, 0) << describe(a.key);
    EXPECT_TRUE(a.grid.scale) << describe(a.key);
    ASSERT_FALSE(segs.empty());
    EXPECT_EQ(segs.back().lane, 1) << "main " << segs.back().metrics[0].intonationCents << " take "
                                   << segs.back().metrics[1].intonationCents;
    for (size_t i = 0; i + 1 < segs.size(); ++i) {
        EXPECT_EQ(segs[i].lane, 0); // identical context phrases: no switching
    }

    //! [AND] On the chromatic A440 grid the consensus (-555 cents) snaps to Eb, so the out-of-key take is kept
    s.scaleSnapping = false;
    au::au3::autocomp::Analysis chromatic;
    const auto segsChromatic = au::au3::autocomp::compute(lanes, KD_RATE, s, &chromatic);
    EXPECT_FALSE(chromatic.grid.scale);
    EXPECT_EQ(segsChromatic.back().lane, 0);

    //! [AND] A known (project) key is used without detection
    s.scaleSnapping = true;
    s.key = kd::KeyCandidate { 0, false, 1.0 };
    au::au3::autocomp::Analysis known;
    EXPECT_EQ(au::au3::autocomp::compute(lanes, KD_RATE, s, &known).back().lane, 1);
    EXPECT_FALSE(known.key.valid);
    EXPECT_TRUE(known.grid.scale);
}

TEST(AutoCompKeyTests, LowKeyConfidenceFallsBackToChromatic)
{
    //! [GIVEN] Chromatic, key-less material on both lanes; the take sings an F# in tune, main 35 cents sharp
    const std::vector<std::vector<int> > ctx { { 60, 61, 62, 63, 64, 65, 66, 67 }, { 68, 69, 70, 71, 72, 71, 70, 69 },
                                               { 68, 67, 66, 65, 64, 63, 62, 61 } };
    const std::vector<std::vector<float> > lanes { keyContext(ctx, 66, 35.0), keyContext(ctx, 66, 0.0) };
    auto s = oneSegment();
    s.minSegmentSec = 2.0;
    au::au3::autocomp::Analysis a;
    const auto segs = au::au3::autocomp::compute(lanes, KD_RATE, s, &a);
    //! [THEN] No scale is imposed (chromatic notes are not pulled to a scale) and plain intonation still decides
    EXPECT_LT(a.key.scaleConfidence, s.keyMinConfidence) << describe(a.key);
    EXPECT_FALSE(a.grid.scale);
    EXPECT_EQ(segs.back().lane, 1);
    for (const auto& seg : segs) {
        EXPECT_LT(seg.metrics[1].intonationCents, 10.0);
    }
}

TEST_F(Au3RecordTests, DetectKeyStoresTheProjectKey)
{
    //! [GIVEN] A track with a tune in D minor at A = 442
    const auto x = melody(minorTune(62), 1200.0 * std::log2(442.0 / 440.0), TEST_SAMPLE_RATE);
    auto* t = addSelectedMonoTrack();
    t->Append(0, reinterpret_cast<constSamplePtr>(x.data()), floatSample, x.size());
    t->Flush();
    //! [WHEN] Detect key on the whole track
    const KeyDetection r = m_record->detectKey(t->GetId(), 0.0, 0.0, true);
    //! [THEN] Reported and stored as the project key
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.keyName, "D minor");
    EXPECT_NEAR(r.tuningHz, 442.0, 0.6);
    const auto& k = au::au3::ProjectKey::Get(projectRef()).key();
    ASSERT_TRUE(k.has_value());
    EXPECT_EQ(k->tonic, 2);
    EXPECT_TRUE(k->minor);
    //! [AND] Saved as project attributes
    XMLStringWriter w;
    w.StartTag(wxT("project"));
    ProjectFileIORegistry::Get().CallWriters(projectRef(), w);
    w.EndTag(wxT("project"));
    EXPECT_NE(w.Find(wxT("musical_key_tonic=\"2\"")), wxNOT_FOUND) << w.ToStdString();
    EXPECT_NE(w.Find(wxT("musical_key_minor=\"1\"")), wxNOT_FOUND);
}

//! Not a test: writes /workspace/key_demo.aup4 (a D minor tune at A = 442) for the Detect key screenshot
TEST_F(Au3RecordTests, DISABLED_MakeKeyDemoProject)
{
    const double a442 = 1200.0 * std::log2(442.0 / 440.0);
    auto notes = minorTune(62);
    const auto again = minorTune(62);
    notes.push_back({ -1, 0.5 });
    notes.insert(notes.end(), again.begin(), again.end());
    const auto x = melody(notes, a442, TEST_SAMPLE_RATE, 0.25f);
    auto* t = addSelectedMonoTrack();
    t->Append(0, reinterpret_cast<constSamplePtr>(x.data()), floatSample, x.size());
    t->Flush();
    t->SetName("Lead Vocal");
    auto lastSaved = TrackList::Create(nullptr);
    ASSERT_TRUE(ProjectFileIO::Get(projectRef()).SaveProject(wxString(m_workingProjectPath), lastSaved.get()));
    m_au3ProjectAccessor->clearSavedState();
    m_au3ProjectAccessor->close();
    ASSERT_TRUE(testtools::copyFile(m_workingProjectPath, "/workspace/key_demo.aup4"));
    m_au3ProjectAccessor.reset();
}

// ---------------------------------------------------------------------------------------------
// Project key: editing, undo, persistence
TEST(ProjectKeyTests, ShortLabels)
{
    using au::au3::ProjectKey;
    EXPECT_EQ(ProjectKey::shortLabel(std::nullopt), "No key");
    EXPECT_EQ(ProjectKey::shortLabel(ProjectKey::Key { 2, true, 1200.0 * std::log2(442.0 / 440.0), 1.0 }), "D min, A442");
    EXPECT_EQ(ProjectKey::shortLabel(ProjectKey::Key { 3, false, 0.0, 1.0 }), "Eb maj, A440");
    EXPECT_EQ(ProjectKey::shortLabel(ProjectKey::Key { 9, false, 1200.0 * std::log2(442.5 / 440.0), 1.0 }), "A maj, A442.5");
}

TEST_F(Au3RecordTests, ProjectKeyEditsAndClearsAreOneUndoStepEach)
{
    auto& history = ::ProjectHistory::Get(projectRef());
    history.InitialState();
    std::vector<std::string> pushed;
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).WillRepeatedly([&](const std::string& l, const std::string& s) {
        pushed.push_back(s);
        history.PushState(::TranslatableString::untranslatable(wxString(l)), ::TranslatableString::untranslatable(wxString(s)));
    });
    int notified = 0;
    muse::async::Asyncable listener;
    m_record->projectKeyChanged().onNotify(&listener, [&notified]() { ++notified; });
    const double a442 = 1200.0 * std::log2(442.0 / 440.0);

    //! [GIVEN] No key
    EXPECT_FALSE(m_record->projectKey().has_value());
    //! [WHEN] Set D minor at A442, set it again, edit to A major, then clear
    ASSERT_TRUE(m_record->setProjectKey(ProjectKeyInfo { 2, true, a442 }));
    ASSERT_TRUE(m_record->setProjectKey(ProjectKeyInfo { 2, true, a442 })); // unchanged: no history entry
    ASSERT_TRUE(m_record->setProjectKey(ProjectKeyInfo { 9, false, 0.0 }));
    ASSERT_TRUE(m_record->setProjectKey(std::nullopt));
    EXPECT_FALSE(m_record->setProjectKey(ProjectKeyInfo { 12, false, 0.0 })); // invalid root rejected
    //! [THEN] Three history entries, listeners told each time
    EXPECT_EQ(pushed.size(), 3u);
    EXPECT_FALSE(m_record->projectKey().has_value());
    EXPECT_EQ(notified, 3);
    EXPECT_EQ(m_record->projectKeyLabel(std::nullopt), "No key");

    //! [AND] Each undo goes back one edit
    auto undo = [&]() {
        UndoManager::Get(projectRef()).Undo([&history](const UndoStackElem& elem) { history.PopState(elem.state); });
    };
    undo();
    ASSERT_TRUE(m_record->projectKey().has_value());
    EXPECT_EQ(*m_record->projectKey(), (ProjectKeyInfo { 9, false, 0.0 }));
    undo();
    ASSERT_TRUE(m_record->projectKey().has_value());
    EXPECT_EQ(m_record->projectKey()->tonic, 2);
    EXPECT_TRUE(m_record->projectKey()->minor);
    undo();
    EXPECT_FALSE(m_record->projectKey().has_value());
    EXPECT_FALSE(history.UndoAvailable());
    //! [AND] Redo re-applies
    UndoManager::Get(projectRef()).Redo([&history](const UndoStackElem& elem) { history.PopState(elem.state); });
    ASSERT_TRUE(m_record->projectKey().has_value());
    EXPECT_EQ(m_record->projectKey()->tonic, 2);
    EXPECT_GE(notified, 6);
    m_record->projectKeyChanged().disconnect(&listener);
}

TEST_F(Au3RecordTests, ProjectKeySurvivesSaveCloseAndReopen)
{
    const double a435 = 1200.0 * std::log2(435.0 / 440.0);
    //! [GIVEN] F# minor at A = 435 stored in the project, which is saved and closed
    ASSERT_TRUE(m_record->setProjectKey(ProjectKeyInfo { 6, true, a435 }));
    auto lastSaved = TrackList::Create(nullptr);
    ASSERT_TRUE(ProjectFileIO::Get(projectRef()).SaveProject(wxString(m_workingProjectPath), lastSaved.get()));
    m_au3ProjectAccessor->clearSavedState();
    m_au3ProjectAccessor->close();
    m_au3ProjectAccessor.reset();

    //! [WHEN] Reopened
    m_au3ProjectAccessor = std::make_shared<Au3ProjectAccessor>(muse::modularity::globalCtx());
    ASSERT_TRUE(m_au3ProjectAccessor->load(muse::io::path_t(m_workingProjectPath), false));
    ON_CALL(*m_currentProject, au3ProjectPtr()).WillByDefault(Return(m_au3ProjectAccessor->au3ProjectPtr()));

    //! [THEN] The key and tuning are back
    const auto key = m_record->projectKey();
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key->tonic, 6);
    EXPECT_TRUE(key->minor);
    EXPECT_NEAR(key->tuningCents, a435, 1e-6);
    EXPECT_EQ(m_record->projectKeyLabel(key), "F# min, A435");

    //! [AND] Clearing and saving again leaves no key after another reopen
    ASSERT_TRUE(m_record->setProjectKey(std::nullopt));
    auto lastSaved2 = TrackList::Create(nullptr);
    ASSERT_TRUE(ProjectFileIO::Get(projectRef()).SaveProject(wxString(m_workingProjectPath), lastSaved2.get()));
    m_au3ProjectAccessor->clearSavedState();
    m_au3ProjectAccessor->close();
    m_au3ProjectAccessor.reset();
    m_au3ProjectAccessor = std::make_shared<Au3ProjectAccessor>(muse::modularity::globalCtx());
    ASSERT_TRUE(m_au3ProjectAccessor->load(muse::io::path_t(m_workingProjectPath), false));
    ON_CALL(*m_currentProject, au3ProjectPtr()).WillByDefault(Return(m_au3ProjectAccessor->au3ProjectPtr()));
    EXPECT_FALSE(m_record->projectKey().has_value());
}

TEST_F(Au3RecordTests, DetectKeyDoesNotStoreUnlessAsked)
{
    const auto x = melody(minorTune(62), 0.0, TEST_SAMPLE_RATE);
    auto* t = addSelectedMonoTrack();
    t->Append(0, reinterpret_cast<constSamplePtr>(x.data()), floatSample, x.size());
    t->Flush();
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).Times(0);
    const KeyDetection r = m_record->detectKey(t->GetId(), 0.0, 0.0, false);
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.keyName, "D minor");
    EXPECT_FALSE(m_record->projectKey().has_value());
}

TEST_F(Au3RecordTests, AutoCompReportsTheKeyItUsed)
{
    //! [GIVEN] Main and one take with a C major tune
    const std::vector<std::vector<int> > ctx { { 60, 62, 64, 65, 67, 65, 64, 62 }, { 60, 64, 67, 72, 67, 64, 60, 60 } };
    std::vector<std::pair<double, double> > notes;
    for (const auto& ph : ctx) {
        for (int m : ph) {
            notes.push_back({ m, 0.25 });
        }
        notes.push_back({ -1, 0.5 });
    }
    const auto x = melody(notes, 0.0, TEST_SAMPLE_RATE);
    auto* main = addSelectedMonoTrack();
    main->Append(0, reinterpret_cast<constSamplePtr>(x.data()), floatSample, x.size());
    main->Flush();
    const double len = main->GetEndTime();
    auto snapshot = takelanes::snapshotFrom(*main, 0.0);
    ASSERT_TRUE(takelanes::preserveAsTake(projectRef(), *main, *snapshot, 0.0, len));

    //! [WHEN] Auto-comp without, then with a project key
    std::string summary;
    takelanes::autoComp(projectRef(), *main, 2.0, 0.0, true, &summary);
    EXPECT_EQ(summary.rfind("detected C maj", 0), 0u) << summary;
    au::au3::ProjectKey::Get(projectRef()).setKey(au::au3::ProjectKey::Key { 7, false, 0.0, 1.0 });
    takelanes::autoComp(projectRef(), *main, 2.0, 0.0, true, &summary);
    //! [THEN] The stored key is used and reported
    EXPECT_EQ(summary, "project key G maj, A440");
}

namespace tr = au::au3::transients;

static void expectOnsetsMatchHits(const std::vector<tr::Onset>& on, const std::vector<double>& hits, const char* what)
{
    ASSERT_EQ(on.size(), hits.size()) << what;
    for (size_t i = 0; i < hits.size(); ++i) {
        EXPECT_NEAR(on[i].attack, hits[i], 0.005) << what << " hit " << i;
        // the slice point sits just before the attack (pre-roll), never after it
        EXPECT_LE(on[i].time, hits[i] + 0.001) << what << " hit " << i;
        EXPECT_GE(on[i].time, hits[i] - 0.005) << what << " hit " << i;
    }
}

TEST(TransientDetection, OnsetsOfASyntheticDrumLoopAreWithin5ms)
{
    for (double rate : { 44100.0, 48000.0 }) {
        for (double offset : { 0.3, 0.4137 }) {
            const auto loop = drumLoop(rate, 120.0, 2, offset);
            const auto on = tr::detect(loop.x.data(), loop.x.size(), rate);
            expectOnsetsMatchHits(on, loop.hits, ("rate " + std::to_string(int(rate)) + " offset " + std::to_string(offset)).c_str());
        }
    }
    //! a faster loop (eighths 115 ms apart) with more background noise
    const auto fast = drumLoop(44100.0, 130.0, 2, 0.25, 0.003f);
    expectOnsetsMatchHits(tr::detect(fast.x.data(), fast.x.size(), 44100.0), fast.hits, "fast");
}

TEST(TransientDetection, ClicksAreFoundAtTheirSample)
{
    const double rate = 44100.0;
    std::vector<float> x(size_t(2 * rate), 0.0f);
    const std::vector<double> at { 0.2, 0.7, 1.05, 1.5 };
    for (double t : at) {
        const size_t s = size_t(t * rate);
        for (size_t i = 0; i < 64; ++i) {
            x[s + i] = (i % 2 ? -0.7f : 0.7f) * float(1.0 - i / 64.0);
        }
    }
    const auto on = tr::detect(x.data(), x.size(), rate);
    expectOnsetsMatchHits(on, at, "clicks");
    //! and at a high rate (larger frames)
    const double hr = 96000.0;
    std::vector<float> y(size_t(2 * hr), 0.0f);
    for (double t : at) {
        const size_t s = size_t(t * hr);
        for (size_t i = 0; i < 128; ++i) {
            y[s + i] = (i % 2 ? -0.7f : 0.7f) * float(1.0 - i / 128.0);
        }
    }
    expectOnsetsMatchHits(tr::detect(y.data(), y.size(), hr), at, "clicks 96k");
}

TEST(TransientDetection, NoFalsePositivesOnASustainedTone)
{
    for (double rate : { 44100.0, 48000.0 }) {
        const auto x = sustainedTone(rate, 4.0, 0.5);
        for (double sens : { 20.0, 50.0, 80.0 }) {
            const auto on = tr::detect(x.data(), x.size(), rate, { sens });
            //! only the note start (if anything), never the vibrato, swell or the end of the buffer
            ASSERT_LE(on.size(), 1u) << "sensitivity " << sens;
            if (!on.empty()) {
                EXPECT_NEAR(on[0].attack, 0.5, 0.03);
            }
        }
    }
    //! silence and constant noise give nothing
    std::vector<float> silence(44100, 0.0f);
    EXPECT_TRUE(tr::detect(silence.data(), silence.size(), 44100.0).empty());
}

TEST(TransientDetection, SensitivityAndMinimumGap)
{
    const auto loop = drumLoop(44100.0, 120.0, 2, 0.3);
    const auto low = tr::detect(loop.x.data(), loop.x.size(), 44100.0, { 5.0 });
    const auto mid = tr::detect(loop.x.data(), loop.x.size(), 44100.0, { 50.0 });
    //! low sensitivity keeps the kicks and snares (on the beats) and drops the soft hats
    EXPECT_LT(low.size(), mid.size());
    //! every kick and snare (on the beats) is still there
    for (int b = 0; b < 8; ++b) {
        const double t = 0.3 + 0.5 * b;
        EXPECT_TRUE(std::any_of(low.begin(), low.end(), [t](const tr::Onset& o) { return std::abs(o.attack - t) < 0.005; })) << t;
    }
    //! a gap longer than the eighths merges them, keeping the stronger hit
    tr::Settings wide;
    wide.minGapSec = 0.4;
    const auto merged = tr::detect(loop.x.data(), loop.x.size(), 44100.0, wide);
    EXPECT_EQ(merged.size(), 8u);
}

TEST(TransientDetection, GridHelpers)
{
    EXPECT_DOUBLE_EQ(tr::gridStep(120, 4, 4, tr::Division::Beat), 0.5);
    EXPECT_DOUBLE_EQ(tr::gridStep(120, 4, 4, tr::Division::Bar), 2.0);
    EXPECT_DOUBLE_EQ(tr::gridStep(120, 4, 4, tr::Division::Sixteenth), 0.125);
    EXPECT_DOUBLE_EQ(tr::gridStep(120, 6, 8, tr::Division::Beat), 0.25);
    EXPECT_DOUBLE_EQ(tr::gridStep(120, 6, 8, tr::Division::Bar), 1.5);
    const auto lines = tr::gridLines(0.3, 2.0, 0.5);
    ASSERT_EQ(lines.size(), 3u); // strictly inside: 0.5 1.0 1.5
    EXPECT_DOUBLE_EQ(lines[0], 0.5);
    EXPECT_DOUBLE_EQ(lines[2], 1.5);
    EXPECT_DOUBLE_EQ(tr::quantize(0.31, 0.25, 1.0), 0.25);
    EXPECT_DOUBLE_EQ(tr::quantize(0.31, 0.25, 0.5), 0.28);
    EXPECT_DOUBLE_EQ(tr::quantize(0.31, 0.25, 0.0), 0.31);
}


TEST_F(Au3RecordTests, DetectTransientsMarksTheClipAndWritesLabels)
{
    const auto loop = drumLoop(TEST_SAMPLE_RATE, 120.0, 2, 0.3);
    Au3WaveTrack* track = addSamplesTrack(loop.x);
    auto clip = *track->Intervals().begin();
    const trackedit::ClipKey key { track->GetId(), clip->GetId() };
    auto& history = ::ProjectHistory::Get(projectRef());
    history.InitialState();
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).Times(1).WillOnce([&history](auto, auto) {
        history.PushState(::TranslatableString::untranslatable(wxString("T")), ::TranslatableString::untranslatable(wxString("T")));
    });
    ASSERT_TRUE(m_record->detectTransients(key, 50, true));

    //! [THEN] One marker per hit, shown in project time
    const auto shown = clip->GetVisibleTransientTimes();
    ASSERT_EQ(shown.size(), loop.hits.size());
    for (size_t i = 0; i < shown.size(); ++i) {
        EXPECT_NEAR(shown[i], loop.hits[i], 0.005);
    }
    //! [AND] markers follow the clip when it moves, and are saved
    clip->ShiftBy(1.0);
    EXPECT_NEAR(clip->GetVisibleTransientTimes()[0], loop.hits[0] + 1.0, 0.005);
    clip->ShiftBy(-1.0);
    XMLStringWriter xml;
    track->WriteXML(xml);
    EXPECT_NE(xml.ToStdString().find("transients=\""), std::string::npos);
    {
        auto fresh = track->CopyClip(*clip, true);
        EXPECT_EQ(fresh->GetTransients(), clip->GetTransients());
        fresh->SetTransients({});
        AttributesList attrs;
        attrs.emplace_back("transients", XMLAttributeValueView(std::string_view("0.25 1.5")));
        EXPECT_TRUE(fresh->HandleXMLTag("waveclip", attrs));
        EXPECT_EQ(fresh->GetTransients(), (std::vector<double> { 0.25, 1.5 }));
    }

    //! [AND] A label track "Transients" with numbered labels at the onsets
    const ::LabelTrack* lt = nullptr;
    for (auto* l : ::TrackList::Get(projectRef()).Any<const ::LabelTrack>()) {
        lt = l;
    }
    ASSERT_TRUE(lt);
    EXPECT_EQ(lt->GetName(), wxString("Transients"));
    ASSERT_EQ(lt->GetNumLabels(), int(loop.hits.size()));
    EXPECT_EQ(lt->GetLabel(0)->title, wxString("1"));
    EXPECT_NEAR(lt->GetLabel(3)->getT0(), shown[3], 1e-9);

    //! [WHEN] Undo: markers and label track are gone in one step
    UndoManager::Get(projectRef()).Undo([&history](const UndoStackElem& elem) { history.PopState(elem.state); });
    EXPECT_TRUE(::TrackList::Get(projectRef()).Any<const ::LabelTrack>().empty());
    Au3WaveTrack* after = DomAccessor::findWaveTrack(projectRef(), track->GetId());
    ASSERT_TRUE(after);
    EXPECT_TRUE((*after->Intervals().begin())->GetTransients().empty());
}

TEST_F(Au3RecordTests, SliceAtTransientsSplitsAtEveryHitWithCrossfades)
{
    const auto loop = drumLoop(TEST_SAMPLE_RATE, 120.0, 2, 0.3);
    Au3WaveTrack* track = addSamplesTrack(loop.x);
    auto clip = *track->Intervals().begin();
    const trackedit::ClipKey key { track->GetId(), clip->GetId() };
    const size_t total = loop.x.size();
    const auto before = rendered(*track, 0, total);

    auto& history = ::ProjectHistory::Get(projectRef());
    history.InitialState();
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).Times(1).WillOnce([&history](auto, auto) {
        history.PushState(::TranslatableString::untranslatable(wxString("S")), ::TranslatableString::untranslatable(wxString("S")));
    });
    //! [WHEN] Slicing a clip that has no markers yet (detects first) - one undo step
    ASSERT_TRUE(m_record->sliceAtTransients(key, 0.0, 2));

    //! [THEN] One piece before the first hit plus one per hit, contiguous, cut just before each hit
    const auto pieces = track->SortedIntervalArray();
    ASSERT_EQ(pieces.size(), loop.hits.size() + 1);
    for (size_t i = 1; i < pieces.size(); ++i) {
        EXPECT_NEAR(pieces[i]->GetPlayStartTime(), loop.hits[i - 1], 0.005);
        EXPECT_LE(pieces[i]->GetPlayStartTime(), loop.hits[i - 1] + 0.001);
        EXPECT_NEAR(pieces[i]->GetPlayStartTime(), pieces[i - 1]->GetPlayEndTime(), 1e-9);
        //! a short fade-in that crossfades with the neighbour's handle (same lineage)
        EXPECT_NEAR(pieces[i]->GetFadeInDuration(), 0.003, 1e-12);
        EXPECT_EQ(pieces[i]->GetFadeOutDuration(), 0.0);
    }
    EXPECT_EQ(pieces[0]->GetFadeInDuration(), 0.0);
    //! [AND] the sliced loop plays back the same (crossfade of identical material is transparent)
    const auto after = rendered(*track, 0, total);
    double maxErr = 0;
    for (size_t i = 0; i < total; ++i) {
        maxErr = std::max(maxErr, double(std::abs(after[i] - before[i])));
    }
    EXPECT_LT(maxErr, 1e-3);

    //! [WHEN] Undo -> the single clip is back
    UndoManager::Get(projectRef()).Undo([&history](const UndoStackElem& elem) { history.PopState(elem.state); });
    Au3WaveTrack* restored = DomAccessor::findWaveTrack(projectRef(), track->GetId());
    ASSERT_TRUE(restored);
    EXPECT_EQ(restored->NIntervals(), 1u);
}

TEST_F(Au3RecordTests, SliceToGridSplitsAtEveryBeatOfTheProjectTempo)
{
    auto& sig = ProjectTimeSignature::Get(projectRef());
    sig.SetTempo(120.0);
    sig.SetUpperTimeSignature(4);
    sig.SetLowerTimeSignature(4);
    const auto loop = drumLoop(TEST_SAMPLE_RATE, 120.0, 2, 0.3); // 4.8 s
    Au3WaveTrack* track = addSamplesTrack(loop.x);
    const trackedit::ClipKey key { track->GetId(), (*track->Intervals().begin())->GetId() };
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).Times(2);

    ASSERT_TRUE(m_record->sliceToGrid(key, 1));
    auto pieces = track->SortedIntervalArray();
    ASSERT_EQ(pieces.size(), 10u); // cuts at 0.5 .. 4.5
    for (size_t i = 1; i < pieces.size(); ++i) {
        EXPECT_NEAR(pieces[i]->GetPlayStartTime(), 0.5 * i, 1.0 / TEST_SAMPLE_RATE);
        EXPECT_NEAR(pieces[i]->GetFadeInDuration(), 0.003, 1e-12);
    }
    //! [AND] bars on one of the pieces made: 6/8 at 90 BPM -> bar 6 * (60/90 * 4/8) = 2 s
    sig.SetTempo(90.0);
    sig.SetUpperTimeSignature(6);
    sig.SetLowerTimeSignature(8);
    Au3WaveTrack* t2 = addSamplesTrack(std::vector<float>(size_t(5.0 * TEST_SAMPLE_RATE), 0.1f));
    ASSERT_TRUE(m_record->sliceToGrid({ t2->GetId(), (*t2->Intervals().begin())->GetId() }, 0));
    pieces = t2->SortedIntervalArray();
    ASSERT_EQ(pieces.size(), 3u);
    EXPECT_NEAR(pieces[1]->GetPlayStartTime(), 2.0, 1e-4);
    EXPECT_NEAR(pieces[2]->GetPlayStartTime(), 4.0, 1e-4);
}

TEST_F(Au3RecordTests, SliceAndQuantizeMovesSlicesToTheGrid)
{
    auto& sig = ProjectTimeSignature::Get(projectRef());
    sig.SetTempo(120.0);
    sig.SetUpperTimeSignature(4);
    sig.SetLowerTimeSignature(4);
    //! hits on eighths but 30 ms late against the grid, and with +-10 ms played jitter
    auto loop = drumLoop(TEST_SAMPLE_RATE, 120.0, 1, 0.53);
    Au3WaveTrack* track = addSamplesTrack(loop.x);
    const trackedit::ClipKey key { track->GetId(), (*track->Intervals().begin())->GetId() };
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).Times(1);
    ASSERT_TRUE(m_record->sliceAtTransients(key, 1.0, 2));

    const auto pieces = track->SortedIntervalArray();
    ASSERT_EQ(pieces.size(), loop.hits.size() + 1);
    for (size_t i = 1; i < pieces.size(); ++i) {
        const double s = pieces[i]->GetPlayStartTime();
        //! on a 1/8 line (0.25 s), the hit moved with it, and still contiguous with the previous piece
        EXPECT_NEAR(s, 0.5 + 0.25 * (i - 1), 1.0 / TEST_SAMPLE_RATE) << i;
        EXPECT_NEAR(pieces[i - 1]->GetPlayEndTime(), s, 1e-9) << i;
    }
    //! the kick that was at 0.53 now starts at ~0.5 (+pre-roll): silence before, energy just after
    const auto out = rendered(*track, track->TimeToLongSamples(0.49), size_t(0.03 * TEST_SAMPLE_RATE));
    double pre = 0, post = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        (i < size_t(0.009 * TEST_SAMPLE_RATE) ? pre : post) += out[i] * out[i];
    }
    EXPECT_GT(post, 100 * pre);

    //! half strength lands halfway
    Au3WaveTrack* t2 = addSamplesTrack(loop.x);
    EXPECT_CALL(*m_projectHistory, pushHistoryState(_, _)).Times(1);
    ASSERT_TRUE(m_record->sliceAtTransients({ t2->GetId(), (*t2->Intervals().begin())->GetId() }, 0.5, 2));
    const auto half = t2->SortedIntervalArray();
    ASSERT_EQ(half.size(), loop.hits.size() + 1);
    EXPECT_NEAR(half[1]->GetPlayStartTime(), 0.5 + 0.014, 0.003);
}

TEST_F(Au3RecordTests, ClipStatsReportDurationPeakAndClipping)
{
    //! [GIVEN] a 1 s take peaking at -6 dBFS
    std::vector<float> x(size_t(TEST_SAMPLE_RATE), 0.0f);
    for (size_t i = 0; i < x.size(); ++i) {
        x[i] = 0.5f * float(std::sin(2 * M_PI * 440.0 * i / TEST_SAMPLE_RATE));
    }
    Au3WaveTrack* track = addSamplesTrack(x);
    auto clip = *track->Intervals().begin();
    auto st = m_record->clipStats({ track->GetId(), clip->GetId() });
    ASSERT_TRUE(st);
    EXPECT_NEAR(st->duration, 1.0, 1e-6);
    EXPECT_NEAR(st->peakDb, -6.02, 0.05);
    EXPECT_FALSE(st->clipped);

    //! [GIVEN] the same take driven into full scale for 30 samples
    for (size_t i = 1000; i < 1030; ++i) {
        x[i] = i % 2 ? -1.0f : 1.0f;
    }
    Au3WaveTrack* hot = addSamplesTrack(x);
    st = m_record->clipStats({ hot->GetId(), (*hot->Intervals().begin())->GetId() });
    ASSERT_TRUE(st);
    EXPECT_TRUE(st->clipped);
    EXPECT_EQ(st->clippedSamples, 30u);
    EXPECT_NEAR(st->peakDb, 0.0, 1e-6);

    //! unknown clip
    EXPECT_FALSE(m_record->clipStats({ track->GetId(), 987654 }));
    //! a track without take lanes
    EXPECT_EQ(m_record->takeCount(track->GetId()), 0);
}
}

namespace au::record {
// ---- live monitoring with real built-in effects ------------------------------------------

namespace {
const PluginID kCompressorId = wxT("au-test:compressor");
const PluginID kNoiseId = wxT("au-test:noise-suppression");
const PluginID kBreathId = wxT("au-test:breath-removal");

const EffectInstanceFactory* builtinFactory(const PluginID& id)
{
    static au::effects::CompressorEffect compressor;
    static au::effects::NoiseSuppressionEffect noise;
    static au::effects::BreathRemovalEffect breath;
    if (id == kCompressorId) {
        return &compressor;
    }
    if (id == kNoiseId) {
        return &noise;
    }
    if (id == kBreathId) {
        return &breath;
    }
    return nullptr;
}

std::shared_ptr<RealtimeEffectState> addEffect(RealtimeEffectList& list, const PluginID& id)
{
    auto state = RealtimeEffectState::make_shared(id);
    EXPECT_TRUE(list.AddState(state));
    return state;
}

double rms(const std::vector<float>& v)
{
    double sum = 0;
    for (float x : v) {
        sum += double(x) * x;
    }
    return std::sqrt(sum / std::max<size_t>(1, v.size()));
}

//! quiet interleaved stereo sine (well under the compressor threshold)
std::vector<float> quietSine(size_t frames, double rate)
{
    std::vector<float> v(2 * frames);
    for (size_t i = 0; i < frames; ++i) {
        v[2 * i] = v[2 * i + 1] = 0.01f * float(std::sin(2 * M_PI * 440.0 * double(i) / rate));
    }
    return v;
}
}

TEST_F(Au3RecordTests, MonitorChainFromRealBuiltinEffects)
{
    RealtimeEffectState::EffectFactory::Scope scope { &builtinFactory };
    Au3WaveTrack* track = addSelectedMonoTrack();
    addEffect(RealtimeEffectList::Get(*track), kBreathId);
    addEffect(RealtimeEffectList::Get(*track), kCompressorId);
    addEffect(RealtimeEffectList::Get(projectRef()), kNoiseId);

    auto all = monitor::buildMonitorChain(projectRef(), track, TEST_SAMPLE_RATE, false, 0.020);
    // track stack first, in order, then master
    EXPECT_EQ(all.used, (std::vector<std::string> { "Breath removal", "Compressor", "Noise suppression" }));
    EXPECT_TRUE(all.bypassed.empty());
    ASSERT_EQ(all.chain->effects.size(), 3u);
    double sum = 0;
    for (const auto& e : all.chain->effects) {
        sum += e->latencySeconds();
    }
    EXPECT_NEAR(all.effectLatencySeconds, sum, 1e-9);
    const double breathLatency = all.chain->effects[0]->latencySeconds();
    EXPECT_GT(breathLatency, 0.1); // look-ahead breath detector (~230 ms)

    // RNNoise works on 10 ms frames and reports ~21 ms; use a threshold that keeps it
    const double noiseLatency = all.chain->effects[2]->latencySeconds();
    EXPECT_LT(noiseLatency, 0.030);
    auto low = monitor::buildMonitorChain(projectRef(), track, TEST_SAMPLE_RATE, true, 0.030);
    EXPECT_EQ(low.bypassed, (std::vector<std::string> { "Breath removal" }));
    EXPECT_EQ(low.used, (std::vector<std::string> { "Compressor", "Noise suppression" }));
    EXPECT_NEAR(low.effectLatencySeconds, all.effectLatencySeconds - breathLatency, 1e-9);

    // the default threshold (25 ms) keeps noise suppression (~21 ms) and bypasses breath removal
    auto byDefault = monitor::buildMonitorChain(projectRef(), track, TEST_SAMPLE_RATE, true,
                                                DEFAULT_LOW_LATENCY_THRESHOLD_MS / 1000.0);
    EXPECT_EQ(byDefault.bypassed, (std::vector<std::string> { "Breath removal" }));
    EXPECT_EQ(byDefault.used, (std::vector<std::string> { "Compressor", "Noise suppression" }));
    EXPECT_GT(noiseLatency, OLD_DEFAULT_LOW_LATENCY_THRESHOLD_MS / 1000.0); // why the old 20 ms default dropped it

    // the chain processes audio blocks of any size without touching the source effects
    monitor::MonitorProcessor p;
    p.setChain(std::move(low.chain));
    auto buf = quietSine(3000, TEST_SAMPLE_RATE);
    p.ProcessPlaythrough(buf.data(), 3000);
    for (float x : buf) {
        ASSERT_TRUE(std::isfinite(x));
    }
}

TEST_F(Au3RecordTests, MonitorPicksUpLiveSettingsEditsInPlace)
{
    RealtimeEffectState::EffectFactory::Scope scope { &builtinFactory };
    auto state = addEffect(RealtimeEffectList::Get(projectRef()), kCompressorId);
    auto built = monitor::buildMonitorChain(projectRef(), nullptr, TEST_SAMPLE_RATE, false, 0.020);
    ASSERT_EQ(built.used, (std::vector<std::string> { "Compressor" }));
    monitor::MonitorProcessor p;
    p.setChain(std::move(built.chain));

    auto runRms = [&]() {
        double last = 0;
        for (int b = 0; b < 20; ++b) { // long enough to flush the look-ahead
            auto buf = quietSine(512, TEST_SAMPLE_RATE);
            p.ProcessPlaythrough(buf.data(), 512);
            last = rms(buf);
        }
        return last;
    };
    const double before = runRms();

    state->GetAccess()->ModifySettings([](EffectSettings& settings) {
        settings.cast<CompressorSettings>()->makeupGainDb += 12.0;
        return nullptr;
    });
    p.syncSettings(); // what the 150 ms poll does when the stack did not change structurally
    const double after = runRms();
    EXPECT_EQ(p.chainSize(), 1u);
    EXPECT_NEAR(20.0 * std::log10(after / before), 12.0, 1.0);
}

TEST_F(Au3RecordTests, MonitorChainSignatureTracksStructuralChanges)
{
    RealtimeEffectState::EffectFactory::Scope scope { &builtinFactory };
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto& list = RealtimeEffectList::Get(*track);
    const auto empty = monitor::monitorChainSignature(projectRef(), track);

    auto a = addEffect(list, kCompressorId);
    const auto one = monitor::monitorChainSignature(projectRef(), track);
    EXPECT_NE(one, empty); // added

    auto b = addEffect(list, kNoiseId);
    const auto two = monitor::monitorChainSignature(projectRef(), track);
    EXPECT_NE(two, one);

    list.MoveEffect(1, 0);
    const auto moved = monitor::monitorChainSignature(projectRef(), track);
    EXPECT_NE(moved, two); // reordered

    a->SetActive(false);
    const auto toggled = monitor::monitorChainSignature(projectRef(), track);
    EXPECT_NE(toggled, moved); // switched off
    a->SetActive(true);
    EXPECT_EQ(monitor::monitorChainSignature(projectRef(), track), moved);

    list.RemoveState(b);
    EXPECT_EQ(monitor::monitorChainSignature(projectRef(), track), one); // removed

    EXPECT_NE(monitor::monitorChainSignature(projectRef(), nullptr), one); // other target
}

TEST_F(Au3RecordTests, PollMonitoringRebuildsOnStackChange)
{
    RealtimeEffectState::EffectFactory::Scope scope { &builtinFactory };
    ON_CALL(*m_recordConfiguration, isInputMonitoringOn()).WillByDefault(Return(true));
    ON_CALL(*m_recordConfiguration, monitorThroughEffects()).WillByDefault(Return(true));
    ON_CALL(*m_recordConfiguration, lowLatencyThresholdMs()).WillByDefault(Return(20.0));
    Au3WaveTrack* track = addSelectedMonoTrack();
    ON_CALL(*m_selectionController, selectedTracks())
    .WillByDefault(Return(trackedit::TrackIdList { track->GetId() }));
    m_record->initMonitoring();

    m_record->refreshMonitoring();
    EXPECT_TRUE(m_record->monitoringStatus().effects.empty());

    addEffect(RealtimeEffectList::Get(*track), kCompressorId);
    m_record->pollMonitoring();
    EXPECT_EQ(m_record->monitoringStatus().effects, (std::vector<std::string> { "Compressor" }));

    addEffect(RealtimeEffectList::Get(projectRef()), kNoiseId);
    m_record->pollMonitoring();
    EXPECT_EQ(m_record->monitoringStatus().effects, (std::vector<std::string> { "Compressor", "Noise suppression" }));
}
}

namespace au::record {
TEST(RecordConfigurationTests, LowLatencyThresholdDefaultAndMigration)
{
    EXPECT_DOUBLE_EQ(DEFAULT_LOW_LATENCY_THRESHOLD_MS, 25.0);
    EXPECT_DOUBLE_EQ(migrateLowLatencyThresholdMs(20.0), 25.0); // stored old default
    EXPECT_DOUBLE_EQ(migrateLowLatencyThresholdMs(25.0), 25.0);
    EXPECT_DOUBLE_EQ(migrateLowLatencyThresholdMs(15.0), 15.0); // user choices are kept
    EXPECT_DOUBLE_EQ(migrateLowLatencyThresholdMs(40.0), 40.0);
}
}

// ---- plugin parameter automation ---------------------------------------------------------
#include "au3wrap/internal/paramautomation.h"
#include "au3-xml/XMLFileReader.h"
#include "au3-import-export/ExportPluginHelpers.h"
#include "au3-mixer/Mix.h"
#include <atomic>
#include <new>
#include <thread>

namespace {
std::atomic<long> gCountedAllocs { 0 };
thread_local bool tCountAllocs = false;
}

void* operator new(std::size_t n)
{
    if (tCountAllocs) {
        gCountedAllocs.fetch_add(1);
    }
    if (void* p = std::malloc(n ? n : 1)) {
        return p;
    }
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace au::record {
using au::au3::ParamAutomationData;
using au::au3::ParamLane;
using au::au3::ParamPoint;
namespace pa = au::au3::paramautomation;

namespace {
ParamLane makeupLane(const PluginID& id, std::vector<ParamPoint> pts, bool step = false)
{
    ParamLane lane;
    lane.effectId = id.ToStdString();
    lane.paramKey = "makeupGainDb";
    lane.step = step;
    lane.points = std::move(pts);
    return lane;
}

std::vector<float> monoSine(size_t frames, double rate, float amp = 0.01f)
{
    std::vector<float> v(frames);
    for (size_t i = 0; i < frames; ++i) {
        v[i] = amp * float(std::sin(2 * M_PI * 440.0 * double(i) / rate));
    }
    return v;
}

double windowDb(const std::vector<float>& v, double rate, double t0, double t1)
{
    std::vector<float> w(v.begin() + size_t(t0 * rate), v.begin() + size_t(t1 * rate));
    return 20.0 * std::log10(rms(w));
}
}

TEST(ParamAutomation, EnvelopeEvaluation)
{
    const std::vector<ParamPoint> pts { { 1.0, 0.0 }, { 2.0, 10.0 }, { 4.0, -10.0 } };
    EXPECT_DOUBLE_EQ(pa::evaluate({}, false, 1.0, 7.0), 7.0); // empty -> fallback
    EXPECT_DOUBLE_EQ(pa::evaluate(pts, false, 0.0, 7.0), 0.0); // holds before first
    EXPECT_DOUBLE_EQ(pa::evaluate(pts, false, 1.5, 7.0), 5.0);
    EXPECT_DOUBLE_EQ(pa::evaluate(pts, false, 2.0, 7.0), 10.0);
    EXPECT_DOUBLE_EQ(pa::evaluate(pts, false, 3.0, 7.0), 0.0);
    EXPECT_DOUBLE_EQ(pa::evaluate(pts, false, 9.0, 7.0), -10.0); // holds after last
    EXPECT_DOUBLE_EQ(pa::evaluate(pts, true, 1.9, 7.0), 0.0); // step holds the left value
    EXPECT_DOUBLE_EQ(pa::evaluate(pts, true, 3.9, 7.0), 10.0);

    std::vector<ParamPoint> v;
    EXPECT_EQ(pa::insertPoint(v, { 2.0, 1.0 }), 0u);
    EXPECT_EQ(pa::insertPoint(v, { 1.0, 1.0 }), 0u);
    EXPECT_EQ(pa::insertPoint(v, { 3.0, 1.0 }), 2u);
    EXPECT_EQ(pa::insertPoint(v, { 2.0, 5.0 }), 1u); // same time replaces
    ASSERT_EQ(v.size(), 3u);
    EXPECT_DOUBLE_EQ(v[1].v, 5.0);
}

TEST_F(Au3RecordTests, ParamAutomationXmlRoundTrip)
{
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto& data = ParamAutomationData::Get(*track);
    data.lanes.push_back(makeupLane(kCompressorId, { { 0.0, -3.5 }, { 1.0 / 3.0, 12.25 } }, true));
    auto other = makeupLane(kCompressorId, { { 2.0, 1.0 } });
    other.occurrence = 1;
    other.paramKey = "thresholdDb";
    other.visible = false;
    data.lanes.push_back(other);
    data.shown = true;
    data.activeLane = 1;

    // saved inside the track element
    XMLStringWriter trackXml;
    track->WriteXML(trackXml);
    EXPECT_NE(trackXml.find(wxT("<paramlane")), wxString::npos);

    struct Wrap : XMLTagHandler {
        ParamAutomationData data;
        bool HandleXMLTag(const std::string_view&, const AttributesList&) override { return true; }
        XMLTagHandler* HandleXMLChild(const std::string_view& tag) override
        {
            return tag == ParamAutomationData::XMLTag() ? &data : nullptr;
        }
    } wrap;
    XMLStringWriter xml;
    xml.StartTag(wxT("wrap"));
    data.WriteXML(xml);
    xml.EndTag(wxT("wrap"));
    XMLFileReader reader;
    ASSERT_TRUE(reader.ParseString(&wrap, xml));
    const auto& back = wrap.data;
    ASSERT_EQ(back.lanes.size(), 2u);
    EXPECT_TRUE(back.shown);
    EXPECT_EQ(back.activeLane, 1);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_TRUE(back.lanes[i].sameTarget(data.lanes[i]));
        EXPECT_EQ(back.lanes[i].step, data.lanes[i].step);
        EXPECT_EQ(back.lanes[i].visible, data.lanes[i].visible);
        EXPECT_EQ(back.lanes[i].points, data.lanes[i].points); // exact (%.17g)
    }
}

TEST_F(Au3RecordTests, ParamAutomationUndoRestoresLanes)
{
    Au3WaveTrack* track = addSelectedMonoTrack();
    const auto trackId = track->GetId();
    auto& history = ::ProjectHistory::Get(projectRef());
    history.InitialState();

    ParamAutomationData::Get(*track).lanes.push_back(makeupLane(kCompressorId, { { 0.0, 0.0 } }));
    history.PushState(::TranslatableString::untranslatable(wxString("Add lane")),
                      ::TranslatableString::untranslatable(wxString("Add lane")));
    track = DomAccessor::findWaveTrack(projectRef(), trackId);
    pa::insertPoint(ParamAutomationData::Get(*track).lanes[0].points, { 1.0, 6.0 });
    history.PushState(::TranslatableString::untranslatable(wxString("Add point")),
                      ::TranslatableString::untranslatable(wxString("Add point")));

    auto undo = [&] {
        UndoManager::Get(projectRef()).Undo([&history](const UndoStackElem& elem) { history.PopState(elem.state); });
        return &ParamAutomationData::Get(*DomAccessor::findWaveTrack(projectRef(), trackId));
    };
    auto* d = undo();
    ASSERT_EQ(d->lanes.size(), 1u);
    EXPECT_EQ(d->lanes[0].points.size(), 1u);
    d = undo();
    EXPECT_TRUE(d->lanes.empty());
    UndoManager::Get(projectRef()).Redo([&history](const UndoStackElem& elem) { history.PopState(elem.state); });
    d = &ParamAutomationData::Get(*DomAccessor::findWaveTrack(projectRef(), trackId));
    EXPECT_EQ(d->lanes.size(), 1u);
}

TEST_F(Au3RecordTests, ParamAutomationListsCompressorParams)
{
    RealtimeEffectState::EffectFactory::Scope scope { &builtinFactory };
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto& list = RealtimeEffectList::Get(*track);
    auto a = addEffect(list, kCompressorId);
    auto b = addEffect(list, kCompressorId);
    EXPECT_EQ(pa::occurrenceAt(list, 0), 0);
    EXPECT_EQ(pa::occurrenceAt(list, 1), 1);
    const auto params = pa::listParams(*a);
    auto it = std::find_if(params.begin(), params.end(), [](const auto& p) { return p.key == "makeupGainDb"; });
    ASSERT_NE(it, params.end());
    EXPECT_DOUBLE_EQ(it->min, -30.0);
    EXPECT_DOUBLE_EQ(it->max, 30.0);
    EXPECT_FALSE(it->isInteger);
    EXPECT_GE(params.size(), 7u);
}

TEST_F(Au3RecordTests, ParamAutomationApplyIsAllocationFreeAndClamps)
{
    RealtimeEffectState::EffectFactory::Scope scope { &builtinFactory };
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto state = addEffect(RealtimeEffectList::Get(*track), kCompressorId);
    auto lane = makeupLane(kCompressorId, { { 0.0, 0.0 }, { 1.0, 100.0 } });
    auto lane2 = lane;
    lane2.paramKey = "thresholdDb";
    lane2.points = { { 0.0, -20.0 } };
    auto applier = pa::makeApplier(*state, { &lane, &lane2 });
    ASSERT_TRUE(applier);
    EffectSettings settings = state->GetSettings();
    gCountedAllocs = 0;
    tCountAllocs = true;
    for (int i = 0; i < 1000; ++i) {
        applier->Apply(settings, i / 1000.0, nullptr);
    }
    applier->Apply(settings, 0.1, nullptr);
    tCountAllocs = false;
    EXPECT_EQ(gCountedAllocs.load(), 0);
    EXPECT_NEAR(settings.cast<CompressorSettings>()->makeupGainDb, 10.0, 1e-9);
    EXPECT_NEAR(settings.cast<CompressorSettings>()->thresholdDb, -20.0, 1e-9);
    applier->Apply(settings, 0.9, nullptr);
    EXPECT_NEAR(settings.cast<CompressorSettings>()->makeupGainDb, 30.0, 1e-9); // clamped to max
}

TEST_F(Au3RecordTests, ParamAutomationPlaybackRampsCompressorMakeup)
{
    RealtimeEffectState::EffectFactory::Scope scope { &builtinFactory };
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto state = addEffect(RealtimeEffectList::Get(*track), kCompressorId);
    ParamAutomationData::Get(*track).lanes.push_back(makeupLane(kCompressorId, { { 0.0, 0.0 }, { 1.0, 24.0 } }));
    pa::install(projectRef());
    ASSERT_TRUE(state->GetAutomation());

    const double rate = TEST_SAMPLE_RATE;
    auto run = [&](bool withTime) {
        // the caller owns the instance while processing (as RealtimeEffectManager does)
        auto instance = state->Initialize(rate, 4096);
        EXPECT_TRUE(instance);
        EXPECT_TRUE(state->AddGroup(track, 1, float(rate), 4096));
        const size_t total = size_t(rate);
        auto in = monoSine(total, rate);
        std::vector<float> out(total), dummy(4096);
        for (size_t pos = 0; pos < total; pos += 1000) {
            const size_t n = std::min<size_t>(1000, total - pos);
            RealtimeEffectState::SetProcessTime(withTime ? pos / rate : -1.0, withTime ? rate : 0.0);
            const float* ip[] = { in.data() + pos };
            float* op[] = { out.data() + pos };
            EXPECT_TRUE(state->ProcessStart(true));
            state->Process(track, 1, ip, op, dummy.data(), n);
            state->ProcessEnd();
        }
        RealtimeEffectState::SetProcessTime(-1.0, 0.0);
        state->Finalize();
        return out;
    };
    const auto ramp = run(true);
    // expected makeup 24*t dB: 0.15 s -> 3.6, 0.85 s -> 20.4
    EXPECT_NEAR(windowDb(ramp, rate, 0.8, 0.9) - windowDb(ramp, rate, 0.1, 0.2), 16.8, 1.5);
    EXPECT_NEAR(windowDb(ramp, rate, 0.45, 0.55) - windowDb(ramp, rate, 0.1, 0.2), 8.4, 1.5);

    // without a process time (e.g. monitoring) automation is not applied: flat level
    const auto flat = run(false);
    EXPECT_NEAR(windowDb(flat, rate, 0.8, 0.9) - windowDb(flat, rate, 0.1, 0.2), 0.0, 0.5);
}

TEST_F(Au3RecordTests, ParamAutomationExportMixRampsCompressorMakeup)
{
    RealtimeEffectState::EffectFactory::Scope scope { &builtinFactory };
    Au3WaveTrack* track = addSelectedMonoTrack();
    const double rate = track->GetRate();
    auto tone = monoSine(size_t(2 * rate), rate);
    track->Append(0, reinterpret_cast<constSamplePtr>(tone.data()), floatSample, tone.size());
    track->Flush();
    addEffect(RealtimeEffectList::Get(*track), kCompressorId);
    ParamAutomationData::Get(*track).lanes.push_back(makeupLane(kCompressorId, { { 0.0, 0.0 }, { 2.0, 24.0 } }));

    auto render = [&]() {
        pa::install(projectRef());
        auto mixer = ExportPluginHelpers::CreateMixer(projectRef(), false, 0.0, 2.0, 1, 4096, true, rate,
                                                      floatSample, nullptr);
        std::vector<float> out;
        while (auto n = mixer->Process()) {
            auto p = reinterpret_cast<const float*>(mixer->GetBuffer());
            out.insert(out.end(), p, p + n);
        }
        return out;
    };
    const auto ramp = render();
    ASSERT_GE(ramp.size(), size_t(1.9 * rate));
    // 12 dB per second of makeup
    EXPECT_NEAR(windowDb(ramp, rate, 1.6, 1.8) - windowDb(ramp, rate, 0.2, 0.4), 16.8, 1.5);

    // removing the lane renders flat again
    ParamAutomationData::Get(*track).lanes.clear();
    const auto flat = render();
    EXPECT_NEAR(windowDb(flat, rate, 1.6, 1.8) - windowDb(flat, rate, 0.2, 0.4), 0.0, 0.5);
}

namespace {
//! Applier that detects use after destruction
class CheckedAutomation final : public RealtimeEffectAutomation
{
public:
    explicit CheckedAutomation(std::atomic<long>& bad)
        : mBad(bad) {}
    ~CheckedAutomation() override { mAlive.store(0); }
    void Apply(EffectSettings&, double, EffectInstance*) const noexcept override
    {
        if (mAlive.load() != kAlive) {
            mBad.fetch_add(1);
        }
        mApplied.fetch_add(1);
    }

    static constexpr int kAlive = 0x5a5a;
    std::atomic<int> mAlive { kAlive };
    std::atomic<long>& mBad;
    static inline std::atomic<long> mApplied { 0 };
};
}

TEST_F(Au3RecordTests, ParamAutomationSwapWhileProcessingIsSafe)
{
    RealtimeEffectState::EffectFactory::Scope scope { &builtinFactory };
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto state = addEffect(RealtimeEffectList::Get(*track), kCompressorId);
    const double rate = TEST_SAMPLE_RATE;
    auto instance = state->Initialize(rate, 4096);
    ASSERT_TRUE(instance);
    ASSERT_TRUE(state->AddGroup(track, 1, float(rate), 4096));

    std::atomic<long> bad { 0 };
    std::atomic<bool> stop { false };
    CheckedAutomation::mApplied = 0;
    state->SetAutomation(std::make_shared<CheckedAutomation>(bad));
    std::thread audio([&] {
        auto in = monoSine(1024, rate);
        std::vector<float> out(1024), dummy(1024);
        double t = 0;
        while (!stop.load()) {
            RealtimeEffectState::SetProcessTime(t, rate);
            const float* ip[] = { in.data() };
            float* op[] = { out.data() };
            state->ProcessStart(true);
            state->Process(track, 1, ip, op, dummy.data(), 1024);
            state->ProcessEnd();
            t += 1024 / rate;
        }
    });
    for (int i = 0; i < 2000; ++i) {
        state->SetAutomation(i % 7 == 6 ? nullptr : std::make_shared<CheckedAutomation>(bad));
    }
    // let the audio thread run with the final object a little
    state->SetAutomation(std::make_shared<CheckedAutomation>(bad));
    const auto before = CheckedAutomation::mApplied.load();
    for (int spin = 0; spin < 2000 && CheckedAutomation::mApplied.load() < before + 8; ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stop = true;
    audio.join();
    state->SetAutomation(nullptr);
    state->Finalize();
    EXPECT_EQ(bad.load(), 0);
    EXPECT_GT(CheckedAutomation::mApplied.load(), 8);
}
}

// ---- plugin parameter automation (VST3 / LV2) ---------------------------------------------
#include "au3-module-manager/PluginManager.h"
#include "au3-module-manager/PluginDescriptor.h"
#include "au3-module-manager/IEffectIdResolver.h"
#include "au3-preferences/BasicSettings.h"
#include <unordered_map>
#include <variant>
#ifdef AUTOTEST_LV2
#include "au3-lv2/LV2EffectBase.h"
#include "au3-lv2/LV2Symbols.h"
#include <lilv/lilv.h>
#endif
#ifdef AUTOTEST_VST3
#include "au3-vst3/VST3EffectBase.h"
#include "au3-vst3/VST3Instance.h"
#include <public.sdk/source/vst/hosting/module.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#endif

namespace au::record {
namespace {
const PluginID kLv2GainId = wxT("au-test:lv2-autogain");
const PluginID kVst3GainId = wxT("au-test:vst3-autogain");


//! In-memory settings so PluginManager can serve per-plugin preferences (copied from au3/tests/MockedPrefs.cpp)
class TestPluginSettings final : public audacity::BasicSettings
{
    std::vector<wxString> mGroupStack { "/" };

    using DataType = std::variant<
        wxString,
        bool,
        int,
        long,
        long long,
        double>;

    template<typename T>
    bool DoRead(const wxString& key, T* value) const
    {
        auto it = mStorage.find(MakePath(key));
        if (it == mStorage.end()) {
            return false;
        }
        if (auto ptr = std::get_if<T>(&it->second)) {
            *value = *ptr;
            return true;
        }
        return false;
    }

    bool DoWrite(const wxString& key, DataType value)
    {
        mStorage[key] = std::move(value);
        return true;
    }

protected:

    void DoBeginGroup(const wxString& prefix) override
    {
        mGroupStack.push_back(prefix);
    }

    void DoEndGroup() noexcept override
    {
        mGroupStack.pop_back();
    }

    bool HasGroup(const wxString& key) const override
    {
        return true;
    }

    bool HasEntry(const wxString& key) const override
    {
        return mStorage.find(MakePath(key)) != mStorage.end();
    }

    bool Flush() noexcept override
    {
        return true;
    }

    wxString GetGroup() const override
    {
        return mGroupStack.back();
    }

    wxArrayString GetChildGroups() const override
    {
        return { };
    }

    wxArrayString GetChildKeys() const override
    {
        return { };
    }

    bool Remove(const wxString& key) override
    {
        if (key.empty()) {
            for (auto it = mStorage.begin(); it != mStorage.end(); ++it) {
                if (it->first.StartsWith(key)) {
                    it = mStorage.erase(it);
                }
            }
        } else {
            auto it = mStorage.find(MakePath(key));
            if (it != mStorage.end()) {
                mStorage.erase(it);
                return true;
            }
        }
        return false;
    }

    void Clear() override
    {
        mStorage.clear();
    }

    bool Read(const wxString& key, bool* value) const override { return DoRead(key, value); }

    bool Read(const wxString& key, int* value) const override { return DoRead(key, value); }
    bool Read(const wxString& key, long* value) const override { return DoRead(key, value); }
    bool Read(const wxString& key, long long* value) const override { return DoRead(key, value); }
    bool Read(const wxString& key, double* value) const override { return DoRead(key, value); }
    bool Read(const wxString& key, wxString* value) const override { return DoRead(key, value); }

    bool Write(const wxString& key, bool value) override { return DoWrite(key, { value }); }
    bool Write(const wxString& key, int value) override { return DoWrite(key, { value }); }
    bool Write(const wxString& key, long value) override { return DoWrite(key, { value }); }
    bool Write(const wxString& key, long long value) override { return DoWrite(key, { value }); }
    bool Write(const wxString& key, double value) override { return DoWrite(key, { value }); }
    bool Write(const wxString& key, const wxString& value) override { return DoWrite(key, { value }); }

private:

    wxString MakePath(const wxString& key) const
    {
        return mGroupStack.back() + "/" + key;
    }

    std::unordered_map<wxString, DataType> mStorage;
};

void ensurePluginManagerSettings()
{
    static bool done = false;
    if (!done) {
        done = true;
        PluginManager::Get().Initialize([](const FilePath&) -> std::unique_ptr<audacity::BasicSettings> {
            return std::make_unique<TestPluginSettings>();
        }, nullptr);
    }
}

//! The instances read per-plugin preferences through PluginManager: make the effect known
void registerWithPluginManager(const EffectDefinitionInterface& effect)
{
    ensurePluginManagerSettings();
    PluginDescriptor d;
    d.SetPluginType(PluginTypeEffect);
    d.SetID(PluginManager::GetID(&effect));
    d.SetSymbol(effect.GetSymbol());
    d.SetVendor(effect.GetVendor().Internal());
    d.SetEffectFamily(effect.GetFamily().Internal());
    d.SetEnabled(true);
    d.SetValid(true);
    PluginManager::Get().RegisterPlugin(std::move(d));
}

#ifdef AUTOTEST_LV2
const LV2EffectBase* lv2Gain()
{
    static std::unique_ptr<LV2EffectBase> effect = []() -> std::unique_ptr<LV2EffectBase> {
        const std::string dir = std::string(AUTOTEST_PLUGIN_DIR) + "/lv2";
        setenv("LV2_PATH", dir.c_str(), 1);
        if (!LV2Symbols::InitializeGWorld()) {
            return {};
        }
        lilv_world_load_all(LV2Symbols::gWorld);
        LilvNode* uri = lilv_new_uri(LV2Symbols::gWorld, "urn:audacity:test:autogain");
        const LilvPlugin* plug = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(LV2Symbols::gWorld), uri);
        lilv_node_free(uri);
        if (!plug) {
            return {};
        }
        auto e = std::make_unique<LV2EffectBase>(*plug);
        if (!e->InitializePlugin()) {
            return {};
        }
        registerWithPluginManager(*e);
        return e;
    }();
    return effect.get();
}
#endif

#ifdef AUTOTEST_VST3
const VST3EffectBase* vst3Gain()
{
    static std::unique_ptr<VST3EffectBase> effect = []() -> std::unique_ptr<VST3EffectBase> {
        std::string error;
        const std::string path = std::string(AUTOTEST_PLUGIN_DIR) + "/vst3/AutoTestGain.vst3";
        auto module = VST3::Hosting::Module::create(path, error);
        if (!module) {
            std::cerr << "VST3 test plugin: " << error << std::endl;
            return {};
        }
        for (const auto& info : module->getFactory().classInfos()) {
            if (info.category() == kVstAudioEffectClass) {
                auto e = std::make_unique<VST3EffectBase>(module, info);
                registerWithPluginManager(*e);
                return e;
            }
        }
        return {};
    }();
    return effect.get();
}
#endif

const EffectInstanceFactory* pluginFactory(const PluginID& id)
{
#ifdef AUTOTEST_LV2
    if (id == kLv2GainId) {
        return lv2Gain();
    }
#endif
#ifdef AUTOTEST_VST3
    if (id == kVst3GainId) {
        return vst3Gain();
    }
#endif
    return builtinFactory(id);
}

ParamLane pluginLane(const PluginID& id, const std::string& key, std::vector<ParamPoint> pts)
{
    ParamLane lane;
    lane.effectId = id.ToStdString();
    lane.paramKey = key;
    lane.points = std::move(pts);
    return lane;
}

const au::au3::ParamInfo* findParam(const std::vector<au::au3::ParamInfo>& params, const std::string& key)
{
    auto it = std::find_if(params.begin(), params.end(), [&](const auto& p) { return p.key == key; });
    return it == params.end() ? nullptr : &*it;
}
}

class PluginAutomationTests : public Au3RecordTests
{
protected:
    //! Run the realtime (playback) path of `state` over one second of a quiet tone
    std::vector<float> playOneSecond(const std::shared_ptr<RealtimeEffectState>& state, Au3WaveTrack* track)
    {
        const double rate = TEST_SAMPLE_RATE;
        auto instance = state->Initialize(rate, 4096);
        EXPECT_TRUE(instance);
        EXPECT_TRUE(state->AddGroup(track, 1, float(rate), 4096));
        const size_t total = size_t(rate);
        auto in = monoSine(total, rate, 0.05f);
        std::vector<float> out(total), dummy(4096);
        for (size_t pos = 0; pos < total; pos += 1000) {
            const size_t n = std::min<size_t>(1000, total - pos);
            RealtimeEffectState::SetProcessTime(pos / rate, rate);
            const float* ip[] = { in.data() + pos };
            float* op[] = { out.data() + pos };
            state->ProcessStart(true);
            state->Process(track, 1, ip, op, dummy.data(), n);
            state->ProcessEnd();
        }
        RealtimeEffectState::SetProcessTime(-1.0, 0.0);
        state->Finalize();
        return out;
    }

    //! Render two seconds of a tone through the export mixer
    std::vector<float> exportTwoSeconds(Au3WaveTrack* track)
    {
        const double rate = track->GetRate();
        if (track->Intervals().empty()) {
            auto tone = monoSine(size_t(2 * rate), rate, 0.05f);
            track->Append(0, reinterpret_cast<constSamplePtr>(tone.data()), floatSample, tone.size());
            track->Flush();
        }
        pa::install(projectRef());
        auto mixer = ExportPluginHelpers::CreateMixer(projectRef(), false, 0.0, 2.0, 1, 4096, true, rate, floatSample, nullptr);
        std::vector<float> out;
        while (auto n = mixer->Process()) {
            auto p = reinterpret_cast<const float*>(mixer->GetBuffer());
            out.insert(out.end(), p, p + n);
        }
        return out;
    }

    //! Applying `lanes` many times allocates nothing
    long allocationsDuringApply(const RealtimeEffectAutomation& applier, EffectSettings& settings, EffectInstance* instance)
    {
        gCountedAllocs = 0;
        tCountAllocs = true;
        for (int i = 0; i < 2000; ++i) {
            applier.Apply(settings, i / 1000.0, instance);
        }
        tCountAllocs = false;
        return gCountedAllocs.load();
    }
};

#ifdef AUTOTEST_LV2
TEST_F(PluginAutomationTests, Lv2EnumeratesControlInputPorts)
{
    RealtimeEffectState::EffectFactory::Scope scope { &pluginFactory };
    ASSERT_TRUE(lv2Gain());
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto state = addEffect(RealtimeEffectList::Get(*track), kLv2GainId);
    EXPECT_EQ(pa::formatOf(*state), au::au3::ParamFormat::LV2);

    const auto params = pa::listParams(*state);
    ASSERT_EQ(params.size(), 3u); // gain, mode, mute; not the output port "level"
    const auto* gain = findParam(params, "gain");
    ASSERT_TRUE(gain);
    EXPECT_EQ(gain->name, "Gain");
    EXPECT_FLOAT_EQ(gain->min, -40.0);
    EXPECT_FLOAT_EQ(gain->max, 24.0);
    EXPECT_FLOAT_EQ(gain->def, 0.0);
    EXPECT_FALSE(gain->isInteger);
    EXPECT_FALSE(gain->normalized);
    const auto* mode = findParam(params, "mode");
    ASSERT_TRUE(mode);
    EXPECT_TRUE(mode->isInteger);
    EXPECT_EQ(mode->stepCount, 2);
    EXPECT_EQ(mode->enumLabels, (std::vector<std::string> { "Clean", "Warm", "Hot" }));
    const auto* mute = findParam(params, "mute");
    ASSERT_TRUE(mute);
    EXPECT_EQ(mute->stepCount, 1);
    EXPECT_FALSE(findParam(params, "level"));

    EXPECT_EQ(pa::valueText(*state, "mode", 1.2), "Warm");
    EXPECT_EQ(pa::valueText(*state, "mute", 1.0), "On");
    EXPECT_NE(pa::valueText(*state, "gain", -6.0).find("-6.00"), std::string::npos);
}

TEST_F(PluginAutomationTests, Lv2ApplyWritesPortValuesWithoutAllocating)
{
    RealtimeEffectState::EffectFactory::Scope scope { &pluginFactory };
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto state = addEffect(RealtimeEffectList::Get(*track), kLv2GainId);
    auto gain = pluginLane(kLv2GainId, "gain", { { 0.0, -40.0 }, { 1.0, 40.0 } }); // clamps to 24
    auto mode = pluginLane(kLv2GainId, "mode", { { 0.0, 0.0 }, { 1.0, 2.0 } });
    auto applier = pa::makeApplier(*state, { &gain, &mode });
    ASSERT_TRUE(applier);
    EffectSettings settings = state->GetSettings();
    EXPECT_EQ(allocationsDuringApply(*applier, settings, nullptr), 0);
    applier->Apply(settings, 0.25, nullptr);
    const auto& values = settings.cast<LV2EffectSettings>()->values;
    ASSERT_GE(values.size(), 3u); // control ports in order: gain, mode, mute, (level)
    EXPECT_FLOAT_EQ(values[0], -20.0f);
    EXPECT_FLOAT_EQ(values[1], 1.0f); // 0.5 snapped to a scale point
    applier->Apply(settings, 0.9, nullptr);
    EXPECT_FLOAT_EQ(values[0], 24.0f);
    EXPECT_FLOAT_EQ(values[1], 2.0f);
}

TEST_F(PluginAutomationTests, Lv2PlaybackAndExportRampGain)
{
    RealtimeEffectState::EffectFactory::Scope scope { &pluginFactory };
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto state = addEffect(RealtimeEffectList::Get(*track), kLv2GainId);
    ParamAutomationData::Get(*track).lanes.push_back(pluginLane(kLv2GainId, "gain", { { 0.0, -24.0 }, { 1.0, 0.0 } }));
    pa::install(projectRef());
    ASSERT_TRUE(state->GetAutomation());
    const auto out = playOneSecond(state, track);
    EXPECT_NEAR(windowDb(out, TEST_SAMPLE_RATE, 0.8, 0.9) - windowDb(out, TEST_SAMPLE_RATE, 0.1, 0.2), 16.8, 0.5);

    auto& lanes = ParamAutomationData::Get(*track).lanes;
    lanes[0].points = { { 0.0, -24.0 }, { 2.0, 0.0 } };
    const auto rendered = exportTwoSeconds(track);
    ASSERT_GE(rendered.size(), size_t(1.9 * track->GetRate()));
    EXPECT_NEAR(windowDb(rendered, track->GetRate(), 1.6, 1.8) - windowDb(rendered, track->GetRate(), 0.2, 0.4), 16.8, 0.5);
}
#endif

#ifdef AUTOTEST_VST3
TEST_F(PluginAutomationTests, Vst3EnumeratesAutomatableParameters)
{
    RealtimeEffectState::EffectFactory::Scope scope { &pluginFactory };
    ASSERT_TRUE(vst3Gain());
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto state = addEffect(RealtimeEffectList::Get(*track), kVst3GainId);
    EXPECT_EQ(pa::formatOf(*state), au::au3::ParamFormat::VST3);

    const auto params = pa::listParams(*state);
    // gain (100) and mode (200); bypass (300) and the read-only meter (400) are skipped
    ASSERT_EQ(params.size(), 2u);
    const auto* gain = findParam(params, "100");
    ASSERT_TRUE(gain);
    EXPECT_EQ(gain->name, "Gain");
    EXPECT_EQ(gain->units, "dB");
    EXPECT_TRUE(gain->normalized);
    EXPECT_DOUBLE_EQ(gain->min, 0.0);
    EXPECT_DOUBLE_EQ(gain->max, 1.0);
    EXPECT_NEAR(gain->def, 0.625, 1e-9);
    const auto* mode = findParam(params, "200");
    ASSERT_TRUE(mode);
    EXPECT_EQ(mode->stepCount, 2);
    EXPECT_EQ(mode->enumLabels, (std::vector<std::string> { "Clean", "Warm", "Hot" }));

    // the plugin's own value-to-string, plus units
    const auto text = pa::valueText(*state, "100", 0.25);
    EXPECT_NE(text.find("-24"), std::string::npos) << text;
    EXPECT_NE(text.find("dB"), std::string::npos) << text;
    EXPECT_EQ(pa::valueText(*state, "200", 0.5), "Warm");
}

TEST_F(PluginAutomationTests, Vst3ApplyQueuesWithoutAllocating)
{
    RealtimeEffectState::EffectFactory::Scope scope { &pluginFactory };
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto state = addEffect(RealtimeEffectList::Get(*track), kVst3GainId);
    auto gain = pluginLane(kVst3GainId, "100", { { 0.0, 0.0 }, { 1.0, 1.0 } });
    auto applier = pa::makeApplier(*state, { &gain });
    ASSERT_TRUE(applier);
    auto instance = state->GetInstance();
    ASSERT_TRUE(dynamic_cast<VST3Instance*>(instance.get()));
    EffectSettings settings = state->GetSettings();
    EXPECT_EQ(allocationsDuringApply(*applier, settings, instance.get()), 0);
}

TEST_F(PluginAutomationTests, Vst3PlaybackAndExportRampGain)
{
    RealtimeEffectState::EffectFactory::Scope scope { &pluginFactory };
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto state = addEffect(RealtimeEffectList::Get(*track), kVst3GainId);
    // normalized 0.25 = -24 dB, 0.625 = 0 dB
    ParamAutomationData::Get(*track).lanes.push_back(pluginLane(kVst3GainId, "100", { { 0.0, 0.25 }, { 1.0, 0.625 } }));
    pa::install(projectRef());
    ASSERT_TRUE(state->GetAutomation());
    const auto out = playOneSecond(state, track);
    EXPECT_NEAR(windowDb(out, TEST_SAMPLE_RATE, 0.8, 0.9) - windowDb(out, TEST_SAMPLE_RATE, 0.1, 0.2), 16.8, 0.5);

    ParamAutomationData::Get(*track).lanes[0].points = { { 0.0, 0.25 }, { 2.0, 0.625 } };
    const auto rendered = exportTwoSeconds(track);
    ASSERT_GE(rendered.size(), size_t(1.9 * track->GetRate()));
    EXPECT_NEAR(windowDb(rendered, track->GetRate(), 1.6, 1.8) - windowDb(rendered, track->GetRate(), 0.2, 0.4), 16.8, 0.5);
}
#endif

TEST_F(PluginAutomationTests, PluginLanesSaveByStableParameterIds)
{
    Au3WaveTrack* track = addSelectedMonoTrack();
    auto& data = ParamAutomationData::Get(*track);
    data.lanes.push_back(pluginLane(kVst3GainId, "100", { { 0.5, 0.25 } }));
    data.lanes.push_back(pluginLane(kLv2GainId, "gain", { { 1.0, -6.0 } }));
    XMLStringWriter xml;
    track->WriteXML(xml);
    EXPECT_NE(xml.find(wxT("param=\"100\"")), wxString::npos);
    EXPECT_NE(xml.find(wxT("param=\"gain\"")), wxString::npos);
    EXPECT_NE(xml.find(wxT("effect=\"au-test:vst3-autogain\"")), wxString::npos);
}
}
