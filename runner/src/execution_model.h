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
