// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2015 Intel Corporation. All Rights Reserved.

#pragma once

#include <vector>

#include "device.h"
#include "context.h"
#include "backend.h"
#include "ds5-private.h"
#include "hw-monitor.h"
#include "image.h"
#include <mutex>

namespace rsimpl
{
    static const std::vector<std::uint16_t> rs4xx_sku_pid = { ds::RS400P_PID, ds::RS410A_PID, ds::RS420R_PID, ds::RS430C_PID, ds::RS450T_PID };

    class ds5_camera;

    class ds5_timestamp_reader : public frame_timestamp_reader
    {
        static const int pins = 2;
        std::vector<bool> started;
        std::vector<int64_t> total;
        std::vector<int> last_timestamp;
        mutable std::vector<int64_t> counter;
        mutable std::recursive_mutex _mtx;
    public:
        ds5_timestamp_reader()
            : started(pins), total(pins),
              last_timestamp(pins), counter(pins)
        {
            reset();
        }

        void reset() override
        {
            std::lock_guard<std::recursive_mutex> lock(_mtx);
            for (auto i = 0; i < pins; ++i)
            {
                started[i] = false;
                total[i] = 0;
                last_timestamp[i] = 0;
                counter[i] = 0;
            }
        }

        bool validate_frame(const request_mapping& mode, const void * frame) const override
        {
            std::lock_guard<std::recursive_mutex> lock(_mtx);
            // Validate that at least one byte of the image is nonzero
            for (const uint8_t * it = (const uint8_t *)frame, *end = it + mode.pf->get_image_size(mode.profile.width, mode.profile.height); it != end; ++it)
            {
                if (*it)
                {
                    return true;
                }
            }

            return false;
        }

        double get_frame_timestamp(const request_mapping& /*mode*/, const void * frame) override
        {
            std::lock_guard<std::recursive_mutex> lock(_mtx);
            // TODO: generate timestamp
            return 0;
        }

        unsigned long long get_frame_counter(const request_mapping & mode, const void * /*frame*/) const override
        {
            std::lock_guard<std::recursive_mutex> lock(_mtx);
            int pin_index = 0;
            if (mode.pf->fourcc == 0x5a313620) // Z16
                pin_index = 1;

            return ++counter[pin_index];
        }
    };

    class ds5_hid_timestamp_reader : public frame_timestamp_reader
    {
        static const int sensors = 2;
        std::vector<bool> started;
        std::vector<int64_t> total;
        std::vector<int> last_timestamp;
        mutable std::vector<int64_t> counter;
        mutable std::recursive_mutex _mtx;
    public:
        ds5_hid_timestamp_reader()
        {
            started.resize(sensors);
            total.resize(sensors);
            last_timestamp.resize(sensors);
            counter.resize(sensors);
            reset();
        }

        void reset() override
        {
            std::lock_guard<std::recursive_mutex> lock(_mtx);
            for (auto i = 0; i < sensors; ++i)
            {
                started[i] = false;
                total[i] = 0;
                last_timestamp[i] = 0;
                counter[i] = 0;
            }
        }

        bool validate_frame(const request_mapping& mode, const void * frame) const override
        {
            std::lock_guard<std::recursive_mutex> lock(_mtx);
            // Validate that at least one byte of the image is nonzero
            for (const uint8_t * it = (const uint8_t *)frame, *end = it + mode.pf->get_image_size(mode.profile.width, mode.profile.height); it != end; ++it)
            {
                if (*it)
                {
                    return true;
                }
            }

            return false;
        }

        double get_frame_timestamp(const request_mapping& mode, const void * frame) override
        {
            std::lock_guard<std::recursive_mutex> lock(_mtx);
            auto frame_size = mode.profile.width * mode.profile.height;
            static const unsigned hid_data_size = 14;
            static const unsigned timestamp_offset = 6;
            if (frame_size == hid_data_size)
            {
                 return static_cast<double>(*((uint64_t*)((const uint8_t*)frame + timestamp_offset)));
            }
            return 0; // TODO: return time_point
        }

