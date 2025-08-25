#include "db.hpp"
#include "log.hpp"

#include "sqlite3.h"

#include <cstdio>

#define META_FIELD_VERSION "db_version"
#define META_TABLE         "meta_table"
#define CCD_RESULTS_TABLE  "ccd_results"
namespace {
constexpr char kDbName[] = "results.db";
static sqlite3 *s_database = NULL;

enum PreparedStatements {
    CCD_RESULT_INSERT,
    CCD_RESULT_GET_LAST_ID,
    CCD_RESULT_UPDATE_DATA,
    CCD_RESULT_UPDATE_NAME,
    CCD_RESULT_UPDATE_NOTES,
    CCD_RESULT_COUNT_IN_TIME_RANGE,
    CCD_RESULT_QUERY_IN_TIME_RANGE,
    __COUNT,
};

sqlite3_stmt *prepared_stmt[PreparedStatements::__COUNT];
static const char *sql_statements[PreparedStatements::__COUNT] = {
    // clang-format off
    /* CCD_RESULT_INSERT              */ "INSERT INTO " CCD_RESULTS_TABLE " (timestamp, integration_time, iterations, result) VALUES (?, ?, ?, ?);",
    /*CCD_RESULT_GET_LAST_ID          */ "SELECT MAX(rowid) FROM " CCD_RESULTS_TABLE,
    /* CCD_RESULT_UPDATE_DATA         */ "UPDATE " CCD_RESULTS_TABLE " SET result = ? WHERE rowid = ?;",
    /* CCD_RESULT_UPDATE_NAME         */ "UPDATE " CCD_RESULTS_TABLE " SET name = ? WHERE rowid = ?;",
    /* CCD_RESULT_UPDATE_NOTES        */ "UPDATE " CCD_RESULTS_TABLE " SET notes = ? WHERE rowid = ?;",
    /* CCD_RESULT_COUNT_IN_TIME_RANGE */ "SELECT COUNT(*) FROM " CCD_RESULTS_TABLE " WHERE timestamp BETWEEN ? AND ?",
    /* CCD_RESULT_QUERY_IN_TIME_RANGE */ "SELECT rowid, name, timestamp, integration_time, iterations, notes, length(result), result FROM " CCD_RESULTS_TABLE " WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp;",
    // clang-format on
};

bool create_tables(sqlite3 *db)
{
    constexpr char initial_setup[] =
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        "CREATE TABLE IF NOT EXISTS " CCD_RESULTS_TABLE " ("
        "name TEXT,"
        "timestamp INTEGER NOT NULL,"
        "integration_time INTEGER NOT NULL,"
        "iterations INTEGER NOT NULL,"
        "notes TEXT,"
        "result BLOB"
        ");"
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        "CREATE TABLE IF NOT EXISTS " META_TABLE " ("
        "name TEXT PRIMARY KEY,"
        "value NOT NULL"
        ");"
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        "INSERT OR IGNORE INTO " META_TABLE " VALUES(\"" META_FIELD_VERSION "\", 1);"
        ////////////////////////////////////////////////////////////////////////////////////////////////////
        ;

    char *err_msg = NULL;
    int create_table_result = sqlite3_exec(db, initial_setup, 0, 0, &err_msg);
    if (create_table_result != SQLITE_OK) {
        LOG_ERROR("SQL error: [{}]", err_msg);
        return false;
    }

    return true;
}

bool update_db(sqlite3 *db)
{
    static constexpr char kSQL[] = "SELECT * FROM " META_TABLE;
    sqlite3_stmt *stmt;
    int prepare_result = sqlite3_prepare_v2(db, kSQL, -1, &stmt, NULL);
    if (prepare_result != SQLITE_OK) {
        LOG_ERROR("Failed to prepare statement: [{}] [{}]", kSQL, sqlite3_errmsg(s_database));
        return false;
    }

    bool done = false;
    while (!done) {
        int query_result = sqlite3_step(stmt);
        if (query_result == SQLITE_DONE) {
            done = true;
            continue;
        }
        if (query_result != SQLITE_ROW) {
            LOG_ERROR("Failed to read meta info", sqlite3_errstr(query_result));
            done = true;
            continue;
        }

        const char *field_name = (char *)sqlite3_column_text(stmt, 0);
        if (strcmp(META_FIELD_VERSION, field_name) == 0) {
            s64 version = sqlite3_column_int64(stmt, 1);
            switch (version) {
                default: {
                    case 1: LOG_NORM("Database is on latest version [{}]", version); break;
                }
            }
        }
    }

    sqlite3_finalize(stmt);
    return true;
}
} // namespace

