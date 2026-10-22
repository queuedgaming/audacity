/*
* Audacity: A Digital Audio Editor
*/
#include "clipcontextmenumodel.h"
#include "fadehandles.h"

#include <cmath>

#include "framework/global/translation.h"
#include "framework/global/realfn.h"
#include "framework/ui/view/iconcodes.h"

#include "spectrogram/spectrogramcommands.h"
#include "trackedit/dom/track.h"

using namespace au::projectscene;
using namespace muse::uicomponents;
using namespace muse::actions;

static const ActionCode ENABLE_STRETCH_CODE("stretch-clip-to-match-tempo");
static const ActionCode RENDER_PITCH_SPEED_CODE("clip-render-pitch-speed");
static const ActionCode RESET_PITCH_SPEED_CODE("clip-reset-pitch-speed");
static const ActionCodeList PITCH_SPEED_CODES { RENDER_PITCH_SPEED_CODE, RESET_PITCH_SPEED_CODE };

namespace {
//! NOTE: can be moved to the framework
bool containsAny(const ActionCodeList& list, const ActionCodeList& actionCodes)
{
    return std::any_of(actionCodes.begin(), actionCodes.end(), [&](ActionCode code) {
        return std::find(list.begin(), list.end(), code) != list.end();
    });
}
}

void ClipContextMenuModel::load()
{
    AbstractMenuModel::load();

    auto makeItemWithArg = [this](const ActionCode& actionCode, const muse::TranslatableString& title = {}) {
        MenuItem* item = makeMenuItem(actionCode);
        item->setArgs(ActionData::make_arg1<trackedit::ClipKey>(m_clipKey.key));
        if (!title.isEmpty()) {
            item->setTitle(title);
        }
        return item;
    };

    auto enableStretchItem = makeItemWithArg(ENABLE_STRETCH_CODE);
    updateStretchEnabledState(*enableStretchItem);

    auto renderPitchSpeedItem = makeItemWithArg(RENDER_PITCH_SPEED_CODE);
    updatePitchSpeedModifiedEnabledState(*renderPitchSpeedItem);

    auto resetPitchSpeedItem = makeItemWithArg(RESET_PITCH_SPEED_CODE);
    updatePitchSpeedModifiedEnabledState(*resetPitchSpeedItem);

    auto colorItems = makeClipColourItems();

    MenuItemList cutAndItems {
        makeMenuItem("cut-leave-gap",
                     muse::TranslatableString("clip", "Cut and leave gap")),
        makeMenuItem("cut-per-track-ripple",
                     muse::TranslatableString("clip", "Cut and close gap on this track")),
        makeMenuItem("cut-all-tracks-ripple",
                     muse::TranslatableString("clip", "Cut and close gap on all tracks")),
    };

    MenuItemList deleteAndItems {
        makeMenuItem("delete-leave-gap",
                     muse::TranslatableString("clip", "Delete and leave gap")),
        makeMenuItem("delete-per-track-ripple",
                     muse::TranslatableString("clip", "Delete and close gap on this track")),
        makeMenuItem("delete-all-tracks-ripple",
                     muse::TranslatableString("clip", "Delete and close gap on all tracks")),
    };

    MenuItemList items {
        makeItemWithArg("clip-properties"),
        makeItemWithArg("rename-item", muse::TranslatableString("clip", "Rename clip")),
        makeMenu(muse::TranslatableString("clip", "Clip color"), colorItems, "colorMenu"),
        makeSeparator(),
        makeItemWithArg("action://trackedit/cut"),
        makeItemWithArg("action://trackedit/copy"),
        makeItemWithArg("duplicate"),
        makeItemWithArg("action://trackedit/delete"),
        makeSeparator(),
        makeMenu(muse::TranslatableString("clip", "Cut and…"), cutAndItems, "menu-cut-and"),
        makeMenu(muse::TranslatableString("clip", "Delete and…"), deleteAndItems, "menu-delete-and"),
        makeSeparator(),
        makeItemWithArg("split"),
        makeItemWithArg("disjoin", muse::TranslatableString("clip", "Split at silences")),
        makeSeparator(),
        enableStretchItem,
        makeItemWithArg("clip-pitch-speed-open"),
        resetPitchSpeedItem,
        renderPitchSpeedItem,
        makeSeparator(),
        makeFadesMenu(),
        makeTransientsMenu(),
    };

    const auto project = globalContext()->currentProject();
    if (project && project->viewState()) {
        muse::ValCh<trackedit::TrackViewType> valCh = project->viewState()->trackViewType(m_clipKey.trackId());

        if (valCh.val == trackedit::TrackViewType::Spectrogram || valCh.val == trackedit::TrackViewType::WaveformAndSpectrogram) {
            items.push_back(makeSeparator());
            items.push_back(makeMenuItem(muse::rcommand::make_query(spectrogram::TRACK_SPECTROGRAM_SETTINGS_COMMAND, {
                { spectrogram::TRACK_SPECTROGRAM_SETTINGS_TRACK_ID_PARAM, muse::Val(static_cast<int>(m_clipKey.trackId())) }
            })));
        }

        valCh.ch.onReceive(this, [this](auto) { load(); }, Mode::SetReplace);
    }

    trackedit::ITrackeditProjectPtr prj = globalContext()->currentTrackeditProject();
    if (prj) {
        prj->clipList(m_clipKey.trackId()).onItemChanged(this, [this](const trackedit::Clip& clip) {
            if (clip.key == m_clipKey.key) {
                load();
            }
        }, muse::async::Asyncable::Mode::SetReplace);

        prj->trackChanged().onReceive(this, [this](const trackedit::Track& track) {
            if (track.id == m_clipKey.trackId()) {
                updateColorCheckedState();
            }
        }, muse::async::Asyncable::Mode::SetReplace);
    }

    setItems(items);

    updateColorCheckedState();
    updateColorMenu();
}

