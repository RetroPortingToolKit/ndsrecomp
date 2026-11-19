#pragma once
#include <string>

enum class NdsExecutionModel { Faithful, Performance };
// Fixed before boot. Unsupported explicit requests fail instead of silently
// reporting a performance profile while running another implementation.
bool nds_execution_model_select(const std::string& name, const std::string& rom_sha1,
                                std::string& error);
NdsExecutionModel nds_execution_model();
const char* nds_execution_model_identity();
bool nds_performance_execution();

// Boot-time selection of the optional native services inside the performance
// model. Connectivity only decides who owns the ARM7: the offline ARM7 driver
// replacement implements no wireless/WFC/local-wireless services, so any
// connected profile keeps the original ARM7 driver (native ARM7 execution,
// same scheduler). Whole assets are ARM9-only file operations that already run
// beside that original ARM7 driver during the offline bootstrap, so they
// follow the performance model and their own switch, not connectivity.
struct NdsServiceProfileRequest {
    bool performance = false;
    const char* offline_arm7_env = nullptr;  // NDS_OFFLINE_ARM7; "0" disables
    const char* whole_assets_env = nullptr;  // NDS_WHOLE_ASSETS; "0" disables
    bool network = false;
    bool wfc = false;
    bool local_wireless = false;
    bool replay = false;
};
struct NdsServiceProfile {
    bool offline_arm7 = false;
    bool whole_assets = false;
};
NdsServiceProfile nds_service_profile_select(const NdsServiceProfileRequest& request);
