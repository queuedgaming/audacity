/*
* Audacity: A Digital Audio Editor
*/

#include "au3record.h"
#include "au3wrap/internal/takelanes.h"
#include "au3wrap/internal/projectkey.h"
#include "au3wrap/internal/transients.h"
#include "au3-numeric-formats/ProjectTimeSignature.h"
#include "au3-label-track/LabelTrack.h"

#include <cfloat>

#include "au3-strings/TranslatableString.h"

#include "framework/global/translation.h"
#include "framework/global/log.h"

#include "au3-audio-io/ProjectAudioIO.h"
#include "au3-time-frequency-selection/ViewInfo.h"
#include "au3-track/Track.h"
#include "au3-track/PendingTracks.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3-wave-track/WaveClip.h"
#include "au3-stretching-sequence/StretchingSequence.h"
#include "au3-project-rate/ProjectRate.h"

#include "au3wrap/internal/domconverter.h"
#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/internal/wxtypes_convert.h"
#include "au3wrap/au3types.h"

#include "au3audioinput.h"
#include "../../recorderrors.h"

using namespace au::au3::takelanes;
using au::au3::TakeLaneInfo;
using namespace muse;
using namespace muse::async;
using namespace au::record;
using namespace au::au3;

constexpr int RATE_NOT_SELECTED = -1;
using WritableSampleTrackArray = std::vector< std::shared_ptr< WritableSampleTrack > >;

static const muse::actions::ActionQuery PLAYBACK_SEEK_QUERY("action://playback/seek");

struct PropertiesOfSelected
{
    bool allSameRate = false;
    int rateOfSelected = RATE_NOT_SELECTED;
    bool anySelected = false;
};

// GetSelectedProperties collects information about
// currently selected audio tracks
PropertiesOfSelected GetPropertiesOfSelected(const Au3Project& proj)
{
    double rateOfSelection = RATE_NOT_SELECTED;

    PropertiesOfSelected result;
    result.allSameRate = true;

    const auto selectedTracks{
        Au3TrackList::Get(proj).Selected<const Au3WaveTrack>() };

    for (const auto& track : selectedTracks) {
        if (rateOfSelection != RATE_NOT_SELECTED
            && track->GetRate() != rateOfSelection) {
            result.allSameRate = false;
        } else if (rateOfSelection == RATE_NOT_SELECTED) {
            rateOfSelection = track->GetRate();
        }
    }

    result.anySelected = !selectedTracks.empty();
    result.rateOfSelected = rateOfSelection;

    return result;
}

WritableSampleTrackArray ChooseExistingRecordingTracks(Au3Project& proj, const bool selectedOnly, const double targetRate,
                                                       const int recordingChannels)
{
    auto p = &proj;
    bool strictRules = (recordingChannels <= 2);

    // Iterate over all wave tracks, or over selected wave tracks only.
    // If target rate was specified, ignore all tracks with other rates.
    //
    // In the usual cases of one or two recording channels, seek a first-fit
    // unbroken sub-sequence for which the total number of channels matches the
    // required number exactly.  Never drop inputs or fill only some channels
    // of a track.
    //
    // In case of more than two recording channels, choose tracks only among the
    // selected.  Simply take the earliest wave tracks, until the number of
    // channels is enough.  If there are fewer channels than inputs, but at least
    // one channel, then some of the input channels will be dropped.
    //
    // Resulting tracks may be non-consecutive within the list of all tracks
    // (there may be non-wave tracks between, or non-selected tracks when
    // considering selected tracks only.)

    if (!strictRules && !selectedOnly) {
        return {};
    }

    auto& trackList = Au3TrackList::Get(*p);
    WritableSampleTrackArray candidates;
    std::vector<int> channelCounts;
    int totalChannels = 0;
    const auto range = trackList.Any<Au3WaveTrack>();
    for (auto candidate : selectedOnly ? range + &Au3Track::IsSelected : range) {
        if (targetRate != RATE_NOT_SELECTED && candidate->GetRate() != targetRate) {
            continue;
        }

        // count channels in this track
        const int nChannels = static_cast<int>(candidate->NChannels());
        if (strictRules && nChannels > recordingChannels) {
            const bool stereoTrackMonoInput
                =(recordingChannels == 1 && nChannels == 2 && candidates.empty());
            if (stereoTrackMonoInput) {
                candidates.clear();
                channelCounts.clear();
                candidates.push_back(candidate->SharedPointer<Au3WaveTrack>());
                channelCounts.push_back(nChannels);
                totalChannels = nChannels;
                break;
            }

            candidates.clear();
            channelCounts.clear();
            totalChannels = 0;
            continue;
        } else {
            // Might use this but may have to discard some of the accumulated
            while (strictRules
                   && nChannels + totalChannels > recordingChannels) {
                candidates.erase(candidates.begin());
                auto nOldChannels = channelCounts[0];
                assert(nOldChannels > 0);
                channelCounts.erase(channelCounts.begin());
                totalChannels -= nOldChannels;
            }
            candidates.push_back(candidate->SharedPointer<Au3WaveTrack>());
            channelCounts.push_back(nChannels);
            totalChannels += nChannels;
            if (totalChannels >= recordingChannels) {
                // Done!
                return candidates;
            }
        }
    }

    if (strictRules && !candidates.empty()) {
        const bool allowDownmix
            =(recordingChannels == 2 && totalChannels == 1 && candidates.size() == 1);
        const bool allowUpmix
            =(recordingChannels == 1 && totalChannels == 2 && candidates.size() == 1
              && !channelCounts.empty() && channelCounts.front() == 2);
        if (allowDownmix || allowUpmix) {
            return candidates;
        }
    }

    if (!strictRules && !candidates.empty()) {
        // good enough
        return candidates;
    }

    // If the loop didn't exit early, we could not find enough channels
    return {};
}

TransportSequences MakeTransportTracks(Au3TrackList& trackList, bool selectedOnly, bool nonWaveToo)
{
    TransportSequences result;
    {
        const auto range = trackList.Any<Au3WaveTrack>()
                           + (selectedOnly ? &Au3Track::IsSelected : &Au3Track::Any);
        for (auto pTrack : range) {
            result.playbackSequences.push_back(
                StretchingSequence::Create(*pTrack, pTrack->GetClipInterfaces()));
        }
    }
    if (nonWaveToo) {
        const auto range = trackList.Any<const PlayableTrack>()
                           + (selectedOnly ? &Au3Track::IsSelected : &Au3Track::Any);
        for (auto pTrack : range) {
            if (!track_cast<const SampleTrack*>(pTrack)) {
                if (auto pSequence
                        =std::dynamic_pointer_cast<const OtherPlayableSequence>(
                              pTrack->shared_from_this())
                        ) {
                    result.otherPlayableSequences.push_back(pSequence);
                }
            }
        }
    }
    return result;
}