ClipKey ClipContextMenuModel::clipKey() const
{
    return m_clipKey;
}

void ClipContextMenuModel::setClipKey(const ClipKey& newClipKey)
{
    m_clipKey = newClipKey;
    emit clipKeyChanged();
}

void ClipContextMenuModel::onActionsStateChanges(const muse::actions::ActionCodeList& codes)
{
    AbstractMenuModel::onActionsStateChanges(codes);

    if (containsAction(codes, ENABLE_STRETCH_CODE)) {
        MenuItem& item = findItem(ActionCode(ENABLE_STRETCH_CODE));
        updateStretchEnabledState(item);
    }

    if (containsAny(codes, PITCH_SPEED_CODES)) {
        for (const auto& code : PITCH_SPEED_CODES) {
            MenuItem& item = findItem(ActionCode(code));
            updatePitchSpeedModifiedEnabledState(item);
        }
    }

    if (containsAny(codes, m_colorChangeActionCodeList)) {
        updateColorCheckedState();
    }
}

void ClipContextMenuModel::updateStretchEnabledState(MenuItem& item)
{
    project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    auto clip = project->trackeditProject()->clip(m_clipKey.key);
    if (!clip.isValid()) {
        return;
    }
    auto state = item.state();
    state.checked = clip.stretchToMatchTempo;
    item.setState(state);
}

void ClipContextMenuModel::updatePitchSpeedModifiedEnabledState(MenuItem& item)
{
    project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    auto clip = project->trackeditProject()->clip(m_clipKey.key);
    if (!clip.isValid()) {
        return;
    }

    const bool hasPitchOrSpeed = clip.pitch != 0 || !muse::RealIsEqual(clip.speed, 1.0);
    auto state = item.state();
    state.enabled = state.enabled && hasPitchOrSpeed;
    item.setState(state);
}

void ClipContextMenuModel::updateColorCheckedState()
{
    project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    auto clip = project->trackeditProject()->clip(m_clipKey.key);
    if (!clip.isValid()) {
        return;
    }

    const auto trackOpt = project->trackeditProject()->track(m_clipKey.trackId());
    if (trackOpt) {
        const muse::Color trackColor = projectSceneConfiguration()->clipColor(trackOpt->colorIndex);
        MenuItem& autoColorItem = findItem(muse::actions::ActionCode("action://trackedit/clip/change-color-auto"));
        muse::ui::UiAction action = autoColorItem.action();
        action.iconCode = muse::ui::IconCode::Code::FRETBOARD_MARKER_CIRCLE_FILLED;
        action.iconColor = QString::fromStdString(trackColor.toString());
        autoColorItem.setAction(action);
    }

    for (const auto& action : m_colorChangeActionCodeList) {
        MenuItem& item = findItem(ActionCode(action));
        ActionQuery query(action);

        bool checked = false;
        if (clip.isAutoColor && action == m_colorChangeActionCodeList.at(0)) {
            checked = true;
        } else if (!clip.isAutoColor && query.contains("colorindex") && query.param("colorindex").toInt() == clip.colorIndex) {
            checked = true;
        }

        auto state = item.state();
        state.checked = checked;
        item.setState(state);
    }
}

void ClipContextMenuModel::updateColorMenu()
{
    ClipStyles::Style clipStyle = projectSceneConfiguration()->clipStyle();
    MenuItem& colorMenu = findMenu("colorMenu");

    if (clipStyle == ClipStyles::Style::CLASSIC) {
        colorMenu.setState(muse::ui::UiActionState::make_disabled());
    } else {
        colorMenu.setState(muse::ui::UiActionState::make_enabled());
    }
}

