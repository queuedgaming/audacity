/*
* Audacity: A Digital Audio Editor
*/
#include "recordcommandsstate.h"

#include "framework/global/log.h"

#include "../recordcommands.h"

using namespace au::record;
using namespace muse;
using namespace muse::rcommand;

std::string RecordCommandsState::moduleName() const
{
    return "record";
}

void RecordCommandsState::init()
{
    m_moduleRegister = commandsRegister()->moduleRegister(moduleName());
    IF_ASSERT_FAILED(m_moduleRegister) {
        return;
    }

    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        updateCommandStates();
    });

    controller()->isRecordAllowedChanged().onNotify(this, [this]() {
        updateCommandStates();
    });

    controller()->isRecordingChanged().onNotify(this, [this]() {
        updateCommandStates();
    });

    controller()->isMicMeteringOnChanged().onNotify(this, [this]() {
        updateCommandStates({ RECORD_TOGGLE_MIC_METERING_COMMAND });
    });

    controller()->isInputMonitoringOnChanged().onNotify(this, [this]() {
        updateCommandStates({ RECORD_TOGGLE_INPUT_MONITORING_COMMAND });
    });

    // Take and key commands depend on the focused / selected track and the time selection
    if (selectionController()) {
        selectionController()->selectedTracksChanged().onReceive(this, [this](const auto&) { updateTrackCommandStates(); });
        selectionController()->dataSelectedStartTimeChanged().onReceive(this, [this](const auto&) { updateTrackCommandStates(); });
        selectionController()->dataSelectedEndTimeChanged().onReceive(this, [this](const auto&) { updateTrackCommandStates(); });
    }
    if (trackNavigationController()) {
        trackNavigationController()->focusedTrackChanged().onReceive(this, [this](const auto&, bool) { updateTrackCommandStates(); });
    }

    updateCommandStates();
}

void RecordCommandsState::updateTrackCommandStates()
{
    updateCommandStates({ RECORD_NEXT_TAKE_COMMAND, RECORD_PREVIOUS_TAKE_COMMAND, RECORD_ACTIVATE_TAKE_COMMAND,
                          RECORD_COMP_TAKE_SELECTION_COMMAND, RECORD_AUTO_COMP_COMMAND, RECORD_DETECT_KEY_COMMAND });
}

void RecordCommandsState::deinit()
{
    globalContext()->currentProjectChanged().disconnect(this);
    controller()->isRecordAllowedChanged().disconnect(this);
    controller()->isRecordingChanged().disconnect(this);
    controller()->isMicMeteringOnChanged().disconnect(this);
    controller()->isInputMonitoringOnChanged().disconnect(this);
}

void RecordCommandsState::updateCommandStates(const std::vector<Command>& commands)
{
    IF_ASSERT_FAILED(m_moduleRegister) {
        return;
    }

    const auto& commandList = commands.empty() ? m_moduleRegister->commandList() : commands;

    for (const Command& command : commandList) {
        const CommandState newState = commandState(command);
        if (m_commandStates[command] != newState) {
            m_commandStates[command] = newState;
            m_commandStateChanged.send(command, newState);
        }
    }
}

CommandState RecordCommandsState::commandState(const Command& command) const
{
    if (globalContext()->currentProject() == nullptr) {
        return CommandState(false);
    }

    if (command == RECORD_START_COMMAND
        || command == RECORD_ON_CURRENT_TRACK_COMMAND
        || command == RECORD_ON_NEW_TRACK_COMMAND) {
        return CommandState(controller()->isRecordAllowed() && !controller()->isLeadInRecording());
    }

    if (command == RECORD_NEXT_TAKE_COMMAND) {
        return CommandState(controller()->canSwitchTake(TakeSwitch::Next));
    }
    if (command == RECORD_PREVIOUS_TAKE_COMMAND) {
        return CommandState(controller()->canSwitchTake(TakeSwitch::Previous));
    }
    if (command == RECORD_ACTIVATE_TAKE_COMMAND) {
        return CommandState(controller()->canSwitchTake(TakeSwitch::Activate));
    }
    if (command == RECORD_COMP_TAKE_SELECTION_COMMAND) {
        return CommandState(controller()->canSwitchTake(TakeSwitch::CompSelection));
    }
    if (command == RECORD_DETECT_KEY_COMMAND) {
        return CommandState(controller()->canDetectKey());
    }
    if (command == RECORD_AUTO_COMP_COMMAND) {
        return CommandState(controller()->canSwitchTake(TakeSwitch::AutoComp));
    }

    if (command == RECORD_LEAD_IN_RECORDING_COMMAND) {
        return CommandState(controller()->isRecordAllowed() && !controller()->isRecording());
    }

    if (command == RECORD_STOP_COMMAND) {
        return CommandState(controller()->isRecording());
    }

    if (command == RECORD_TOGGLE_MIC_METERING_COMMAND) {
        return CommandState(true, controller()->isMicMeteringOn());
    }

    if (command == RECORD_TOGGLE_INPUT_MONITORING_COMMAND) {
        return CommandState(true, controller()->isInputMonitoringOn());
    }

    return CommandState(true);
}

async::Channel<Command, CommandState> RecordCommandsState::commandStateChanged() const
{
    return m_commandStateChanged;
}
