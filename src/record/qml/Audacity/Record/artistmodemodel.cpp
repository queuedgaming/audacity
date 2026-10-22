/*
* Audacity: A Digital Audio Editor
*/
#include "artistmodemodel.h"

#include <chrono>
#include <cmath>

#include "context/iplaybackstate.h"
#include "record/iaudioinput.h"

using namespace au::record;
using namespace muse;

ArtistModeModel::ArtistModeModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

static double nowSeconds()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void ArtistModeModel::init()
{
    configuration()->artistModeEnabledChanged().onNotify(this, [this]() { emit activeChanged(); });
    for (auto n : { configuration()->metronomeEnabledChanged(), configuration()->countInEnabledChanged(),
                    configuration()->punchOutAtSelectionEndChanged(), configuration()->keepTakesWhenRecordingOverChanged(),
                    configuration()->leadInTimeDurationChanged() }) {
        n.onNotify(this, [this]() { emit settingsChanged(); });
    }

    if (recordController()) {
        recordController()->isRecordingChanged().onNotify(this, [this]() {
            if (isRecording()) {
                // a new take: fresh peak hold and clip indicator
                m_peakDb = -144.0;
                m_clipped = false;
                emit levelChanged();
            }
            emit transportChanged();
        });
    }
    if (record()) {
        record()->recordPositionChanged().onReceive(this, [this](const muse::secs_t& t) { setPosition(t); });
        record()->recordingFinished().onNotify(this, [this]() { onRecordingFinished(); });
        if (auto input = record()->audioInput()) {
            input->recordSignalChanges().onReceive(this, [this](audio::audioch_t, const audio::MeterSignal& s) {
                onMeterSignal(s.peak.pressure, nowSeconds());
            });
        }
    }
    if (auto state = globalContext()->playbackState()) {
        state->playbackStatusChanged().onReceive(this, [this](playback::PlaybackStatus) { emit transportChanged(); });
        state->playbackPositionChanged().onReceive(this, [this](const muse::secs_t& t) {
            if (!isRecording()) {
                setPosition(t);
            }
        });
    }
    const auto onProject = [this]() {
        if (auto prj = globalContext()->currentTrackeditProject()) {
            prj->timeSignatureChanged().onReceive(this, [this](const trackedit::TimeSignature&) { emit projectInfoChanged(); });
            prj->tracksChanged().onReceive(this, [this](const std::vector<trackedit::Track>&) { updateTrack(); });
            // the project key lives in the project: only reachable once one is open
            record()->projectKeyChanged().onNotify(this, [this]() { emit projectInfoChanged(); });
        }
        updateTrack();
        emit projectInfoChanged();
    };
    globalContext()->currentTrackeditProjectChanged().onNotify(this, onProject);
    if (selectionController()) {
        selectionController()->tracksSelected().onReceive(this, [this](const trackedit::TrackIdList&) { updateTrack(); });
    }
    onProject();
}

bool ArtistModeModel::active() const
{
    return configuration()->artistModeEnabled();
}

bool ArtistModeModel::isRecording() const
{
    return recordController() && recordController()->isRecording();
}

bool ArtistModeModel::isPlaying() const
{
    auto state = globalContext()->playbackState();
    return state && state->isPlaying();
}

QString ArtistModeModel::formatPosition(double secs)
{
    secs = std::max(0.0, secs);
    const int tenths = int(std::floor(secs * 10.0 + 1e-6));
    return QString("%1:%2.%3").arg(tenths / 600, 2, 10, QChar('0')).arg((tenths / 10) % 60, 2, 10, QChar('0')).arg(tenths % 10);
}

QString ArtistModeModel::positionText() const
{
    return formatPosition(m_position);
}

void ArtistModeModel::setPosition(double secs)
{
    if (formatPosition(secs) == formatPosition(m_position)) {
        m_position = secs;
        return;
    }
    m_position = secs;
    emit positionChanged();
}

au::trackedit::TrackId ArtistModeModel::armedTrack() const
{
    // AU4 records onto the selected tracks (or a new one): the first selected track is the one we show
    if (selectionController()) {
        const auto sel = selectionController()->selectedTracks();
        if (!sel.empty()) {
            return sel.front();
        }
    }
    return trackedit::TrackId(-1);
}

void ArtistModeModel::updateTrack()
{
    QString name;
    int takes = 0;
    const auto id = armedTrack();
    auto prj = globalContext()->currentTrackeditProject();
    if (prj && id != trackedit::TrackId(-1)) {
        name = QString::fromStdString(prj->trackName(id).value_or(std::string()));
        takes = record()->takeCount(id);
    }
    if (name != m_trackName || takes != m_takeLaneCount) {
        m_trackName = name;
        m_takeLaneCount = takes;
        emit trackChanged();
    }
}

