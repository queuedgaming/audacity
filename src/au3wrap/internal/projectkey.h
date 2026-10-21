/*
 * Audacity: A Digital Audio Editor
 */
#pragma once

#include <optional>

#include "au3-registries/ClientData.h"
#include "au3-project/Project.h"

class AudacityProject;

namespace au::au3 {
//! Project-level musical key and tuning reference, saved with the project next to the
//! tempo / time signature attributes (musical_key_tonic, musical_key_minor, musical_key_tuning_cents).
//! Set by "Detect key"; used by auto-comp pitch scoring when present.
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
    void setKey(const std::optional<Key>& key) { m_key = key; }

private:
    std::optional<Key> m_key;
};
}
