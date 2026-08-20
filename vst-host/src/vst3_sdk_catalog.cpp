// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/vst3_sdk_catalog.hpp"

#include "public.sdk/source/vst/hosting/module.h"

#include <utility>

namespace hibiki {

bool Vst3SdkModuleCatalogV1::scan(const std::string& module_path,
                                  std::vector<Vst3SdkClassInfoV1>& classes,
                                  std::string& error) const {
    classes.clear();
    error.clear();
    auto module = VST3::Hosting::Module::create(module_path, error);
    if (!module) {
        if (error.empty()) error = "VST3 module could not be loaded";
        return false;
    }
    for (const auto& info : module->getFactory().classInfos()) {
        Vst3SdkClassInfoV1 result;
        result.class_id = info.ID().toString();
        result.name = info.name();
        result.vendor = info.vendor();
        result.category = info.category();
        result.version = info.version();
        result.audio_module = result.category == "Audio Module Class";
        classes.push_back(std::move(result));
    }
    return true;
}

}  // namespace hibiki