QString ArtistModeModel::tempoText() const
{
    auto prj = globalContext()->currentTrackeditProject();
    if (!prj) {
        return "--";
    }
    return QString::number(prj->timeSignature().tempo, 'g', 4) + " BPM";
}

QString ArtistModeModel::timeSignatureText() const
{
    auto prj = globalContext()->currentTrackeditProject();
    if (!prj) {
        return "--";
    }
    const auto sig = prj->timeSignature();
    return QString("%1/%2").arg(sig.upper).arg(sig.lower);
}

QString ArtistModeModel::keyText() const
{
    if (!record() || !globalContext()->currentTrackeditProject()) {
        return "--";
    }
    return QString::fromStdString(record()->projectKeyLabel(record()->projectKey()));
}

bool ArtistModeModel::metronome() const { return configuration()->metronomeEnabled(); }
void ArtistModeModel::setMetronome(bool on) { configuration()->setMetronomeEnabled(on); }
bool ArtistModeModel::countIn() const { return configuration()->countInEnabled(); }
void ArtistModeModel::setCountIn(bool on) { configuration()->setCountInEnabled(on); }
double ArtistModeModel::countInSeconds() const { return configuration()->leadInTimeDuration(); }
bool ArtistModeModel::punchOut() const { return configuration()->punchOutAtSelectionEnd(); }
void ArtistModeModel::setPunchOut(bool on) { configuration()->setPunchOutAtSelectionEnd(on); }
bool ArtistModeModel::keepTakes() const { return configuration()->keepTakesWhenRecordingOver(); }
void ArtistModeModel::setKeepTakes(bool on) { configuration()->setKeepTakesWhenRecordingOver(on); }

void ArtistModeModel::onMeterSignal(double peakDb, double nowSec)
{
    peakDb = std::clamp(peakDb, -144.0, 6.0);
    m_levelDb = peakDb;
    if (peakDb >= m_peakDb || nowSec - m_peakAt > PEAK_HOLD_SECONDS) {
        m_peakDb = peakDb;
        m_peakAt = nowSec;
    }
    if (peakDb >= CLIP_DB) {
        m_clipped = true;
    }
    emit levelChanged();
}

void ArtistModeModel::resetClipIndicator()
{
    m_clipped = false;
    m_peakDb = m_levelDb;
    emit levelChanged();
}

void ArtistModeModel::onRecordingFinished()
{
    const auto& keys = record()->recordingClipKeys();
    std::optional<TakeStats> total;
    for (const auto& key : keys) {
        if (const auto st = record()->clipStats(key)) {
            if (!total) {
                total = *st;
            } else {
                // several tracks recorded at once: longest duration, loudest peak, any clipping
                total->duration = std::max(total->duration, st->duration);
                total->peakDb = std::max(total->peakDb, st->peakDb);
                total->clippedSamples += st->clippedSamples;
                total->clipped = total->clipped || st->clipped;
            }
        }
    }
    if (total) {
        m_lastTake = total;
        emit lastTakeChanged();
    }
    updateTrack();
}

QString ArtistModeModel::lastTakeText() const
{
    if (!m_lastTake) {
        return tr("No take recorded yet");
    }
    const QString peak = m_lastTake->peakDb <= -144.0 ? QString("-inf") : QString::number(m_lastTake->peakDb, 'f', 1);
    return tr("Last take: %1 s, peak %2 dBFS, %3")
           .arg(QString::number(m_lastTake->duration, 'f', 1), peak,
                m_lastTake->clipped ? tr("CLIPPED (%1 samples)").arg(m_lastTake->clippedSamples) : tr("no clipping"));
}

void ArtistModeModel::dispatch(const char* action)
{
    dispatcher()->dispatch(muse::actions::ActionQuery(action));
}

void ArtistModeModel::toggleRecord()
{
    if (isRecording()) {
        dispatch("action://record/stop");
    } else if (countIn()) {
        // count-in = lead-in recording: the transport rolls leadInTimeDuration before the record point
        dispatch("action://record/lead-in-recording");
    } else {
        dispatch("action://record/start");
    }
}

void ArtistModeModel::stop()
{
    dispatch(isRecording() ? "action://record/stop" : "action://playback/stop");
}

void ArtistModeModel::togglePlay()
{
    dispatch("action://playback/toggle-play-stop");
}

void ArtistModeModel::nextTake() { dispatch("action://record/next-take"); }
void ArtistModeModel::activateTake() { dispatch("action://record/activate-take"); }
void ArtistModeModel::autoComp() { dispatch("action://record/auto-comp-takes"); }

void ArtistModeModel::exitMode()
{
    configuration()->setArtistModeEnabled(false);
}
