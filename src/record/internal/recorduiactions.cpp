/*
* Audacity: A Digital Audio Editor
*/
#include "recorduiactions.h"

#include "framework/ui/view/iconcodes.h"
#include "framework/global/types/translatablestring.h"

#include "context/uicontext.h"
#include "context/shortcutcontext.h"

using namespace au::record;
using namespace muse;
using namespace muse::ui;
using namespace muse::actions;

static const ActionQuery RECORD_START_QUERY("action://record/start");
static const ActionQuery RECORD_PAUSE_QUERY("action://record/pause");
static const ActionQuery RECORD_STOP_QUERY("action://record/stop");
static const ActionQuery RECORD_LEVEL_QUERY("action://record/level");
static const ActionQuery RECORD_TOGGLE_MIC_METERING("action://record/toggle-mic-metering");
static const ActionQuery RECORD_TOGGLE_INPUT_MONITORING("action://record/toggle-input-monitoring");
static const ActionQuery RECORD_LEAD_IN_RECORDING_QUERY("action://record/lead-in-recording");
static const ActionQuery RECORD_NEXT_TAKE_QUERY("action://record/next-take");
static const ActionQuery RECORD_PREVIOUS_TAKE_QUERY("action://record/previous-take");
static const ActionQuery RECORD_ACTIVATE_TAKE_QUERY("action://record/activate-take");
static const ActionQuery RECORD_COMP_TAKE_SELECTION_QUERY("action://record/comp-take-selection");
static const ActionQuery RECORD_AUTO_COMP_QUERY("action://record/auto-comp-takes");
static const ActionQuery RECORD_DETECT_KEY_QUERY("action://record/detect-key");

const UiActionList RecordUiActions::m_mainActions = {
    UiAction(RECORD_START_QUERY.toString(),
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_OPENED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Record"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Record"),
             IconCode::Code::RECORD_FILL
             ),
    UiAction(RECORD_PAUSE_QUERY.toString(),
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Pause"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Pause"),
             IconCode::Code::PAUSE_FILL
             ),
    UiAction(RECORD_STOP_QUERY.toString(),
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_OPENED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Stop"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Stop record"),
             IconCode::Code::STOP_FILL
             ),
    UiAction(RECORD_LEVEL_QUERY.toString(),
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Record level"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Set record level"),
             IconCode::Code::MICROPHONE
             ),
    UiAction(RECORD_TOGGLE_MIC_METERING.toString(),
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Show mic metering"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Show mic metering"),
             Checkable::Yes
             ),
    UiAction(RECORD_TOGGLE_INPUT_MONITORING.toString(),
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Turn on input monitoring"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Turn on input monitoring"),
             Checkable::Yes
             ),
    UiAction(RECORD_LEAD_IN_RECORDING_QUERY.toString(),
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Lead-in Recording"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Start lead-in recording"),
             IconCode::Code::RECORD_FILL
             ),
    UiAction(RECORD_NEXT_TAKE_QUERY.toString(),
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Next take"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Play the next take of the focused track")
             ),
    UiAction(RECORD_PREVIOUS_TAKE_QUERY.toString(),
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Previous take"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Play the previous take of the focused track")
             ),
    UiAction(RECORD_ACTIVATE_TAKE_QUERY.toString(),
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Activate take"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Make the focused take lane the active take of its track")
             ),
    UiAction(RECORD_COMP_TAKE_SELECTION_QUERY.toString(),
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Comp selection from take"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Use the selected time range of the focused take lane on its main track")
             ),
    UiAction(RECORD_AUTO_COMP_QUERY.toString(),
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Auto-comp takes"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Pick the cleanest take for each phrase of the focused track")
             ),
    UiAction(RECORD_DETECT_KEY_QUERY.toString(),
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Detect key"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Estimate the key, scale and tuning of the selection or focused track and store it as the project key")
             ),
    UiAction("record-on-current-track",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_OPENED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Record on current track"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Record on current track")
             ),
    UiAction("record-on-new-track",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_OPENED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Record on new track"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Record on new track")
             ),
};

RecordUiActions::RecordUiActions(const muse::modularity::ContextPtr& ctx, std::shared_ptr<RecordController> controller)
    : muse::Contextable(ctx), m_controller(controller)
{
}

void RecordUiActions::init()
{
    m_controller->isRecordAllowedChanged().onNotify(this, [this]() {
        ActionCodeList codes;

        for (const UiAction& action : actionsList()) {
            codes.push_back(action.code);
        }

        m_actionEnabledChanged.send(codes);
    });

    m_controller->isRecordingChanged().onNotify(this, [this]() {
        ActionCodeList codes;

        for (const UiAction& action : actionsList()) {
            codes.push_back(action.code);
        }

        m_actionEnabledChanged.send(codes);
    });

    m_controller->isMicMeteringOnChanged().onNotify(this, [this]() {
        m_actionCheckedChanged.send(ActionCodeList { RECORD_TOGGLE_MIC_METERING.toString() });
    });

    m_controller->isInputMonitoringOnChanged().onNotify(this, [this]() {
        m_actionCheckedChanged.send(ActionCodeList { RECORD_TOGGLE_INPUT_MONITORING.toString() });
    });
}

const UiActionList& RecordUiActions::actionsList() const
{
    static UiActionList alist;
    if (alist.empty()) {
        alist.insert(alist.end(), m_mainActions.cbegin(), m_mainActions.cend());
    }
    return alist;
}

bool RecordUiActions::actionEnabled(const UiAction& act) const
{
    return m_controller->canReceiveAction(act.code);
}

bool RecordUiActions::actionChecked(const UiAction& act) const
{
    if (act.code == RECORD_TOGGLE_MIC_METERING.toString()) {
        return m_controller->isMicMeteringOn();
    }

    if (act.code == RECORD_TOGGLE_INPUT_MONITORING.toString()) {
        return m_controller->isInputMonitoringOn();
    }

    return false;
}

muse::async::Channel<ActionCodeList> RecordUiActions::actionEnabledChanged() const
{
    return m_actionEnabledChanged;
}

muse::async::Channel<ActionCodeList> RecordUiActions::actionCheckedChanged() const
{
    return m_actionCheckedChanged;
}
