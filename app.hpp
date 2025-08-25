#pragma once
#include "shorthand.hpp"

#include <chrono>

void set_window_title(std::string_view);

struct AppCommand {
    enum Type : u8 {
        ConnectToDevice,
        StartCCDOperation,
        CCDOperationUpdateName,
        CCDOperationUpdateNote,
        CCDOperationLoad,
        DecodeIncommingData,
    };

    Type type;
    union {
        std::string_view com_path;
        struct {
            u32 exposure;
            u32 iterations;
        } ccd_op;
        struct {
            u32 frame_start;
            u32 frame_end;
        };
        struct {
            std::chrono::seconds start_date;
            std::chrono::seconds end_date;
        };
        u32 operation_to_update;
    }data;
};

void queue_command(const AppCommand &cmd);

struct ComPort {
    std::string friendly_name;
    std::string com_path;
};

struct COMHandle;

enum class COMConnectionStatus : u8 { NOT_CONNECTED, CONNECTED, CONNECTION_ERROR };

struct Comms {
    std::vector<ComPort> enumerated_ports;
    COMHandle *com_connection = nullptr;
    COMConnectionStatus connection_status = COMConnectionStatus::NOT_CONNECTED;
    std::string connected_com_path;

    static constexpr auto kReadDataMaxSize = 1_MB;
    u8 *read_data_buffer = nullptr;
    u32 read_data_size = 0;
};

u32 handle_incomming_data(COMHandle *handle, u8 *buffer, u32 offset, u32 len);
void enumerate_com_ports(std::vector<ComPort> *ports);

struct CCDOperation {
    u32 id;
    std::chrono::local_seconds ts;
    u32 exposure_time_in_us;
    u32 iterations;
    // TODO change this from vector to just an alloc because vector is idiotic
    std::vector<u32> accumulated_values;
    std::string name;
    std::string note;
};

struct App {
    std::vector<CCDOperation> ccd_operations;
};

void handle_commands(App *app, Comms *comms);
