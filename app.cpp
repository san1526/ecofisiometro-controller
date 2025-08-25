#include "app.hpp"

#include "db.hpp"
#include "log.hpp"
#include <cassert>

#if _WIN32
#define NOMINMAX
#include "windows.h"

struct COMHandle {
    HANDLE com_handle;
};

COMHandle *setup_com_port(std::string_view com_port)
{
    HANDLE hCom = CreateFile(com_port.data(),
                             GENERIC_READ | GENERIC_WRITE,
                             0,             //  must be opened with exclusive-access
                             NULL,          //  default security attributes
                             OPEN_EXISTING, //  must use OPEN_EXISTING
                             // FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, //  not overlapped I/O
                             FILE_ATTRIBUTE_NORMAL, //  not overlapped I/O
                             NULL);                 //  hTemplate must be NULL for comm devices

    if (hCom == INVALID_HANDLE_VALUE) {
        //  Handle the error.
        LOG_ERROR("CreateFile failed with error {}", GetLastError());
        return nullptr;
    }

    // TODO all of these settings are ignored in our device implementation
#if 0
    DCB dcb = {.DCBlength = sizeof(DCB)};
    if (!GetCommState(hCom, &dcb)) {
        //  Handle the error.
        LOG_ERROR("GetCommState failed with error {}", GetLastError());
        return nullptr;
    }

    dcb.BaudRate = CBR_115200; //  baud rate
    // dcb.BaudRate = CBR_9600;   //  baud rate
    dcb.ByteSize = 8;          //  data size, xmit and rcv
    dcb.Parity = NOPARITY;     //  parity bit
    dcb.StopBits = ONESTOPBIT; //  stop bit

    if (!SetCommState(hCom, &dcb)) {
        //  Handle the error.
        LOG_ERROR("SetCommState failed with error {}", GetLastError());
        return nullptr;
    }
#endif

    COMMTIMEOUTS timeouts;
    GetCommTimeouts(hCom, &timeouts);
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    SetCommTimeouts(hCom, &timeouts);

    return new COMHandle{hCom};
}

void enumerate_com_ports(std::vector<ComPort> *ports)
{
    HKEY hKey = 0;
    if (::RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Hardware\\DeviceMap\\SERIALCOMM", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD cValues = 0;              // number of values for key
        DWORD cchMaxValue = 0;          // longest value name
        DWORD cbMaxValueData = 0;       // longest value data
        DWORD cbSecurityDescriptor = 0; // size of security descriptor
        FILETIME ftLastWriteTime;       // last write time

        // Get the class name and the value count.
        if (ERROR_SUCCESS
            == ::RegQueryInfoKey(hKey,                  // key handle
                                 NULL,                  // buffer for class name
                                 NULL,                  // size of class string
                                 NULL,                  // reserved
                                 NULL,                  // number of subkeys
                                 NULL,                  // longest subkey size
                                 NULL,                  // longest class string
                                 &cValues,              // number of values for this key
                                 &cchMaxValue,          // longest value name
                                 &cbMaxValueData,       // longest value data
                                 &cbSecurityDescriptor, // security descriptor
                                 &ftLastWriteTime)      // last write time
        ) {

            char achValue[1024] = {0};
            DWORD cchValue = 1024;
            BYTE data[128] = {0};
            DWORD dataLen = 128;
            for (DWORD i = 0; i < cValues; i++) {
                cchValue = 1024;
                dataLen = 128;
                if (ERROR_SUCCESS == ::RegEnumValue(hKey, i, achValue, &cchValue, NULL, NULL, data, &dataLen)) {
                    ports->push_back(ComPort{
                        {achValue, cchValue},
                        std::format("\\\\.\\{}", (char *)data)
                    });
                }
            }
        }
    }

    ::RegCloseKey(hKey);
}

bool write_to_com_device(COMHandle *conn, void *data, uint32_t len)
{
#if 0
    OVERLAPPED overlapped;
    memset(&overlapped, 0, sizeof(overlapped));
    WriteFile(conn->com_handle, data, len, NULL, &overlapped);
    DWORD written = 0;
    GetOverlappedResult(conn->com_handle, &overlapped, &written, true);
#else
    DWORD written = 0;
    WriteFile(conn->com_handle, data, len, &written, NULL);
#endif
    return len == written;
}

uint32_t read_from_com_device(COMHandle *conn, void *data, uint32_t len)
{
    DWORD read = 0;
    ReadFile(conn->com_handle, data, len, &read, NULL);
    return read;
}
#endif

enum class HostToDeviceCommand : u8 {
    GetInfo,
    CCDSensor,
};