MenuItemList ClipContextMenuModel::makeClipColourItems()
{
    m_colorChangeActionCodeList.clear();
    MenuItemList items;
    items <<
        makeMenuItem("action://trackedit/clip/change-color-auto",
                     muse::TranslatableString("clip", "Same as track color"));
    items << makeSeparator();
    m_colorChangeActionCodeList.push_back("action://trackedit/clip/change-color-auto");

    const auto& colorInfos = projectSceneConfiguration()->clipColorInfos();
    for (const auto& info : colorInfos) {
        items << makeMenuItem(makeClipColorChangeAction(info.index).toString(),
                              info.title);
        m_colorChangeActionCodeList.push_back(makeClipColorChangeAction(info.index).toString());
    }

    return items;
}

namespace {
const QString FADE_IN_PREFIX = QStringLiteral("clip-fade-in:");
const QString FADE_OUT_PREFIX = QStringLiteral("clip-fade-out:");
const QString FADE_CURVE_ID = QStringLiteral("clip-fade-equal-power");
const std::vector<int> FADE_LENGTHS_MS { 0, 10, 50, 100, 200, 500, 1000 };

muse::TranslatableString fadeLengthTitle(int ms)
{
    if (ms == 0) {
        return muse::TranslatableString("clip", "None");
    }
    if (ms >= 1000) {
        return muse::TranslatableString("clip", "%1 s").arg(ms / 1000);
    }
    return muse::TranslatableString("clip", "%1 ms").arg(ms);
}
}

MenuItem* ClipContextMenuModel::makeFadesMenu()
{
    trackedit::Clip clip;
    if (auto prj = globalContext()->currentTrackeditProject()) {
        clip = prj->clip(m_clipKey.key);
    }
    const bool valid = clip.isValid();
    const auto lengthItems = [&](const QString& prefix, double current) {
        MenuItemList list;
        const int currentMs = static_cast<int>(std::lround(current * 1000.0));
        bool matched = false;
        for (int ms : FADE_LENGTHS_MS) {
            auto* item = new MenuItem(this);
            item->setId(prefix + QString::number(ms));
            item->setTitle(fadeLengthTitle(ms));
            muse::ui::UiActionState state;
            state.enabled = valid;
            state.checked = currentMs == ms;
            item->setState(state);
            item->setCheckable(true);
            matched = matched || currentMs == ms;
            list << item;
        }
        if (!matched) {
            // A length set by comping or another value: show it, checked
            auto* item = new MenuItem(this);
            item->setId(prefix + QString::number(currentMs));
            item->setTitle(fadeLengthTitle(currentMs));
            muse::ui::UiActionState state;
            state.enabled = valid;
            state.checked = true;
            item->setState(state);
            item->setCheckable(true);
            list << item;
        }
        return list;
    };
    auto* curve = new MenuItem(this);
    curve->setId(FADE_CURVE_ID);
    curve->setTitle(muse::TranslatableString("clip", "Equal-power curve"));
    muse::ui::UiActionState curveState;
    curveState.enabled = valid && (clip.fadeIn > 0 || clip.fadeOut > 0);
    curveState.checked = clip.fadeCurve == 1;
    curve->setState(curveState);
    curve->setCheckable(true);

    MenuItemList items {
        makeMenu(muse::TranslatableString("clip", "Fade in"), lengthItems(FADE_IN_PREFIX, clip.fadeIn), "menu-fade-in", valid),
        makeMenu(muse::TranslatableString("clip", "Fade out"), lengthItems(FADE_OUT_PREFIX, clip.fadeOut), "menu-fade-out", valid),
        curve,
    };
    return makeMenu(muse::TranslatableString("clip", "Fades"), items, "menu-fades", valid);
}

namespace {
const QString TR_PREFIX = QStringLiteral("clip-transients:");
}