void Au3Record::init()
{
    m_audioInput = std::make_shared<Au3AudioInput>(iocContext());

    audioEngine()->recordingUpdateRequested().onNotify(this, [this]() {
        if (m_recordData.empty()) {
            return;
        }
        auto& pendingTracks = PendingTracks::Get(projectRef());
        pendingTracks.UpdatePendingTracks();
    });

    audioEngine()->recordingClipChanged().onReceive(this, [this](const au3::Au3TrackId& trackId, const au3::Au3ClipId& clipId) {
        if (m_recordData.empty()) {
            return;
        }

        // Look up by pendingClipId (stable across deferred creation)
        auto recordedClip = std::find_if(m_recordData.begin(), m_recordData.end(),
                                         [&](const RecordData& r){ return r.pendingClipId == clipId; });
        if (recordedClip == m_recordData.end()) {
            return;
        }

        Au3WaveTrack* origWaveTrack = DomAccessor::findWaveTrack(projectRef(), trackId);
        Au3Track* pendingTrack = PendingTracks::Get(projectRef()).FindPendingTrack(*origWaveTrack);
        if (!pendingTrack) {
            return;
        }

        Au3WaveTrack* pendingWaveTrack = dynamic_cast<Au3WaveTrack*>(pendingTrack);
        IF_ASSERT_FAILED(pendingWaveTrack) {
            return;
        }

        // For lead-in-recording with lead-in time: the clip only exists on the pending track.
        // Only create it on the original when the engine is actually capturing (after lead-in time).
        if (recordedClip->deferredClipCreation) {
            if (!audioEngine()->isCapturing()) {
                return; // Still in lead-in time — don't create the clip yet
            }

            std::shared_ptr<Au3WaveClip> pendingClip = DomAccessor::findWaveClip(pendingWaveTrack, clipId);
            IF_ASSERT_FAILED(pendingClip) {
                return;
            }

            // Create a matching clip on the original track at the recording start position
            auto origClip = origWaveTrack->CreateClip(pendingClip->GetPlayStartTime(), pendingClip->GetName());
            origClip->SetIsPlaceholder(true);
            origWaveTrack->InsertInterval(origClip, true);
            origClip->SetIsPlaceholder(false);

            // Store the original clip key for commit
            recordedClip->clipKey = trackedit::ClipKey(origWaveTrack->GetId(), origClip->GetId());

            // Link original clip to pending clip's data
            origClip->LinkToOtherSource(*pendingClip.get());
            recordedClip->linkedToPendingClip = true;
            recordedClip->deferredClipCreation = false;
            rebuildRecordingClipKeys();

            trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
            prj->notifyAboutClipAdded(DomConverter::clip(origWaveTrack, origClip.get()));

            m_recordPosition.set(origClip->GetPlayEndTime());
            return;
        }

        // Normal path: clip already exists on original track
        std::shared_ptr<Au3WaveClip> origClip = DomAccessor::findWaveClip(origWaveTrack, recordedClip->clipKey.itemId);
        IF_ASSERT_FAILED(origClip) {
            return;
        }

        if (!recordedClip->linkedToPendingClip) {
            std::shared_ptr<Au3WaveClip> pendingClip = DomAccessor::findWaveClip(pendingWaveTrack, size_t(0));
            IF_ASSERT_FAILED(pendingClip) {
                return;
            }

            origClip->LinkToOtherSource(*pendingClip.get());
            recordedClip->linkedToPendingClip = true;
        }

        // Invalidate waveform cache on the original clip so new audio data gets painted.
        // MarkChanged() is only called on the pendingClip by the audio engine; the origClip
        // shares data via LinkToOtherSource but its WaveformPainter cache is never notified.
        origClip->MarkChanged();

        trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
        prj->notifyAboutClipChanged(DomConverter::clip(origWaveTrack, origClip.get()));

        m_recordPosition.set(origClip->GetPlayEndTime());
    });

    audioEngine()->recordingFinished().onNotify(this, [this]() {
        if (m_recordData.empty()) {
            return;
        }
        m_recordPosition.set(m_recordPosition.val);
        m_recordingFinished.notify();

        muse::actions::ActionQuery q(PLAYBACK_SEEK_QUERY);
        q.addParam("seekTime", muse::Val(m_recordPosition.val));
        q.addParam("triggerPlay", muse::Val(false));
        dispatcher()->dispatch(q);

        m_recordData.clear();
        m_takeSnapshots.clear();
        rebuildRecordingClipKeys();
    });

    audioEngine()->recordingCommitRequested().onNotify(this, [this]() {
        if (m_recordData.empty()) {
            return;
        }
        for (const RecordData& recordEntry : m_recordData) {
            // Skip deferred clips that were never created (cancelled during lead-in time)
            if (recordEntry.deferredClipCreation) {
                continue;
            }
            trackedit::ClipKey clipKey = recordEntry.clipKey;
            Au3WaveTrack* origWaveTrack = DomAccessor::findWaveTrack(projectRef(), ::TrackId(clipKey.trackId));

            std::shared_ptr<Au3WaveClip> origClip = DomAccessor::findWaveClip(origWaveTrack, clipKey.itemId);
            IF_ASSERT_FAILED(origClip) {
                return;
            }

            trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();

            prj->notifyAboutClipChanged(DomConverter::clip(origWaveTrack, origClip.get()));
            m_recordPosition.set(origClip->GetPlayEndTime());
        }

        preserveOverwrittenTakes();

        for (const RecordData& recordEntry : m_recordData) {
            // Skip deferred clips that were never created (cancelled during lead-in time)
            if (recordEntry.deferredClipCreation) {
                continue;
            }
            trackedit::ClipKey clipKey = recordEntry.clipKey;
            trackeditInteraction()->makeRoomForClip(clipKey);
        }

        auto& pendingTracks = PendingTracks::Get(projectRef());
        pendingTracks.ClearPendingTracks();

        commitRecording();
    });
}

