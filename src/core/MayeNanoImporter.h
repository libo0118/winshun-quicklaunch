#pragma once

#include "LauncherStore.h"
#include "Models.h"

#include <QString>

namespace ql {

class MayeNanoImporter {
public:
    static MayeImportResult importFromFile(const QString& mdbJsonPath, LauncherStore& store);
};

} // namespace ql