        unsigned long long get_frame_counter(const request_mapping & mode, const void * /*frame*/) const override
        {
            std::lock_guard<std::recursive_mutex> lock(_mtx);
            int index = 0;
            if (mode.pf->fourcc == 'GYRO')
                index = 1;

            return ++counter[index];
        }
    };

    class ds5_info : public device_info
    {
    public:
        std::shared_ptr<device> create(const uvc::backend& backend) const override;

        uint8_t get_subdevice_count() const override
        {
            auto depth_pid = _depth.front().pid;
            switch(depth_pid)
            {
            case ds::RS400P_PID:
            case ds::RS410A_PID:
            case ds::RS420R_PID:
            case ds::RS430C_PID: return 1;
            case ds::RS450T_PID: return 3;
            default: 
                throw not_implemented_exception(to_string() <<
                    "get_subdevice_count is not implemented for DS5 device of type " <<
                    depth_pid);
            }
        }

        ds5_info(std::shared_ptr<uvc::backend> backend,
                 std::vector<uvc::uvc_device_info> depth,
                 uvc::usb_device_info hwm,
                 std::vector<uvc::hid_device_info> hid)
            : device_info(std::move(backend)), _hwm(std::move(hwm)),
              _depth(std::move(depth)), _hid(std::move(hid)) {}

        static std::vector<std::shared_ptr<device_info>> pick_ds5_devices(
                std::shared_ptr<uvc::backend> backend,
                std::vector<uvc::uvc_device_info>& uvc,
                std::vector<uvc::usb_device_info>& usb,
                std::vector<uvc::hid_device_info>& hid);

    private:
        std::vector<uvc::uvc_device_info> _depth;
        uvc::usb_device_info _hwm;
        std::vector<uvc::hid_device_info> _hid;
    };

    class ds5_camera final : public device
    {
    public:
        class emitter_option : public uvc_xu_option<uint8_t>
        {
        public:
            const char* get_value_description(float val) const override
            {
                switch (static_cast<int>(val))
                {
                    case 0:
                    {
                        return "Off";
                    }
                    case 1:
                    {
                        return "On";
                    }
                    case 2:
                    {
                        return "Auto";
                    }
                    default:
                        throw invalid_value_exception("value not found");
                }
            }

            explicit emitter_option(uvc_endpoint& ep) 
                : uvc_xu_option(ep, ds::depth_xu, ds::DS5_DEPTH_EMITTER_ENABLED,
                                "Power of the DS5 projector, 0 meaning projector off, 1 meaning projector off, 2 meaning projector in auto mode")
            {}
        };

        std::shared_ptr<hid_endpoint> create_hid_device(const uvc::backend& backend,
                                                        const std::vector<uvc::hid_device_info>& all_hid_infos);

        std::shared_ptr<uvc_endpoint> create_depth_device(const uvc::backend& backend,
                                                          const std::vector<uvc::uvc_device_info>& all_device_infos);

        uvc_endpoint& get_depth_endpoint()
        {
            return static_cast<uvc_endpoint&>(get_endpoint(_depth_device_idx));
        }

        ds5_camera(const uvc::backend& backend,
            const std::vector<uvc::uvc_device_info>& dev_info,
            const uvc::usb_device_info& hwm_device,
            const std::vector<uvc::hid_device_info>& hid_info);

        std::vector<uint8_t> send_receive_raw_data(const std::vector<uint8_t>& input) override;
        rs_intrinsics get_intrinsics(int subdevice, stream_profile profile) const override;

    private:
        bool is_camera_in_advanced_mode() const;

        const uint8_t _depth_device_idx;
        std::shared_ptr<hw_monitor> _hw_monitor;


        lazy<std::vector<uint8_t>> _coefficients_table_raw;

        std::vector<uint8_t> get_raw_calibration_table(ds::calibration_table_id table_id) const;

    };
}