muse::Ret Au3Record::start()
{
    //! NOTE: copied from ProjectAudioManager::OnRecord

    //! TODO: should be configurable
    bool altAppearance = false;

    // TODO: should preferNewTrack be an option?
    // bool bPreferNewTrack = false;
    // gPrefs->Read("/GUI/PreferNewTrackRecord", &bPreferNewTrack, false);
    const bool appendRecord = true;//(altAppearance == bPreferNewTrack);

    if (!audioDriverController() || audioDriverController()->inputDevices().empty()
        || audioDriverController()->inputChannelsAvailable() <= 0) {
        return make_ret(Err::NoRecordingDevice);
    }

    Au3Project& project = projectRef();

    double t0 = playbackState()->playbackPosition();
    double t1 = DBL_MAX;
    selectionController()->resetSelectedClips();

    double projectRate = ProjectRate::Get(project).GetRate();

    WritableSampleTrackArray existingTracks;

    // Checking the selected tracks: counting them and
    // making sure they all have the same rate
    const auto selectedTracks{ GetPropertiesOfSelected(project) };
    const int rateOfSelected{ selectedTracks.rateOfSelected };
    const bool anySelected{ selectedTracks.anySelected };
    const bool allSameRate{ selectedTracks.allSameRate };

    if (!allSameRate) {
        return make_ret(Err::MismatchedSamplingRatesError);
    }

    if (appendRecord) {
        const int recordingChannels = std::max(0, audioDriverController()->configuration().inputChannels);

        // Try to find wave tracks to record into.  (If any are selected,
        // try to choose only from them; else if wave tracks exist, may record into any.)
        existingTracks = ChooseExistingRecordingTracks(project, true, rateOfSelected, recordingChannels);
        if (!existingTracks.empty()) {
            projectRate = rateOfSelected;
        } else {
            if (anySelected && rateOfSelected != projectRate) {
                return make_ret(Err::TooFewCompatibleTracksSelected);
            }

            existingTracks = ChooseExistingRecordingTracks(project, false, projectRate, recordingChannels);
        }
    }

    bool useDuplex = true;
    // gPrefs->Read(wxT("/AudioIO/Duplex"), &useDuplex, true); // todo

    TransportSequences transportTracks;
    if (useDuplex) {
        // Remove recording tracks from the list of tracks for duplex ("overdub")
        // playback.
        /* TODO: set up stereo tracks if that is how the user has set up
          * their preferences, and choose sample format based on prefs */
        transportTracks = MakeTransportTracks(Au3TrackList::Get(project), false, true);
        for (const auto& wt : existingTracks) {
            auto end = transportTracks.playbackSequences.end();
            auto it = std::find_if(
                transportTracks.playbackSequences.begin(), end,
                [&wt](const auto& playbackSequence) {
                return playbackSequence->FindChannelGroup() == wt->FindChannelGroup();
            });
            if (it != end) {
                transportTracks.playbackSequences.erase(it);
            }
        }
    }

    std::copy(existingTracks.begin(), existingTracks.end(),
              back_inserter(transportTracks.captureSequences));

    if (audioEngine()->isBusy()) {
        if (audioEngine()->isCapturing()) {
            return make_ret(Ret::Code::InternalError, std::string("Another project is recording"));
        }
        audioEngine()->stopStream();
    }

    audioEngine()->stopMonitoring();

    return doRecord(project, transportTracks, t0, t1, altAppearance, projectRate);
}

muse::Ret Au3Record::pause()
{
    if (!canStopAudioStream()) {
        return make_ret(Err::RecordingStopError);
    }

    audioEngine()->pauseStream(true);

    return make_ok();
}

muse::Ret Au3Record::resume()
{
    if (!canStopAudioStream()) {
        return make_ret(Err::RecordingResumeError);
    }

    audioEngine()->pauseStream(false);

    return make_ok();
}

muse::Ret Au3Record::stop()
{
    //! NOTE: copied from ProjectAudioManager::Stop
    if (!canStopAudioStream()) {
        return make_ret(Err::RecordingStopError);
    }

    audioEngine()->stopStream();
    //Make sure to unpause
    audioEngine()->pauseStream(false);

    // So that we continue monitoring after playing or recording.
    // also clean the MeterQueues
    Au3Project& project = projectRef();
    auto& projectAudioIO = ProjectAudioIO::Get(project);
    auto playbackMeter = projectAudioIO.GetPlaybackMeter();
    if (playbackMeter) {
        playbackMeter->stop();
    }

    auto captureMeter = projectAudioIO.GetCaptureMeter();
    if (captureMeter) {
        captureMeter->stop();
    }

    return make_ok();
}

double Au3Record::punchOutTime(double punchInTime) const
{
    if (!recordConfiguration()->punchOutAtSelectionEnd()) {
        return DBL_MAX;
    }
    const double selStart = selectionController()->dataSelectedStartTime();
    const double selEnd = selectionController()->dataSelectedEndTime();
    // Require a real time selection whose end lies after the punch-in point
    constexpr double minPunchDuration = 0.01;
    if (selEnd > selStart && selEnd > punchInTime + minPunchDuration) {
        return selEnd;
    }
    return DBL_MAX;
}

muse::Ret Au3Record::leadInRecording()
{
    Au3Project& project = projectRef();

    // Recording always starts from the current playhead position, regardless
    // of the playback state or where the last selection was made
    double t1 = std::max(0.0, playbackState()->playbackPosition().to_double());

    // Lead-in recording at t=0 makes no sense — there's no audio to play as lead-in
    if (t1 == 0.0) {
        return make_ret(Err::LeadInRecordingNoValidClipAtCursor);
    }

    // Checking the selected tracks: making sure they all have the same rate
    const auto selectedTracks{ GetPropertiesOfSelected(project) };
    const int rateOfSelected{ selectedTracks.rateOfSelected };
    const bool allSameRate{ selectedTracks.allSameRate };

    if (!allSameRate) {
        return make_ret(Err::MismatchedSamplingRatesError);
    }

    // Find tracks to record into (must be selected)
    const int recordingChannels = std::max(0, audioDriverController()->configuration().inputChannels);
    auto tracks = ChooseExistingRecordingTracks(project, true, rateOfSelected, recordingChannels);
    if (tracks.empty()) {
        return make_ret(Err::LeadInRecordingNoTracksSelected);
    }

    // Try to extract crossfade data from existing audio at the lead-in point.
    // If the cursor is not inside a clip, skip crossfade — just record from that position.
    std::vector<std::vector<float> > crossfadeData;
    const double crossFadeDuration = std::max(0.0,
                                              recordConfiguration()->crossfadeDuration() / 1000.0
                                              );

    bool cursorInClip = true;
    double newt1 = t1;
    for (const auto& wt : tracks) {
        Au3WaveTrack* waveTrack = dynamic_cast<Au3WaveTrack*>(wt.get());
        if (!waveTrack) {
            continue;
        }
        auto rate = waveTrack->GetRate();
        sampleCount testSample(floor(t1 * rate));
        const auto& intervals = static_cast<const Au3WaveTrack*>(waveTrack)->Intervals();
        auto pred = [rate](sampleCount testSample) {
            return [rate, testSample](const auto& pInterval) {
                auto start = floor(pInterval->Start() * rate + 0.5);
                auto end = floor(pInterval->End() * rate + 0.5);
                auto ts = testSample.as_double();
                return ts >= start && ts < end;
            };
        };
        auto begin = intervals.begin(), end = intervals.end(),
             iter = std::find_if(begin, end, pred(testSample));
        if (iter == end) {
            iter = std::find_if(begin, end, pred(testSample - 10));
        }
        if (iter == end) {
            cursorInClip = false;
        } else {
            newt1 = std::min(newt1, (*iter).get()->End() - crossFadeDuration);
        }
    }

    // Only extract crossfade samples if the cursor is inside a clip
    if (cursorInClip) {
        t1 = newt1;
        for (const auto& wt : tracks) {
            Au3WaveTrack* waveTrack = dynamic_cast<Au3WaveTrack*>(wt.get());
            if (!waveTrack) {
                continue;
            }
            const auto endTime = waveTrack->GetEndTime();
            const auto duration
                =std::max(0.0, std::min(crossFadeDuration, endTime - t1));
            const size_t getLen = floor(duration * waveTrack->GetRate());
            if (getLen > 0) {
                const auto nChannels = std::min<size_t>(2, waveTrack->NChannels());
                crossfadeData.resize(nChannels);
                float* buffers[2]{};
                for (size_t ii = 0; ii < nChannels; ++ii) {
                    auto& data = crossfadeData[ii];
                    data.resize(getLen);
                    buffers[ii] = data.data();
                }
                const sampleCount pos = waveTrack->TimeToLongSamples(t1);
                if (!waveTrack->GetFloats(0, nChannels, buffers, pos, getLen)) {
                    LOGW() << "Lead-in recording: GetFloats failed at position " << t1
                           << ", proceeding without crossfade";
                    crossfadeData.clear();
                    break;
                }
                break; // Use crossfade data from first track only
            }
        }
    }

    // Set up transport sequences
    // Unlike AU3, we do NOT clear tracks — AU4 uses non-destructive recording
    // Play all tracks (duplex) so user hears context during lead-in time
    TransportSequences transportTracks;
    transportTracks = MakeTransportTracks(Au3TrackList::Get(project), false, true);

    // Recording tracks are also capture targets
    std::copy(tracks.begin(), tracks.end(),
              back_inserter(transportTracks.captureSequences));

    selectionController()->resetSelectedClips();

    if (audioEngine()->isBusy()) {
        if (audioEngine()->isCapturing()) {
            return make_ret(Ret::Code::InternalError, std::string("Another project is recording"));
        }
        audioEngine()->stopStream();
    }

    audioEngine()->stopMonitoring();

    double leadInTime = std::max(0.0, std::min(t1, recordConfiguration()->leadInTimeDuration()));

    auto* pCrossfadeData = crossfadeData.empty() ? nullptr : &crossfadeData;
    // Punch-out: if a time selection ends after the punch-in point, stop
    // recording automatically at the selection end. Otherwise record until stopped.
    const double punchOut = punchOutTime(t1);
    Ret ret = doRecord(project, transportTracks, t1, punchOut, false, rateOfSelected, leadInTime, pCrossfadeData);

    if (ret) {
        // Initialize recording position to punch point so that cancelling
        // during lead-in returns to the correct position
        m_recordPosition.set(t1);

        // Set playhead to lead-in time start and trigger position tracking.
        // With isDefaultPlayTrackPolicy=false, the stream time starts from
        // t1-leadInTime so this will align with the actual audio position.
        muse::actions::ActionQuery q(PLAYBACK_SEEK_QUERY);
        q.addParam("seekTime", muse::Val(t1 - leadInTime));
        q.addParam("triggerPlay", muse::Val(false));
        dispatcher()->dispatch(q);
    }

    return ret;
}