MenuItem* ClipContextMenuModel::makeTransientsMenu()
{
    trackedit::Clip clip;
    if (auto prj = globalContext()->currentTrackeditProject()) {
        clip = prj->clip(m_clipKey.key);
    }
    const bool valid = clip.isValid();
    const auto item = [&](const QString& id, const muse::TranslatableString& title, bool enabled = true) {
        auto* it = new MenuItem(this);
        it->setId(TR_PREFIX + id);
        it->setTitle(title);
        muse::ui::UiActionState st;
        st.enabled = valid && enabled;
        it->setState(st);
        return it;
    };
    const bool has = !clip.transients.empty();
    MenuItemList grid {
        item("grid:0", muse::TranslatableString("clip", "Bars")),
        item("grid:1", muse::TranslatableString("clip", "Beats")),
        item("grid:2", muse::TranslatableString("clip", "1/8 notes")),
        item("grid:3", muse::TranslatableString("clip", "1/16 notes")),
    };
    MenuItemList items {
        item("detect:20", muse::TranslatableString("clip", "Detect transients (low sensitivity)")),
        item("detect:50", muse::TranslatableString("clip", "Detect transients")),
        item("detect:80", muse::TranslatableString("clip", "Detect transients (high sensitivity)")),
        item("labels", muse::TranslatableString("clip", "Detect transients to label track")),
        item("slice:0", muse::TranslatableString("clip", "Slice at transients")),
        item("slice:50", muse::TranslatableString("clip", "Slice and quantize 50% (1/8)")),
        item("slice:100", muse::TranslatableString("clip", "Slice and quantize 100% (1/8)")),
        makeMenu(muse::TranslatableString("clip", "Slice to grid"), grid, "menu-slice-grid", valid),
        item("clear", muse::TranslatableString("clip", "Clear transient markers"), has),
    };
    return makeMenu(muse::TranslatableString("clip", "Transients"), items, "menu-transients", valid);
}

void ClipContextMenuModel::handleMenuItem(const QString& itemId)
{
    if (itemId.startsWith(TR_PREFIX)) {
        const QString cmd = itemId.mid(TR_PREFIX.size());
        const auto arg = [&] { return cmd.section(':', 1).toInt(); };
        if (cmd.startsWith("detect:")) {
            record()->detectTransients(m_clipKey.key, arg(), false);
        } else if (cmd == "labels") {
            record()->detectTransients(m_clipKey.key, 50, true);
        } else if (cmd.startsWith("slice:")) {
            record()->sliceAtTransients(m_clipKey.key, arg() / 100.0, 2);
        } else if (cmd.startsWith("grid:")) {
            record()->sliceToGrid(m_clipKey.key, arg());
        } else if (cmd == "clear") {
            record()->clearTransients(m_clipKey.key);
        }
        return;
    }
    const bool in = itemId.startsWith(FADE_IN_PREFIX);
    const bool out = itemId.startsWith(FADE_OUT_PREFIX);
    if (!in && !out && itemId != FADE_CURVE_ID) {
        AbstractMenuModel::handleMenuItem(itemId);
        return;
    }
    auto prj = globalContext()->currentTrackeditProject();
    if (!prj) {
        return;
    }
    const trackedit::Clip clip = prj->clip(m_clipKey.key);
    if (!clip.isValid()) {
        return;
    }
    double fadeIn = clip.fadeIn;
    double fadeOut = clip.fadeOut;
    int curve = clip.fadeCurve;
    if (in) {
        fadeIn = itemId.mid(FADE_IN_PREFIX.size()).toInt() / 1000.0;
    } else if (out) {
        fadeOut = itemId.mid(FADE_OUT_PREFIX.size()).toInt() / 1000.0;
    } else {
        curve = curve == 1 ? 0 : 1;
    }
    record()->setClipFades(m_clipKey.key, fadeIn, fadeOut, curve);
}

QPointF ClipContextMenuModel::fadeDragPreview(bool fadeInHandle, double startIn, double startOut, double dxSec) const
{
    double length = 0.0;
    if (auto prj = globalContext()->currentTrackeditProject()) {
        const trackedit::Clip clip = prj->clip(m_clipKey.key);
        length = clip.endTime - clip.startTime;
    }
    const auto f = fadehandles::drag({ startIn, startOut }, length, fadeInHandle, dxSec);
    return QPointF(f.in, f.out);
}

void ClipContextMenuModel::applyFadeDrag(double fadeIn, double fadeOut, int startCurve, bool toggleCurve)
{
    record()->setClipFades(m_clipKey.key, fadeIn, fadeOut, fadehandles::curveAfterDrag(startCurve, toggleCurve));
}

QPointF ClipContextMenuModel::fadeDragSnap(bool fadeInHandle, double fadeIn, double fadeOut, double snappedEdge) const
{
    trackedit::Clip clip;
    if (auto prj = globalContext()->currentTrackeditProject()) {
        clip = prj->clip(m_clipKey.key);
    }
    const auto f = fadehandles::snapEdge({ fadeIn, fadeOut }, clip.startTime, clip.endTime, fadeInHandle, snappedEdge);
    return QPointF(f.in, f.out);
}

QString ClipContextMenuModel::fadeDragReadout(bool fadeInHandle, double seconds, int curve, bool showCurve) const
{
    return QString::fromStdString(fadehandles::readout(fadeInHandle, seconds, curve, showCurve));
}
