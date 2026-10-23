/*
* Audacity: A Digital Audio Editor
*/
#include "gainstagingmodel.h"

#include "au3wrap/internal/gainstaging.h"

using namespace au::automation;
namespace gs = au::au3::gainstaging;

GainStagingModel::GainStagingModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

double GainStagingModel::defaultTarget(int kind)
{
    switch (kind) {
    case Lufs: return -23.0;
    case Peak: return -1.0;
    default: return -18.0;
    }
}

void GainStagingModel::setTargetKind(int kind)
{
    kind = std::clamp(kind, int(Rms), int(Peak));
    if (kind == m_kind) {
        return;
    }
    m_kind = kind;
    m_target = defaultTarget(kind);
    emit targetChanged();
    rebuild();
}

void GainStagingModel::setTarget(double target)
{
    target = std::clamp(target, -60.0, 0.0);
    if (target == m_target) {
        return;
    }
    m_target = target;
    emit targetChanged();
    rebuild();
}

void GainStagingModel::analyze()
{
    m_levels = gainStaging() ? gainStaging()->analyze() : std::vector<TrackLevels> {};
    rebuild();
}

void GainStagingModel::setUse(int row, bool use)
{
    if (row < 0 || size_t(row) >= m_levels.size()) {
        return;
    }
    m_use[m_levels[size_t(row)].trackId] = use;
    rebuild();
}

void GainStagingModel::rebuild()
{
    m_rows.clear();
    const auto kind = m_kind == Lufs ? gs::TargetKind::Lufs : m_kind == Peak ? gs::TargetKind::Peak : gs::TargetKind::Rms;
    for (const auto& t : m_levels) {
        gs::Levels l;
        l.peakDb = t.peakDb;
        l.truePeakDb = t.truePeakDb;
        l.rmsDb = t.rmsDb;
        l.lufs = t.lufs;
        l.silent = t.silent;
        const auto suggested = gs::suggestTrimDb(l, kind, m_target);
        const auto useIt = m_use.find(t.trackId);
        QVariantMap row;
        row["trackId"] = qlonglong(t.trackId);
        row["name"] = QString::fromStdString(t.name);
        row["channels"] = t.channels;
        row["peakDb"] = t.peakDb;
        row["truePeakDb"] = t.truePeakDb;
        row["rmsDb"] = t.rmsDb;
        row["lufs"] = t.lufs;
        row["silent"] = t.silent;
        row["trimDb"] = t.trimDb;
        row["hasSuggestion"] = suggested.has_value();
        row["suggestedDb"] = suggested.value_or(0.0);
        // true peak after the trim (the trim is applied to the audio as measured)
        row["truePeakAfterDb"] = t.truePeakDb + suggested.value_or(t.trimDb);
        row["overAfter"] = suggested && t.truePeakDb + *suggested > 0.0;
        row["use"] = suggested.has_value() && (useIt == m_use.end() || useIt->second);
        m_rows << row;
    }
    emit rowsChanged();
}

int GainStagingModel::applicableCount() const
{
    int n = 0;
    for (const auto& r : m_rows) {
        n += r.toMap().value("use").toBool() ? 1 : 0;
    }
    return n;
}

int GainStagingModel::apply()
{
    if (!gainStaging()) {
        return 0;
    }
    std::map<trackedit::TrackId, double> trims;
    for (const auto& r : m_rows) {
        const auto m = r.toMap();
        if (m.value("use").toBool()) {
            trims[m.value("trackId").toLongLong()] = m.value("suggestedDb").toDouble();
        }
    }
    const int changed = trims.empty() ? 0 : gainStaging()->applyTrims(trims);
    for (auto& t : m_levels) {
        if (const auto it = trims.find(t.trackId); it != trims.end()) {
            t.trimDb = it->second;
        }
    }
    rebuild();
    return changed;
}