IAudioInputPtr Au3Record::audioInput() const
{
    return m_audioInput;
}

muse::async::Channel<muse::secs_t> Au3Record::recordPositionChanged() const
{
    return m_recordPosition.ch;
}

const std::vector<au::trackedit::ClipKey>& Au3Record::recordingClipKeys() const
{
    return m_recordingClipKeys;
}

void Au3Record::rebuildRecordingClipKeys()
{
    m_recordingClipKeys.clear();
    m_recordingClipKeys.reserve(m_recordData.size());
    for (const auto& rd : m_recordData) {
        if (!rd.deferredClipCreation) {
            m_recordingClipKeys.push_back(rd.clipKey);
        }
    }
}

Notification Au3Record::recordingFinished() const
{
    return m_recordingFinished;
}

Au3Project& Au3Record::projectRef() const
{
    const auto project = reinterpret_cast<Au3Project*>(globalContext()->currentProject()->au3ProjectPtr());
    return *project;
}

Ret Au3Record::doRecord(Au3Project& project,
                        const TransportSequences& sequences,
                        double t0, double t1,
                        bool altAppearance,
                        const double audioStreamSampleRate,
                        double leadInTime,
                        std::vector<std::vector<float> >* crossfadeData)
{
    //! NOTE: copied fromProjectAudioManager::DoRecord

    m_recordPosition.set(t0);

    if (audioEngine()->isBusy()) {
        LOGE() << "Audio IO is busy";
        return make_ret(Ret::Code::InternalError);
    }

    // The engine's pause flag survives stream restarts; clear it so that a
    // recording initiated from a paused playback state starts immediately
    audioEngine()->pauseStream(false);

    // TODO: what is the meaning of the next line, do we need it?
    // projectAudioManager.SetAppending(!altAppearance);
    UNUSED(altAppearance);

    bool success = false;

    auto transportSequences = sequences;

    // Will replace any given capture tracks with temporaries
    transportSequences.captureSequences.clear();

    const auto p = &project;
    auto& trackList = Au3TrackList::Get(project);

    bool appendRecord = !sequences.captureSequences.empty();

    auto insertEmptyInterval
        =[&](Au3WaveTrack& track, double t0, bool placeholder, const Au3WaveTrack* nameSource = nullptr) {
        //! NOTE The pending track is an empty copy of the original, so the take
        //! number must be resolved against the original track's clips
        const Au3WaveTrack& source = nameSource ? *nameSource : track;
        wxString name;
        for (auto i = 1;; ++i) {
            //: a numerical suffix added to distinguish otherwise like-named clips when new record started
            //: %1 is the track name, %2 is the numerical suffix distinguishing like-named clips
            name = ::au3::qtToWx(::TranslatableString("audacity", "%1 #%2", "clip name template")
                                 .arg(source.GetName()).arg(i).translated());
            if (!source.HasClipNamed(name)) {
                break;
            }
        }

        auto clip = track.CreateClip(t0, name);
        // So that the empty clip is not skipped for insertion:
        clip->SetIsPlaceholder(true);
        track.InsertInterval(clip, true);
        if (!placeholder) {
            clip->SetIsPlaceholder(false);
        }
        return clip;
    };

    auto& pendingTracks = PendingTracks::Get(project);
    const bool deferClipCreation = (leadInTime > 0.0);

    if (appendRecord) {
        // Append recording:
        // Pad selected/all wave tracks to make them all the same length
        for (const auto& sequence : sequences.captureSequences) {
            Au3WaveTrack* wt{};
            if (!(wt = dynamic_cast<Au3WaveTrack*>(sequence.get()))) {
                assert(false);
                continue;
            }

            if (recordConfiguration()->keepTakesWhenRecordingOver()) {
                if (auto snapshot = snapshotFrom(*wt, t0)) {
                    m_takeSnapshots[trackedit::TrackId(wt->GetId())] = TakeSnapshot { snapshot, t0 };
                }
            }

            // If the track was chosen for recording and playback both,
            // remember the original in leadInTime tracks, before making the
            // pending replacement.
            const auto shared = wt->SharedPointer<Au3WaveTrack>();
            // leadInTimeSequences should be a subset of playbackSequences.
            const auto& range = transportSequences.playbackSequences;
            bool leadInTimeTrack = any_of(range.begin(), range.end(),
                                          [&](const auto& pSequence) {
                return shared.get() == pSequence->FindChannelGroup();
            });
            if (leadInTimeTrack) {
                transportSequences.leadInTimeSequences.push_back(shared);
            }

            if (deferClipCreation) {
                // Lead-in-recording with lead-in time: don't modify the original track yet.
                // Register pending track first (copy of unmodified original), then
                // create the clip only on the pending track. The original track stays
                // clean — the clip will be created on it when recording data arrives.
                auto clipIdHolder = std::make_shared<au3::Au3ClipId>();

                auto updater = [this, trackId = wt->GetId(), clipIdHolder](Au3Track& d, const Au3Track& s){
                    assert(d.NChannels() == s.NChannels());
                    auto& dst = static_cast<Au3WaveTrack&>(d);
                    auto& src = static_cast<const Au3WaveTrack&>(s);
                    dst.Init(src);

                    audioEngine()->recordingClipChanged().send(trackId, *clipIdHolder);
                };

                const auto pending = static_cast<Au3WaveTrack*>(
                    pendingTracks.RegisterPendingChangedTrack(updater, wt)
                    );

                // Insert clip on pending track only
                auto pendingClip = insertEmptyInterval(*pending, t0, false, wt);
                *clipIdHolder = pendingClip->GetId();

                transportSequences.captureSequences.push_back(pending->SharedPointer<Au3WaveTrack>());

                m_recordData.push_back(RecordData { trackedit::ClipKey(), pendingClip->GetId(), false, true });
                rebuildRecordingClipKeys();
                // Don't notify UI — clip will be created on original when recording starts
            } else {
                // Normal recording: create clip on original track immediately
                Au3WaveTrack::IntervalHolder newClip{};
                newClip = insertEmptyInterval(*wt, t0, true);

                auto updater = [this, trackId = wt->GetId(), clipId = newClip->GetId()](Au3Track& d, const Au3Track& s){
                    assert(d.NChannels() == s.NChannels());
                    auto& dst = static_cast<Au3WaveTrack&>(d);
                    auto& src = static_cast<const Au3WaveTrack&>(s);
                    dst.Init(src);

                    audioEngine()->recordingClipChanged().send(trackId, clipId);
                };

                const auto pending = static_cast<Au3WaveTrack*>(
                    pendingTracks.RegisterPendingChangedTrack(updater, wt)
                    );

                if (newClip) {
                    newClip->SetIsPlaceholder(false);
                }
                if (auto copiedClip = pending->NewestOrNewClip()) {
                    copiedClip->SetIsPlaceholder(false);
                }
                transportSequences.captureSequences.push_back(pending->SharedPointer<Au3WaveTrack>());

                m_recordData.push_back(RecordData { trackedit::ClipKey(wt->GetId(), newClip->GetId()), newClip->GetId(), false, false });
                rebuildRecordingClipKeys();

                trackedit::Clip _newClip = DomConverter::clip(pending, newClip.get());
                trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
                prj->notifyAboutClipAdded(_newClip);
            }
        }
        pendingTracks.UpdatePendingTracks();
    }

    if (transportSequences.captureSequences.empty()) {
        // recording to NEW track(s).
        // Count the tracks.
        auto numTracks = trackList.Any<const Au3WaveTrack>().size();

        const auto recordingChannels = std::max(1, audioDriverController()->configuration().inputChannels);

        const RecordingTrackNameOptions nameOptions = recordConfiguration()->recordingTrackNameOptions();
        const wxString defaultTrackName = trackList.MakeUniqueTrackName(Au3WaveTrack::GetDefaultAudioTrackNamePreference());
        const wxString baseTrackName = nameOptions.useCustomName && !nameOptions.customName.empty()
                                       ? wxFromStdString(nameOptions.customName)
                                       : defaultTrackName;

        std::vector<WaveTrack::Holder> newTracks;
        if (recordingChannels == 2) {
            newTracks.push_back(WaveTrackFactory::Get(*p).Create(2));
        } else {
            for (int i = 0; i < recordingChannels; ++i) {
                newTracks.push_back(WaveTrackFactory::Get(*p).Create(1));
            }
        }

        std::vector<std::pair<WaveTrack::Holder, Au3WaveTrack::IntervalHolder> > newTrackHolders;

        int trackCounter = 0;
        for (auto& newTrack : newTracks) {
            // Quantize bounds to the rate of the new track.
            if (t0 < DBL_MAX) {
                t0 = newTrack->SnapToSample(t0);
            }
            if (t1 < DBL_MAX) {
                t1 = newTrack->SnapToSample(t1);
            }

            newTrack->MoveTo(t0);
            wxString nameSuffix = wxString(wxT(""));

            if (nameOptions.addTrackNumber) {
                nameSuffix += wxString::Format(wxT("%d"), 1 + (int)numTracks + trackCounter++);
            }

            if (nameOptions.addDateStamp) {
                if (!nameSuffix.empty()) {
                    nameSuffix += wxT("_");
                }
                nameSuffix += wxDateTime::Now().FormatISODate();
            }

            if (nameOptions.addTimeStamp) {
                if (!nameSuffix.empty()) {
                    nameSuffix += wxT("_");
                }
                nameSuffix += wxDateTime::Now().FormatISOTime();
            }

            // ISO standard would be nice, but ":" is unsafe for file name.
            nameSuffix.Replace(wxT(":"), wxT("-"));

            if (baseTrackName.empty()) {
                newTrack->SetName(nameSuffix);
            } else if (nameSuffix.empty()) {
                newTrack->SetName(baseTrackName);
            } else {
                newTrack->SetName(baseTrackName + wxT("_") + nameSuffix);
            }

            //create a new clip with a proper name before recording is started
            auto newClip = insertEmptyInterval(*newTrack, t0, false);

            newTrackHolders.push_back({ newTrack, newClip });
        }

        auto list = TrackList::Create(p);
        for (auto& w : newTracks) {
            list->Add(std::static_pointer_cast<Track>(std::move(w)));
        }
        pendingTracks.RegisterPendingNewTracks(*list);

        for (auto& [newTrack, newClip] : newTrackHolders) {
            auto updater = [this, trackId = newTrack->GetId(), clipId = newClip->GetId()](Au3Track& d, const Au3Track& s){
                assert(d.NChannels() == s.NChannels());
                auto& dst = static_cast<Au3WaveTrack&>(d);
                auto& src = static_cast<const Au3WaveTrack&>(s);
                dst.Init(src);

                audioEngine()->recordingClipChanged().send(trackId, clipId);
            };

            const auto pending = static_cast<Au3WaveTrack*>(
                pendingTracks.RegisterPendingChangedTrack(updater, newTrack.get())
                );
            // Source clip was marked as placeholder so that it would not be
            // skipped in clip copying.  Un-mark it and its copy now
            if (newClip) {
                newClip->SetIsPlaceholder(false);
            }
            if (auto copiedClip = pending->NewestOrNewClip()) {
                copiedClip->SetIsPlaceholder(false);
            }
            transportSequences.captureSequences.push_back(pending->SharedPointer<Au3WaveTrack>());

            m_recordData.push_back(RecordData { trackedit::ClipKey(newTrack->GetId(), newClip->GetId()), newClip->GetId(), false, false });
            rebuildRecordingClipKeys();

            trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
            prj->notifyAboutTrackAdded(DomConverter::track(newTrack.get()));
            prj->notifyAboutClipAdded(DomConverter::clip(newTrack.get(), newClip.get()));
        }
        pendingTracks.UpdatePendingTracks();
    }

    au::audio::IAudioEngine::StartStreamOptions options;
    options.isDefaultPolicy = false;
    options.sampleRate = audioStreamSampleRate;
    options.leadInTime = leadInTime;
    options.crossfadeData = crossfadeData;

    int token = audioEngine()->startStream(transportSequences, t0, t1, t1, project, options);

    success = (token > 0);

    if (!success) {
        cancelRecording();

        Ret ret = make_ret(Err::RecordingError);
        ret.setText(String::fromStdString(ret.text()).arg(audioEngine()->lastErrorString()).toStdString());

        return ret;
    }

    return make_ok();
}

