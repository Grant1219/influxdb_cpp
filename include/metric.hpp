#ifndef INFLUXDB_BATCH_HPP
#define INFLUXDB_BATCH_HPP

#include <string>
#include <chrono>
#include <fmt/format.h>

namespace influxdb {
    class client;

    enum class precision : uint8_t {
        nano,
        micro,
        milli,
        second,
        minute,
        hour
    };

    class metric {
        public:
            metric(const std::string& measurement)
                : measurement(measurement),
                timestamp(std::chrono::system_clock::now()) {}

            template<typename T>
            metric& add_tag(const std::string& key, const T& val) {
                // TODO escape fields
                tags.push_back(fmt::format("{}={}", key, val));
                return *this;
            }

            template<typename T>
            metric& add_field(const std::string& key, const T& val) {
                // TODO escape fields
                fields.push_back(fmt::format("{}={}", key, val));
                return *this;
            }

        private:
            uint64_t get_timestamp(precision p) {
                using namespace std::chrono;

                switch (p) {
                    case precision::nano:
                        return duration_cast<nanoseconds>(timestamp.time_since_epoch()).count();
                    case precision::micro:
                        return duration_cast<microseconds>(timestamp.time_since_epoch()).count();
                    case precision::milli:
                        return duration_cast<milliseconds>(timestamp.time_since_epoch()).count();
                    case precision::second:
                        return duration_cast<seconds>(timestamp.time_since_epoch()).count();
                    case precision::minute:
                        return duration_cast<minutes>(timestamp.time_since_epoch()).count();
                    case precision::hour:
                        return duration_cast<hours>(timestamp.time_since_epoch()).count();
                    default:
                        return duration_cast<nanoseconds>(timestamp.time_since_epoch()).count();
                }
            }

            std::string get_line(precision p) {
                fmt::MemoryWriter out;
                out.write("{}", measurement);

                for (const auto& tag : tags)
                    out.write(",{}", tag);

                auto itr = fields.begin();
                out.write(" {}", *itr);
                itr++;

                while (itr != fields.end()) {
                    out.write(",{}", *itr);
                    itr++;
                }

                out.write(" {}\n", get_timestamp(p));

                return out.str();
            }

            std::string measurement;
            std::vector<std::string> tags;
            std::vector<std::string> fields;
            std::chrono::system_clock::time_point timestamp;

            friend class client;
    };
}

#endif