bool db_open()
{
    int open_result = sqlite3_open(kDbName, &s_database);
    if (open_result != SQLITE_OK) {
        LOG_ERROR("Cannot open database: [{}]", sqlite3_errmsg(s_database));
        return false;
    }

    if (!create_tables(s_database) || !update_db(s_database)) {
        sqlite3_close(s_database);
        return false;
    }

    for (u32 i = 0; i < (u32)PreparedStatements::__COUNT; ++i) {
        const char *sql_stmt = sql_statements[i];
        int prepare_result = sqlite3_prepare_v2(s_database, sql_stmt, -1, &prepared_stmt[i], NULL);
        if (prepare_result != SQLITE_OK) {
            LOG_ERROR("Failed to prepare statement: [{}-{}] [{}]\n", i, sql_stmt, sqlite3_errmsg(s_database));
            sqlite3_close(s_database);
            return false;
        }
    }

    return true;
}

s64 db_ccd_result_create(
    std::chrono::seconds timestamp, u32 integration_time, u32 iterations, const void *result_data, s32 data_size)
{
    sqlite3_stmt *insert_stmt = prepared_stmt[(u32)PreparedStatements::CCD_RESULT_INSERT];

    sqlite3_reset(insert_stmt);
    sqlite3_clear_bindings(insert_stmt);

    sqlite3_bind_int64(insert_stmt, 1, timestamp.count());
    sqlite3_bind_int(insert_stmt, 2, integration_time);
    sqlite3_bind_int(insert_stmt, 3, iterations);
    sqlite3_bind_blob(insert_stmt, 4, result_data, data_size, SQLITE_STATIC);

    int insert_result = sqlite3_step(insert_stmt);
    if (insert_result != SQLITE_DONE) {
        LOG_ERROR("Insert execution failed: [{}]", sqlite3_errmsg(s_database));
        return -1;
    }

    return sqlite3_last_insert_rowid(s_database);
}

s64 get_next_ccd_result_id()
{
    sqlite3_stmt *get_id_stmt = prepared_stmt[(u32)PreparedStatements::CCD_RESULT_GET_LAST_ID];
    sqlite3_reset(get_id_stmt);

    int insert_result = sqlite3_step(get_id_stmt);
    if (insert_result != SQLITE_ROW) {
        LOG_ERROR("Insert execution failed: [{}]", sqlite3_errmsg(s_database));
        return -1;
    }

    s64 id = sqlite3_column_int64(get_id_stmt, 0);

    insert_result = sqlite3_step(get_id_stmt);
    if (insert_result != SQLITE_DONE) {
        LOG_ERROR("Insert execution failed: [{}]", sqlite3_errmsg(s_database));
        return -1;
    }

    return id + 1;
}

bool db_ccd_result_update_name(s64 row_id, std::string_view name)
{
    sqlite3_stmt *update_stmt = prepared_stmt[(u32)PreparedStatements::CCD_RESULT_UPDATE_NAME];

    sqlite3_reset(update_stmt);
    sqlite3_clear_bindings(update_stmt);

    sqlite3_bind_text(update_stmt, 1, name.data(), (int)name.size(), SQLITE_STATIC);
    sqlite3_bind_int64(update_stmt, 2, row_id);

    int update_result = sqlite3_step(update_stmt);
    if (update_result != SQLITE_DONE) {
        LOG_ERROR("Update execution failed: [{}]", sqlite3_errmsg(s_database));
        return false;
    }

    return true;
}

bool db_ccd_result_update_notes(s64 row_id, std::string_view notes)
{
    sqlite3_stmt *update_stmt = prepared_stmt[(u32)PreparedStatements::CCD_RESULT_UPDATE_NOTES];

    sqlite3_reset(update_stmt);
    sqlite3_clear_bindings(update_stmt);

    sqlite3_bind_text(update_stmt, 1, notes.data(), (int)notes.size(), SQLITE_STATIC);
    sqlite3_bind_int64(update_stmt, 2, row_id);

    int update_result = sqlite3_step(update_stmt);
    if (update_result != SQLITE_DONE) {
        LOG_ERROR("Update execution failed: [{}]", sqlite3_errmsg(s_database));
        return false;
    }

    return true;
}