void Au3Record::preserveOverwrittenTakes()
{
    // Before the new recording makes room for itself (overwriting what was under it),
    // keep the overwritten audio of each pass as a muted take lane of the track
    for (const RecordData& recordEntry : m_recordData) {
        if (recordEntry.deferredClipCreation) {
            continue;
        }
        const auto it = m_takeSnapshots.find(recordEntry.clipKey.trackId);
        if (it == m_takeSnapshots.end()) {
            continue;
        }
        Au3WaveTrack* track = DomAccessor::findWaveTrack(projectRef(), ::TrackId(recordEntry.clipKey.trackId));
        std::shared_ptr<Au3WaveClip> clip = track ? DomAccessor::findWaveClip(track, recordEntry.clipKey.itemId) : nullptr;
        if (!clip) {
            continue;
        }
        const double t0 = it->second.t0;
        const double t1 = clip->GetPlayEndTime();
        if (Au3WaveTrack* take = preserveAsTake(projectRef(), *track, *it->second.audio, t0, t1)) {
            if (auto prj = globalContext()->currentTrackeditProject()) {
                auto& tracks = Au3TrackList::Get(projectRef());
                const int pos = static_cast<int>(std::distance(tracks.begin(), tracks.Find(take)));
                prj->notifyAboutTrackInserted(DomConverter::track(take), pos);
            }
        }
        m_takeSnapshots.erase(it);
    }
    m_takeSnapshots.clear();
}

