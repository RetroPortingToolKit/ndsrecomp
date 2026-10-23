#include "execution_model.h"
#include <cstdio>
#include <cstdlib>
static void require(bool value) { if (!value) std::abort(); }
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
    std::puts("execution model availability, ROM gate and state identity passed");
}
