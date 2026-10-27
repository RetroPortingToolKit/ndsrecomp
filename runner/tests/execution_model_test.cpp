#include "execution_model.h"
#include <cstdio>
#include <cstdlib>
static void require(bool value) { if (!value) std::abort(); }
#if defined(NDS_ARM9_DYNARMIC)
static bool host_available = true;
bool nds_arm9_native_supported() { return host_available; }
#endif
int main() {
    std::string error;
    require(!nds_performance_execution());
    const std::string faithful = nds_execution_model_identity();
    require(nds_execution_model_select("faithful", "unknown", error));
    require(!nds_execution_model_select("typo", "unknown", error) && !error.empty());
    error.clear();
    require(!nds_execution_model_select("performance", "unknown", error) && !error.empty());
    require(!nds_performance_execution());
    error.clear();
    const bool supported = nds_execution_model_select("performance",
        "691e00d9a5dd80b04f80cc7559503e8b06848785", error);
#if defined(NDS_MKDS_PERFORMANCE)
    require(supported && nds_performance_execution());
    require(faithful != nds_execution_model_identity());
#else
    require(!supported && !nds_performance_execution() && !error.empty());
    require(faithful == nds_execution_model_identity());
#endif
    require(nds_execution_model_select("faithful", "unknown", error));
    const bool mph = nds_execution_model_select("performance",
        "90164d1ac127ee5f9815ea4ae7de798c7b5fc629", error);
#if defined(NDS_MPH_PERFORMANCE)
    require(mph && nds_performance_execution());
    require(faithful != nds_execution_model_identity());
#else
    require(!mph && !nds_performance_execution());
#endif
#if defined(NDS_ARM9_DYNARMIC)
    require(nds_execution_model_select("faithful", "unknown", error));
    host_available = false;
    require(!nds_execution_model_select("performance", "691e00d9a5dd80b04f80cc7559503e8b06848785", error));
    require(!nds_performance_execution() && error.find("CPU") != std::string::npos);
    require(nds_execution_model_select("faithful", "unknown", error));
#endif
    std::puts("execution model availability, ROM/CPU gates and state identity passed");
}
