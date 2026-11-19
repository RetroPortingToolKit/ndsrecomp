#include "execution_model.h"
#include "arm9_native_options.h"
#include <cstring>
#if defined(NDS_WHOLE_ASSETS)
#include "whole_asset_service.h"
#endif
#if defined(NDS_OFFLINE_ARM7)
#include "offline_arm7.h"
#endif
#if defined(NDS_ARM9_DYNARMIC)
#include "arm9_native.h"
#endif

namespace {
NdsExecutionModel selected = NdsExecutionModel::Faithful;
}
bool nds_execution_model_select(const std::string& name, const std::string& sha1,
                                std::string& error) {
    if (name == "faithful") {
        selected = NdsExecutionModel::Faithful;
        return true;
    }
    if (name != "performance") {
        error = "--execution-model must be faithful or performance";
        return false;
    }
    bool supported = false;
#if defined(NDS_MKDS_PERFORMANCE)
    supported |= sha1 == "691e00d9a5dd80b04f80cc7559503e8b06848785";
#endif
#if defined(NDS_MPH_PERFORMANCE)
    supported |= sha1 == "90164d1ac127ee5f9815ea4ae7de798c7b5fc629";
#endif
    if (supported) {
#if defined(NDS_ARM9_DYNARMIC)
        if (!nds_arm9_native_supported()) {
            error = "native performance execution is unavailable on this CPU; use faithful execution";
            return false;
        }
#endif
        selected = NdsExecutionModel::Performance;
        return true;
    }
    error = "performance execution is unavailable for this build/image";
    return false;
}
NdsServiceProfile nds_service_profile_select(const NdsServiceProfileRequest& request) {
    const auto enabled = [](const char* value) {
        return !value || std::strcmp(value, "0") != 0;
    };
    const bool connected = request.network || request.wfc ||
        request.local_wireless || request.replay;
    NdsServiceProfile profile;
    profile.offline_arm7 = request.performance && !connected &&
        enabled(request.offline_arm7_env);
    profile.whole_assets = request.performance && enabled(request.whole_assets_env);
    return profile;
}
NdsExecutionModel nds_execution_model() { return selected; }
bool nds_performance_execution() {
    return selected == NdsExecutionModel::Performance;
}
const char* nds_execution_model_identity() {
#if defined(NDS_ARM9_DYNARMIC)
    if(!nds_performance_execution())return "faithful-v1";
    static const std::string identity=std::string("performance-arm9-native-v2-timing2")+
#if defined(NDS_ARM7_NATIVE)
        "-arm7-v1-timing2"+
#endif
#if defined(NDS_NATIVE_CONTROL)
        "-control-v1"+
#endif
#if defined(NDS_ARM9_OBSERVATION_SERVICE)
        (nds_arm9_observation_service()?"-observation-v1":"")+
#endif
#if defined(NDS_OFFLINE_ARM7)
        (nds_offline_arm7_selected()?"-offline-arm7-v1":"")+
#endif
#if defined(NDS_WHOLE_ASSETS)
        (nds_assets::selected()?"-whole-assets-v1":"")+
#endif
        (nds_native_options().blocks?"-block64":"")+
        (nds_native_options().data?"-data-window":"")+
        (nds_native_options().gx?"-gx-window":"")+
        (nds_native_options().events?"-event256":"");
    return identity.c_str();
#else
    return nds_performance_execution() ? "performance-flat-aot-v3-timing-v3" : "faithful-v1";
#endif
}