bool Au3Record::canSwitchTake(trackedit::TrackId trackId, TakeSwitch kind) const
{
    Au3WaveTrack* track = DomAccessor::findWaveTrack(projectRef(), ::TrackId(trackId));
    if (!track) {
        return false;
    }
    Au3WaveTrack* main = TakeLaneInfo::Get(*track).isTake() ? mainOf(projectRef(), *track) : track;
    if (!main) {
        return false;
    }
    if (kind == TakeSwitch::Activate) {
        return track != main && !track->Intervals().empty();
    }
    if (kind == TakeSwitch::CompSelection) {
        return track != main
               && selectionController()->dataSelectedEndTime() > selectionController()->dataSelectedStartTime();
    }
    const auto takes = takesOf(projectRef(), *main);
    return std::any_of(takes.begin(), takes.end(), [](Au3WaveTrack* t) { return !t->Intervals().empty(); });
}

muse::Ret Au3Record::switchTake(trackedit::TrackId trackId, TakeSwitch kind)
{
    if (!canSwitchTake(trackId, kind)) {
        return make_ret(Ret::Code::NotSupported);
    }
    Au3Project& project = projectRef();
    Au3WaveTrack* track = DomAccessor::findWaveTrack(project, ::TrackId(trackId));
    Au3WaveTrack* main = TakeLaneInfo::Get(*track).isTake() ? mainOf(project, *track) : track;

    bool done = false;
    std::string keySummary;
    switch (kind) {
    case TakeSwitch::Activate:
        done = activateTake(project, *main, *track);
        break;
    case TakeSwitch::CompSelection:
        done = compRange(project, *main, *track,
                         selectionController()->dataSelectedStartTime(),
                         selectionController()->dataSelectedEndTime(),
                         std::max(0.0, recordConfiguration()->crossfadeDuration() / 1000.0));
        break;
    case TakeSwitch::AutoComp:
        done = !autoComp(project, *main, recordConfiguration()->autoCompSegmentLength(),
                         std::max(0.0, recordConfiguration()->crossfadeDuration() / 1000.0),
                         recordConfiguration()->autoCompPitchScoring(), &keySummary).empty();
        break;
    case TakeSwitch::Next:
    case TakeSwitch::Previous:
        done = cycleTakes(project, *main, kind == TakeSwitch::Next);
        break;
    }
    if (!done) {
        return make_ret(Ret::Code::InternalError);
    }

    if (auto prj = globalContext()->currentTrackeditProject()) {
        prj->notifyAboutTrackClipListChanged(DomConverter::track(main));
        for (Au3WaveTrack* take : takesOf(project, *main)) {
            prj->notifyAboutTrackClipListChanged(DomConverter::track(take));
        }
    }

    if (kind == TakeSwitch::AutoComp) {
        m_lastAutoCompKey = keySummary;
        projectHistory()->pushHistoryState(muse::qtrc("record", "Auto-comped takes (intonation: %1)").arg(QString::fromStdString(keySummary)).toStdString(),
                                           /*: Undo history entry name; shown after Undo and Redo in the Edit menu */
                                           trc("record", "Auto-comp takes"));
    } else if (kind == TakeSwitch::CompSelection) {
        projectHistory()->pushHistoryState(trc("record", "Comped selection from take"),
                                           /*: Undo history entry name; shown after Undo and Redo in the Edit menu */
                                           trc("record", "Comp take"));
    } else {
        projectHistory()->pushHistoryState(trc("record", "Switched take"),
                                           /*: Undo history entry name; shown after Undo and Redo in the Edit menu */
                                           trc("record", "Switch take"));
    }
    return make_ok();
}

KeyDetection Au3Record::detectKey(trackedit::TrackId trackId, double t0, double t1, bool store)
{
    KeyDetection out;
    Au3Project& project = projectRef();
    const Au3WaveTrack* track = DomAccessor::findWaveTrack(project, ::TrackId(trackId));
    if (!track) {
        return out;
    }
    const auto r = takelanes::detectKey(*track, t0, t1);
    out.valid = r.valid;
    out.tuningCents = r.tuningCents;
    out.tuningHz = r.tuningHz;
    if (!r.valid) {
        return out;
    }
    out.keyName = r.keyName();
    out.tonic = r.key.tonic;
    out.minor = r.key.minor;
    out.correlation = r.key.correlation;
    out.confidence = r.confidence;
    out.scaleConfidence = r.scaleConfidence;
    out.fromPitchTrack = r.usedPitchTrack;
    for (const auto& a : r.alternatives) {
        out.alternatives.emplace_back(keydetect::pitchClassName(a.tonic) + (a.minor ? " minor" : " major"), a.correlation);
    }
    if (store) {
        setProjectKey(ProjectKeyInfo { r.key.tonic, r.key.minor, r.tuningCents });
    }
    return out;
}

