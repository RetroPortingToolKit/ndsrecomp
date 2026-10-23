#include "execution_model.h"

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
#if defined(NDS_MKDS_PERFORMANCE)
    if (sha1 != "691e00d9a5dd80b04f80cc7559503e8b06848785") {
        error = "performance execution requires the supported MKDS AMCE revision 0 image";
        return false;
    }
    selected = NdsExecutionModel::Performance;
    return true;
#else
    (void)sha1;
    error = "this build does not contain the optional MKDS performance banks";
    return false;
#endif
}
NdsExecutionModel nds_execution_model() { return selected; }
bool nds_performance_execution() {
    return selected == NdsExecutionModel::Performance;
}
const char* nds_execution_model_identity() {
    return nds_performance_execution() ? "performance-flat-aot-v3-timing-v3" : "faithful-v1";
}