bool db_ccd_result_update_data(s64 row_id, const void *result_data, s32 data_size)
{
    sqlite3_stmt *update_stmt = prepared_stmt[(u32)PreparedStatements::CCD_RESULT_UPDATE_DATA];

    _defer
    {
        sqlite3_reset(update_stmt);
    };

    sqlite3_clear_bindings(update_stmt);

    sqlite3_bind_blob(update_stmt, 1, result_data, data_size, SQLITE_STATIC);
    sqlite3_bind_int64(update_stmt, 2, row_id);

    int update_result = sqlite3_step(update_stmt);
    if (update_result != SQLITE_DONE) {
        LOG_ERROR("Update execution failed: [{}]", sqlite3_errmsg(s_database));
        return false;
    }

    return true;
}

void db_ccd_result_get_by_time_range(std::chrono::seconds start_time,
                                     std::chrono::seconds end_time,
                                     std::vector<CCDOperation> *ops)
{
    sqlite3_stmt *count_stmt = prepared_stmt[(u32)PreparedStatements::CCD_RESULT_COUNT_IN_TIME_RANGE];
    _defer
    {
        sqlite3_reset(count_stmt);
    };

    sqlite3_bind_int64(count_stmt, 1, start_time.count());
    sqlite3_bind_int64(count_stmt, 2, end_time.count());

    s32 record_count = 0;
    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
        record_count = sqlite3_column_int(count_stmt, 0);
    }

    // TODO this feels weird. We probably just want to reset the list before?
    ops->clear();
    ops->reserve(record_count);

    if (record_count == 0) {
        return;
    }

    LOG_NORM("Found [{}] ccd results in time range [{} - {}]", record_count, start_time, end_time);

    sqlite3_stmt *query_time_range_stmt = prepared_stmt[(u32)PreparedStatements::CCD_RESULT_QUERY_IN_TIME_RANGE];
    _defer
    {
        sqlite3_reset(query_time_range_stmt);
    };

    sqlite3_clear_bindings(query_time_range_stmt);
    sqlite3_bind_int64(query_time_range_stmt, 1, start_time.count());
    sqlite3_bind_int64(query_time_range_stmt, 2, end_time.count());

    bool done = false;
    while (!done) {
        int query_result = sqlite3_step(query_time_range_stmt);
        if (query_result == SQLITE_DONE) {
            done = true;
            continue;
        }
        if (query_result != SQLITE_ROW) {
            LOG_ERROR("Query time range failed: [{}]", sqlite3_errstr(query_result));
            return;
        }

        CCDOperation &record = ops->emplace_back();
        record.id = sqlite3_column_int64(query_time_range_stmt, 0);
        const char *name = (char *)sqlite3_column_text(query_time_range_stmt, 1);
        if (name) {
            record.name = name;
        }
        {
            using namespace std::chrono;
            record.ts = local_seconds{seconds(sqlite3_column_int64(query_time_range_stmt, 2))};
        }
        record.exposure_time_in_us = sqlite3_column_int(query_time_range_stmt, 3);
        record.iterations = sqlite3_column_int(query_time_range_stmt, 4);
        const char *note = (char *)sqlite3_column_text(query_time_range_stmt, 5);
        if (note) {
            record.note = note;
        }

        size_t blob_len = sqlite3_column_int64(query_time_range_stmt, 6);
        const void *blob_data = sqlite3_column_blob(query_time_range_stmt, 7);
        // TODO re-do this
#if 0
        if (blob_len > 0 && blob_data) {
            s64 size = std::min(
                blob_len, record.accumulated_values.size() * sizeof(decltype(record.accumulated_values)::value_type));
            memcpy(record.accumulated_values.data(), blob_data, size);
        }
#else
        u32 item_count = blob_len / sizeof(u32);
        record.accumulated_values.reserve(item_count);
        record.accumulated_values.insert(
            record.accumulated_values.end(), (u32 *)blob_data, ((u32 *)blob_data) + item_count);
#endif
    }
}

void db_close()
{
    for (u32 i = 0; i < (u32)PreparedStatements::__COUNT; ++i) {
        sqlite3_stmt *stmt = prepared_stmt[i];
        sqlite3_finalize(stmt);
    }

    if (s_database) {
        sqlite3_close(s_database);
        s_database = NULL;
    }
}