std::optional<ProjectKeyInfo> Au3Record::projectKey() const
{
    const auto& k = ProjectKey::Get(projectRef()).key();
    if (!k) {
        return std::nullopt;
    }
    return ProjectKeyInfo { k->tonic, k->minor, k->tuningCents };
}

muse::Ret Au3Record::setProjectKey(const std::optional<ProjectKeyInfo>& key)
{
    if (key && (key->tonic < 0 || key->tonic > 11 || std::fabs(key->tuningCents) > 100.0)) {
        return make_ret(Ret::Code::InternalError);
    }
    if (projectKey() == key) {
        return make_ok();
    }
    std::optional<ProjectKey::Key> k;
    if (key) {
        k = ProjectKey::Key { key->tonic, key->minor, key->tuningCents, 1.0 };
    }
    ProjectKey::Get(projectRef()).setKey(k);
    if (key) {
        const std::string label = ProjectKey::shortLabel(k);
        projectHistory()->pushHistoryState(muse::qtrc("record", "Set project key to %1").arg(QString::fromStdString(label)).toStdString(),
                                           /*: Undo history entry name; shown after Undo and Redo in the Edit menu */
                                           trc("record", "Set project key"));
    } else {
        projectHistory()->pushHistoryState(trc("record", "Cleared project key"),
                                           /*: Undo history entry name; shown after Undo and Redo in the Edit menu */
                                           trc("record", "Clear project key"));
    }
    return make_ok();
}

muse::Ret Au3Record::setClipFades(const trackedit::ClipKey& key, double fadeIn, double fadeOut, int curve)
{
    Au3Project& project = projectRef();
    Au3WaveTrack* track = DomAccessor::findWaveTrack(project, ::TrackId(key.trackId));
    if (!track) {
        return make_ret(Ret::Code::InternalError);
    }
    auto clip = DomAccessor::findWaveClip(track, key.itemId);
    if (!clip || fadeIn < 0 || fadeOut < 0) {
        return make_ret(Ret::Code::InternalError);
    }
    // Fades never overlap each other within a clip
    const double len = clip->GetPlayDuration();
    fadeIn = std::min(fadeIn, len);
    fadeOut = std::min(fadeOut, len - fadeIn);
    if (clip->GetFadeInDuration() == fadeIn && clip->GetFadeOutDuration() == fadeOut && clip->GetFadeCurve() == curve) {
        return make_ok();
    }
    clip->SetFadeInDuration(fadeIn);
    clip->SetFadeOutDuration(fadeOut);
    clip->SetFadeCurve(curve);
    if (trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject()) {
        prj->notifyAboutClipChanged(DomConverter::clip(track, clip.get()));
    }
    projectHistory()->pushHistoryState(trc("record", "Changed clip fades"),
                                       /*: Undo history entry name; shown after Undo and Redo in the Edit menu */
                                       trc("record", "Clip fades"));
    return make_ok();
}

namespace {
constexpr double kSliceFade = 0.003;    // crossfade at slice points
constexpr double kMinPiece = 0.005;     // never make a slice shorter than this
}

std::vector<double> Au3Record::detectOnsetTimes(Au3WaveTrack* track, const WaveClip& clip, double sensitivity) const
{
    const double t0 = clip.GetPlayStartTime(), t1 = clip.GetPlayEndTime();
    const auto s0 = track->TimeToLongSamples(t0);
    const size_t len = size_t((track->TimeToLongSamples(t1) - s0).as_long_long());
    if (len == 0) {
        return {};
    }
    const size_t nch = track->NChannels();
    std::vector<float> mono(len, 0.0f), buf(len);
    for (size_t c = 0; c < nch; ++c) {
        float* b[] { buf.data() };
        track->GetFloats(c, 1, b, s0, len, false, FillFormat::fillZero, false);
        for (size_t i = 0; i < len; ++i) {
            mono[i] += buf[i] / float(nch);
        }
    }
    ::au::au3::transients::Settings st;
    st.sensitivity = sensitivity;
    std::vector<double> out;
    const double rate = track->GetRate();
    for (const auto& o : ::au::au3::transients::detect(mono.data(), len, rate, st)) {
        out.push_back(t0 + o.time * clip.GetStretchRatio());
    }
    return out;
}

static double projectGridStep(const Au3Project& project, int division)
{
    const auto& sig = ProjectTimeSignature::Get(project);
    return ::au::au3::transients::gridStep(sig.GetTempo(), sig.GetUpperTimeSignature(), sig.GetLowerTimeSignature(),
                                     static_cast<::au::au3::transients::Division>(std::clamp(division, 0, 3)));
}

muse::Ret Au3Record::detectTransients(const trackedit::ClipKey& key, double sensitivity, bool toLabelTrack)
{
    Au3Project& project = projectRef();
    Au3WaveTrack* track = DomAccessor::findWaveTrack(project, ::TrackId(key.trackId));
    auto clip = track ? DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip) {
        return make_ret(Ret::Code::InternalError);
    }
    const auto times = detectOnsetTimes(track, *clip, sensitivity);
    std::vector<double> src;
    for (double t : times) {
        src.push_back((t - clip->GetSequenceStartTime()) / clip->GetStretchRatio());
    }
    // keep markers of hidden (trimmed) parts, replace those in the visible part
    const double a = (clip->GetPlayStartTime() - clip->GetSequenceStartTime()) / clip->GetStretchRatio();
    const double b = (clip->GetPlayEndTime() - clip->GetSequenceStartTime()) / clip->GetStretchRatio();
    for (double t : clip->GetTransients()) {
        if (t < a - 1e-9 || t >= b) {
            src.push_back(t);
        }
    }
    std::sort(src.begin(), src.end());
    clip->SetTransients(src);

    trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
    if (prj) {
        prj->notifyAboutClipChanged(DomConverter::clip(track, clip.get()));
    }
    if (toLabelTrack && !times.empty()) {
        auto& tracks = ::TrackList::Get(project);
        auto* lt = ::LabelTrack::Create(tracks);
        lt->SetName(wxString::FromUTF8(trc("record", "Transients")));
        if (prj) {
            prj->notifyAboutTrackAdded(DomConverter::labelTrack(lt));
        }
        for (size_t i = 0; i < times.size(); ++i) {
            SelectedRegion r(times[i], times[i]);
            const int64_t id = lt->AddLabel(r, wxString::Format("%d", int(i + 1)));
            if (prj) {
                prj->notifyAboutLabelAdded(DomConverter::label(lt, DomAccessor::findLabel(lt, id)));
            }
        }
    }
    projectHistory()->pushHistoryState(trc("record", "Detected transients"),
                                       /*: Undo history entry name; shown after Undo and Redo in the Edit menu */
                                       trc("record", "Detect transients"));
    return make_ok();
}

