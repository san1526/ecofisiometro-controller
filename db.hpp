#pragma once
#include "shorthand.hpp"

// TODO potential circular deps
#include "app.hpp"

#include <chrono>
#include <vector>

bool db_open();
s64 db_ccd_result_create(
    std::chrono::seconds timestamp, u32 integration_time, u32 iterations, const void *result_data, s32 data_size);
s64 get_next_ccd_result_id();
bool db_ccd_result_update_name(s64 row_id, std::string_view name);
bool db_ccd_result_update_notes(s64 row_id, std::string_view notes);
bool db_ccd_result_update_data(s64 row_id, const void *result_data, s32 data_size);
void db_ccd_result_get_by_time_range(std::chrono::seconds start_time,
                                     std::chrono::seconds end_time,
                                     std::vector<CCDOperation> *);
inline void db_ccd_result_get_all(std::vector<CCDOperation> *operations)
{
    db_ccd_result_get_by_time_range(std::chrono::seconds(0), std::chrono::seconds(s64Max), operations);
}
void db_close();
