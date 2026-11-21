#pragma once

#include "ui_config.hpp"

#include <QString>

namespace data_extraction::dashboard {

class UiConfigLoader {
public:
    static UiAppConfig load_from_file(const QString& file_path);
    static UiAppConfig default_config();
};

}  // namespace data_extraction::dashboard