muse::Ret Au3Record::clearTransients(const trackedit::ClipKey& key)
{
    Au3Project& project = projectRef();
    Au3WaveTrack* track = DomAccessor::findWaveTrack(project, ::TrackId(key.trackId));
    auto clip = track ? DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip) {
        return make_ret(Ret::Code::InternalError);
    }
    if (clip->GetTransients().empty()) {
        return make_ok();
    }
    clip->SetTransients({});
    if (auto prj = globalContext()->currentTrackeditProject()) {
        prj->notifyAboutClipChanged(DomConverter::clip(track, clip.get()));
    }
    projectHistory()->pushHistoryState(trc("record", "Cleared transient markers"),
                                       /*: Undo history entry name; shown after Undo and Redo in the Edit menu */
                                       trc("record", "Clear transients"));
    return make_ok();
}

size_t Au3Record::sliceClipAt(Au3WaveTrack* track, std::shared_ptr<WaveClip> clip, std::vector<double> cuts,
                              double strength, double step)
{
    const double c0 = clip->GetPlayStartTime(), c1 = clip->GetPlayEndTime();
    std::sort(cuts.begin(), cuts.end());
    std::vector<double> use;
    for (double t : cuts) {
        t = track->SnapToSample(t);
        if (t > c0 + kMinPiece && t < c1 - kMinPiece && (use.empty() || t > use.back() + kMinPiece)) {
            use.push_back(t);
        }
    }
    if (use.empty()) {
        return 0;
    }
    auto doSplit = [&] {
        for (double t : use) {
            track->SplitAt(t);
        }
        // the pieces, left to right
        std::vector<std::shared_ptr<WaveClip> > pieces;
        for (const auto& iv : track->SortedIntervalArray()) {
            if (iv->GetPlayStartTime() >= c0 - 1e-9 && iv->GetPlayEndTime() <= c1 + 1e-9) {
                pieces.push_back(iv);
            }
        }
        // quantize: move each slice (not the first) toward its grid line, keeping pieces contiguous
        if (strength > 0 && step > 0) {
            for (size_t i = 1; i < pieces.size(); ++i) {
                auto& prev = pieces[i - 1];
                auto& cur = pieces[i];
                const double from = cur->GetPlayStartTime();
                double to = track->SnapToSample(::au::au3::transients::quantize(from, step, strength));
                to = std::max(to, prev->GetPlayStartTime() + kMinPiece);
                if (std::abs(to - from) < 1e-9) {
                    continue;
                }
                // the left neighbour ends where this slice now starts: trimmed when it moves left,
                // extended into its hidden handle when it moves right. The slice itself moves with
                // its audio; its own end is fixed up the same way when the next slice is placed.
                prev->TrimRightTo(to);
                cur->ShiftBy(to - from);
            }
        }
        // short fade-in at every cut: crossfades with the left neighbour's hidden handle (same lineage)
        for (size_t i = 1; i < pieces.size(); ++i) {
            pieces[i]->SetFadeInDuration(std::min(kSliceFade, pieces[i]->GetPlayDuration() / 2));
            pieces[i]->SetFadeCurve(0);
        }
        return pieces.size();
    };
    size_t n = 0;
    if (auto prj = globalContext()->currentTrackeditProject()) {
        const trackedit::TrackId tid = track->GetId();
        const auto before = prj->clipList(tid);
        n = doSplit();
        const auto after = prj->clipList(tid);
        const auto contains = [](const trackedit::Clips& l, const trackedit::Clip& c) {
            return std::any_of(l.begin(), l.end(), [&](const trackedit::Clip& o) { return o.key == c.key; });
        };
        for (const auto& c : before) {
            if (!contains(after, c)) {
                prj->notifyAboutClipRemoved(c);
            }
        }
        for (const auto& c : after) {
            if (!contains(before, c)) {
                prj->notifyAboutClipAdded(c);
            } else {
                prj->notifyAboutClipChanged(c);
            }
        }
    } else {
        n = doSplit();
    }
    return n;
}

muse::Ret Au3Record::sliceAtTransients(const trackedit::ClipKey& key, double strength, int division)
{
    Au3Project& project = projectRef();
    Au3WaveTrack* track = DomAccessor::findWaveTrack(project, ::TrackId(key.trackId));
    auto clip = track ? DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip) {
        return make_ret(Ret::Code::InternalError);
    }
    std::vector<double> cuts = clip->GetVisibleTransientTimes();
    if (cuts.empty()) {
        cuts = detectOnsetTimes(track, *clip, 50.0);
        std::vector<double> src;
        for (double t : cuts) {
            src.push_back((t - clip->GetSequenceStartTime()) / clip->GetStretchRatio());
        }
        clip->SetTransients(src);
    }
    if (sliceClipAt(track, clip, cuts, std::clamp(strength, 0.0, 1.0), projectGridStep(project, division)) == 0) {
        return make_ok();
    }
    projectHistory()->pushHistoryState(strength > 0 ? trc("record", "Sliced and quantized at transients")
                                       : trc("record", "Sliced at transients"),
                                       /*: Undo history entry name; shown after Undo and Redo in the Edit menu */
                                       trc("record", "Slice at transients"));
    return make_ok();
}

muse::Ret Au3Record::sliceToGrid(const trackedit::ClipKey& key, int division)
{
    Au3Project& project = projectRef();
    Au3WaveTrack* track = DomAccessor::findWaveTrack(project, ::TrackId(key.trackId));
    auto clip = track ? DomAccessor::findWaveClip(track, key.itemId) : nullptr;
    if (!clip) {
        return make_ret(Ret::Code::InternalError);
    }
    const double step = projectGridStep(project, division);
    const auto cuts = ::au::au3::transients::gridLines(clip->GetPlayStartTime(), clip->GetPlayEndTime(), step);
    if (sliceClipAt(track, clip, cuts, 0.0, 0.0) == 0) {
        return make_ok();
    }
    projectHistory()->pushHistoryState(trc("record", "Sliced to grid"),
                                       /*: Undo history entry name; shown after Undo and Redo in the Edit menu */
                                       trc("record", "Slice to grid"));
    return make_ok();
}

muse::async::Notification Au3Record::projectKeyChanged() const
{
    return ProjectKey::Get(projectRef()).keyChanged();
}

std::string Au3Record::projectKeyLabel(const std::optional<ProjectKeyInfo>& key) const
{
    if (!key) {
        return trc("record", "No key");
    }
    return ProjectKey::shortLabel(ProjectKey::Key { key->tonic, key->minor, key->tuningCents, 1.0 });
}

void Au3Record::cancelRecording()
{
    m_takeSnapshots.clear();
    Au3Project& project = projectRef();
    PendingTracks::Get(project).ClearPendingTracks();
}

void Au3Record::commitRecording()
{
    projectHistory()->pushHistoryState(trc("record", "Recorded audio"),
                                       /*: Undo history entry name; shown after Undo and Redo in the Edit menu */
                                       trc("record", "Record"));
}

bool Au3Record::canStopAudioStream() const
{
    return audioEngine()->canStopAudioStream(projectRef());
}

au::context::IPlaybackStatePtr Au3Record::playbackState() const
{
    return globalContext()->playbackState();
}