enum class DeviceToHostResponse : u8 {
    CCDResult,
    Log,
};

inline constexpr u32 get_cobs_overhead(u32 len)
{
    return 1 + (len + 253) / 254;
}

inline constexpr u32 get_max_encoded_size(u32 payload_len)
{
    u32 cmd_size = 1                        // type
                   + 2 * (payload_len != 0) // payload_Len
                   + payload_len            // payload
                   + 2;                     // crc

    return get_cobs_overhead(cmd_size) + 2 /*frame markers*/;
}

u32 cobs_decode(const u8 *data, u32 data_len, u8 *output, u32 output_len)
{
    u8 *output_it = output;

    u8 distance_to_next_0 = 1;
    bool is_delimiter = true;

    const u8 *data_it = data;
    while (data_it != data + data_len) {
        if (output_it >= output + output_len) {
            return -1;
        }

        if (distance_to_next_0 == 1) {
            if (!is_delimiter) {
                *output_it++ = 0;
            }
            distance_to_next_0 = *data_it;
            is_delimiter = distance_to_next_0 == 0xFF;
        } else {
            *output_it = *data_it;
            output_it++;
            distance_to_next_0--;
        }

        data_it++;
    }

    return output_it - output;
}

struct CobsCtx {
    u8 *output_buffer = nullptr;
    u8 *output_it = nullptr;
    u8 *code_byte = nullptr;
    u8 distance_to_last_0;
    u32 output_len;
};

inline CobsCtx cobs_encode_init(u8 *output_buffer, u32 output_len)
{
    return {output_buffer, output_buffer + 1, output_buffer, 1, output_len};
};

inline u32 cobs_encode_end(CobsCtx *ctx)
{
    *ctx->code_byte = ctx->distance_to_last_0;
    if ((u32)(ctx->output_it - ctx->output_buffer) >= ctx->output_len) {
        return u32Max;
    }

    *ctx->output_it = '\0';
    ctx->output_it++;

    return ctx->output_it - ctx->output_buffer;
}

inline bool cobs_encode(CobsCtx *ctx, const void *data, u32 data_len)
{
    u8 *data_it = (u8 *)data;
    while (data_it != (u8 *)data + data_len) {
        if ((u32)(ctx->output_it - ctx->output_buffer) >= ctx->output_len) {
            return false;
        }

        if (*data_it == 0) {
            *ctx->code_byte = ctx->distance_to_last_0;
            ctx->distance_to_last_0 = 1;
            ctx->code_byte = ctx->output_it;
            data_it++;
        } else if (ctx->distance_to_last_0 == 0xFF) {
            *ctx->code_byte = ctx->distance_to_last_0;
            ctx->distance_to_last_0 = 1;
            ctx->code_byte = ctx->output_it;
        } else {
            *ctx->output_it = *data_it;
            ctx->distance_to_last_0++;
            data_it++;
        }

        ctx->output_it++;
    }

    return true;
}

struct Payload {
    u8 *data;
    u8 *cursor;
    u16 len;
};

static u16 get_size(Payload *payload)
{
    u16 size = payload->data < payload->cursor ? payload->cursor - payload->data : payload->data - payload->cursor;
    return size;
}

static bool reached_end(Payload *payload)
{
    return payload->data + payload->len == payload->cursor;
}

static bool ensure_capacity(Payload *payload, u32 desired_cap)
{
    u16 used = get_size(payload);
    u16 left = payload->len - used;
    return left >= desired_cap;
}

