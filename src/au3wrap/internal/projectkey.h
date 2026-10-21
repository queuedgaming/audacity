/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <optional>
#include <string>

#include "framework/global/async/notification.h"

#include "au3-registries/ClientData.h"
#include "au3-project/Project.h"

class AudacityProject;

namespace au::au3 {
//! Project-level musical key and tuning reference, saved with the project next to the
//! tempo / time signature attributes (musical_key_tonic, musical_key_minor, musical_key_tuning_cents).
//! Edited from the playback toolbar (key control) or stored from "Detect key"; used by auto-comp
//! pitch scoring when present. Part of undo/redo state (ProjectKeyRestorer), so each change made with
//! a history push is one undo step.
class ProjectKey : public ClientData::Base
{
public:
    struct Key {
        int tonic = 0;            //!< pitch class, 0 = C
        bool minor = false;
        double tuningCents = 0.0; //!< offset from A4 = 440 Hz
        double confidence = 0.0;  //!< detector confidence when it was stored (not saved)
    };

    static ProjectKey& Get(AudacityProject& project);
    static const ProjectKey& Get(const AudacityProject& project);

    const std::optional<Key>& key() const { return m_key; }
    void setKey(const std::optional<Key>& key);

    //! Sent after every setKey (including undo/redo restores and loading)
    muse::async::Notification keyChanged() const { return m_keyChanged; }

    //! Short label, e.g. "D min, A442" / "Eb maj, A440"; "No key" when unset
    static std::string shortLabel(const std::optional<Key>& key);
    static std::string tonicName(int tonic);

private:
    std::optional<Key> m_key;
    muse::async::Notification m_keyChanged;
};
}