template <typename T>
typename std::enable_if_t<std::is_unsigned_v<T>> serialize_varint(Payload *payload, T number)
{
    do {
        *payload->cursor = ((u8)number & 0b0111'1111) | (u8)((!!(number >> 7)) << 7);
        payload->cursor++;
        number >>= 7;
    } while (number);
}

template <typename T>
typename std::enable_if_t<std::is_signed_v<T>> serialize_varint(Payload *payload, T number)
{
    std::make_unsigned_t<T> zig_zag = (number >> (sizeof(number) * 8 - 1)) ^ (number << 1);
    serialize_varint(payload, zig_zag);
}

template <typename T>
typename std::enable_if_t<std::is_integral_v<T>, bool> serialize(Payload *payload, T number)
{
    static_assert(sizeof(T) < 8, "64 bit integer are not supported for now");
    if (!ensure_capacity(payload, 5 /*Worst case for u64*/)) {
        return false;
    }
    // ensure_capacity(payload, 1 /*Tag*/ + 5 /*Worst case for u64*/))
    // serialize(payload, SerializedFieldTag::VarInt);
    serialize_varint(payload, number);
    return true;
}

template <typename T>
typename std::enable_if_t<std::is_enum_v<T>, bool> serialize(Payload *payload, T number)
{
    return serialize(payload, static_cast<std::underlying_type_t<T>>(number));
}

inline bool serialize(Payload *payload, const byte *data, u32 len)
{
    // ensure_capacity(payload, 1 /*Tag*/ + 5 /*Worst case for u32 Len*/);
    if (!ensure_capacity(payload, 5 + len)) {
        return false;
    }
    // serialize(payload, SerializedFieldTag::DataWithLen);
    serialize_varint(payload, len);
    memcpy(payload->cursor, data, len);
    payload->cursor += len;
    return true;
}

template <typename T>
typename std::enable_if_t<std::is_unsigned_v<T>, bool> deserialize_varint(Payload *payload, T *number)
{
    *number = *payload->cursor & 0b0111'1111;
    for (u32 i = 7; *payload->cursor & 0b1000'0000; i += 7) {
        if (reached_end(payload)) {
            return false;
        }
        // assert(sizeof(T) <= i / 7 - 1);
        payload->cursor++;
        *number |= (T)(*(payload->cursor) & 0b0111'1111) << i;
    }
    payload->cursor++;
    return true;
}

template <typename T>
typename std::enable_if_t<std::is_signed_v<T>, bool> deserialize_varint(Payload *payload, T *number)
{
    if (deserialize_varint(payload, (std::make_unsigned_t<T> *)number)) {
        *number = (*number >> 1) ^ -(*number & 1);
        return true;
    }
    return false;
}

template <typename T>
typename std::enable_if_t<std::is_integral_v<T>, bool> deserialize(Payload *payload, T *number)
{
    // if (read_tag(payload) != SerializedFieldTag::VarInt) {
    //     return false;
    // }

    return deserialize_varint(payload, number);
}

bool deserialize(Payload *payload, const u8 **data, u32 *len)
{
    if (reached_end(payload)) {
        return false;
    }

    if (deserialize(payload, len)) {
        *data = payload->cursor;
        payload->cursor += *len;
        return true;
    }

    return false;
}

template <typename T>
typename std::enable_if_t<std::is_enum_v<T>, bool> deserialize(Payload *payload, T *number)
{
    return deserialize(payload, (std::underlying_type_t<T> *)(number));
}

static std::vector<AppCommand> gCommandQueue;

void queue_command(const AppCommand &cmd)
{
    gCommandQueue.push_back(cmd);
}

u32 handle_incomming_data(COMHandle *handle, u8 *buffer, u32 offset, u32 len)
{
    if (handle == nullptr) {
        return 0;
    }

    u32 total = offset;
    u32 read = 0;
    do {
        read = read_from_com_device(handle, buffer + offset, len - total);
        for (u32 i = 0, j = 0; i < read; ++i) {
            if (buffer[total + i] == '\0') {
                queue_command({
                    .type = AppCommand::DecodeIncommingData,
                    .data{
                          .frame_start = total + j,
                          .frame_end = total + i,
                          }
                });
                j = i + 1;
            }
        }
        total += read;
    } while (read != 0 && total < len);

    return total - offset;
}

void handle_commands(App *app, Comms *comms)
{
    u32 incomming_data_decoded_len = 0;
    for (const auto &command : gCommandQueue) {
        switch (command.type) {
            case AppCommand::ConnectToDevice: {
                COMHandle *handle = setup_com_port(command.data.com_path);
                if (handle) {
                    comms->connected_com_path = command.data.com_path;
                    comms->com_connection = handle;
                    comms->connection_status = COMConnectionStatus::CONNECTED;
                    set_window_title(std::format("Connected to: {}", command.data.com_path));
                } else {
                    comms->connection_status = COMConnectionStatus::CONNECTION_ERROR;
                }
                break;
            }
            case AppCommand::StartCCDOperation: {
                u8 buffer[64];
                Payload payload = {buffer, buffer, sizeof(buffer)};
                serialize(&payload, HostToDeviceCommand::CCDSensor);
                u32 id = get_next_ccd_result_id();
                serialize(&payload, id);
                serialize(&payload, command.data.ccd_op.iterations);
                serialize(&payload, command.data.ccd_op.exposure);
                serialize(&payload, (u16)420 /*crc*/);

                u8 cobs[32];
                CobsCtx ctx = cobs_encode_init(cobs, sizeof(cobs));
                assert(cobs_encode(&ctx, buffer, get_size(&payload)));
                u32 cobs_size = cobs_encode_end(&ctx);

                LOG_NORM("Sending CCD command: Id={}, Exposure={}, Iterations={}",
                         id,
                         command.data.ccd_op.exposure,
                         command.data.ccd_op.iterations);

                assert(write_to_com_device(comms->com_connection, cobs, cobs_size));
                break;
            }
            case AppCommand::CCDOperationUpdateName: {
                if (command.data.operation_to_update < app->ccd_operations.size()) {
                    LOG_ERROR("Trying to update a non exising operation [{}] while current max is [{}]",
                              command.data.operation_to_update,
                              app->ccd_operations.size());
                    break;
                }
                db_ccd_result_update_name(command.data.operation_to_update,
                                          app->ccd_operations[command.data.operation_to_update].name);
                break;
            }
            case AppCommand::CCDOperationUpdateNote: {
                if (command.data.operation_to_update < app->ccd_operations.size()) {
                    LOG_ERROR("Trying to update a non exising operation [{}] while current max is [{}]",
                              command.data.operation_to_update,
                              app->ccd_operations.size());
                    break;
                }
                db_ccd_result_update_name(command.data.operation_to_update,
                                          app->ccd_operations[command.data.operation_to_update].note);
                break;
            }
            case AppCommand::CCDOperationLoad: {
                app->ccd_operations.clear();
                db_ccd_result_get_by_time_range(command.data.start_date, command.data.end_date, &app->ccd_operations);
                break;
            }
            case AppCommand::DecodeIncommingData: {
                // NOTE we keep this buffer for as long as the program is running
                static u8 *decode_buffer = (u8 *)calloc(1, 1 << 20);

                u16 len = cobs_decode(comms->read_data_buffer + command.data.frame_start,
                                      command.data.frame_end - command.data.frame_start,
                                      decode_buffer,
                                      1 << 20);

                incomming_data_decoded_len += command.data.frame_end - command.data.frame_start;

                Payload payload = {decode_buffer, decode_buffer, len};

                DeviceToHostResponse cmd;
                deserialize(&payload, &cmd);

#define OK(x)                                    \
    if (!(x)) {                                  \
        LOG_ERROR("Failed deserializing field"); \
        break;                                   \
    }

                switch (cmd) {
                    case DeviceToHostResponse::CCDResult: {
                        u32 id;
                        OK(deserialize(&payload, &id))
                        u32 iterations;
                        OK(deserialize(&payload, &iterations))
                        u32 exposure;
                        OK(deserialize(&payload, &exposure))
                        u32 pixel_count;
                        OK(deserialize(&payload, &pixel_count));
                        LOG_NORM("Got CCD result for [{}] with [{}] elements", id, pixel_count);

                        using namespace std::chrono;
                        auto now = time_point_cast<seconds>(current_zone()->to_local(system_clock::now()));
                        CCDOperation op = {id, now, exposure, iterations};
                        // TODO validate that pixel count is not something crazy
                        assert(pixel_count < 5000);
                        op.accumulated_values.reserve(pixel_count);
                        for (u32 i = 0; i < pixel_count; ++i) {
                            u32 &n = op.accumulated_values.emplace_back();
                            if (!deserialize(&payload, &n)) {
                                break;
                            }
                        }

                        s64 created_id = db_ccd_result_create(now.time_since_epoch(),
                                                              exposure,
                                                              iterations,
                                                              op.accumulated_values.data(),
                                                              op.accumulated_values.size());
                        assert(id == created_id);

                        app->ccd_operations.push_back(std::move(op));
                        break;
                    }
                    case DeviceToHostResponse::Log: {
                        LogSeverity level;
                        OK(deserialize(&payload, &level));
                        u32 line;
                        OK(deserialize(&payload, &line));
                        u32 func_name_len;
                        const u8 *func_name;
                        OK(deserialize(&payload, &func_name, &func_name_len));
                        u32 msg_len;
                        const u8 *msg;
                        OK(deserialize(&payload, &msg, &msg_len));
                        log_impl(LogContext::DEVICE,
                                 {},
                                 {(char *)func_name, func_name_len},
                                 line,
                                 LogSeverity(level),
                                 {(char *)msg, msg_len});
                        break;
                    }
                    default: {
                        break;
                    }
                }
#undef OK
                break;
            }
            default: {
                LOG_ERROR("Got unkwnown command [{}]", (u32)command.type);
                break;
            }
        }
    }

    if (incomming_data_decoded_len != 0) {
        comms->read_data_size = comms->read_data_size - incomming_data_decoded_len;
        if (comms->read_data_size != 0) {
            memmove(
                comms->read_data_buffer, comms->read_data_buffer + incomming_data_decoded_len, comms->read_data_size);
        }
    }
    gCommandQueue.clear();
}
