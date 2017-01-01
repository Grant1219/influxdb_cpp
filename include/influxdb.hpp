#ifndef INFLUXDB_CLIENT_HPP
#define INFLUXDB_CLIENT_HPP

#include <string>
#include <vector>
#include <chrono>
#include <regex>
#include <algorithm>
#include <exception>
#include <curl/curl.h>

#ifndef FMT_HEADER_ONLY
#define FMT_HEADER_ONLY
#endif
#include "fmt/format.h"

namespace influxdb {
    inline bool initialize() {
        return (curl_global_init(CURL_GLOBAL_ALL) == 0);
    }

    inline void cleanup() {
        curl_global_cleanup();
    }

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
                timestamp(std::chrono::system_clock::now()),
                quote_re("\"") {}

            template<typename T>
            metric& add_tag(const std::string& key, const T& val) {
                tags.push_back(fmt::format("{}={}", key, val));
                return *this;
            }

            template<typename T>
            metric& add_field(const std::string& key, const T& val) {
                fields.push_back(fmt::format("{}={}", key, val));
                return *this;
            }

            metric& add_field(const std::string& key, const std::string& val) {
                fields.push_back(fmt::format("{}=\"{}\"", key, std::regex_replace(val, quote_re, "\\\"")));
                return *this;
            }

            metric& add_field(const std::string& key, const char* val) {
                fields.push_back(fmt::format("{}=\"{}\"", key, std::regex_replace(val, quote_re, "\\\"")));
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
            std::regex quote_re;

            friend class influxdb_client;
    };

    class client {
        public:
            virtual void update() {}
            virtual void add_metric(metric& m) {}
            virtual void write_metrics() {}
            virtual bool is_active() { return false; }
    };

    class dummy_client : public client {};

    class influxdb_client : public client {
        public:
            influxdb_client(std::string url, std::string db, precision p,
                            size_t buffer_size = 2048, bool save_failures = false)
                : base_url(url), database(db), ts_precision(p),
                  max_buffer(buffer_size), save_failures(save_failures),
                  running_handles(0) {
                mhandle = curl_multi_init();

                if (mhandle == nullptr)
                    throw std::runtime_error("Failed to initialize curl multi interface");

                post_data.reserve(max_buffer);
                write_url = format_write_url(base_url, database);
            }

            ~influxdb_client() {
                curl_multi_cleanup(mhandle);
            }

            void update() final override {
                CURLMcode rcode;
                prev_running_handles = running_handles;

                rcode = curl_multi_perform(mhandle, &running_handles);

                if (rcode != CURLM_OK)
                    throw std::runtime_error(curl_multi_strerror(rcode));

                if (running_handles < prev_running_handles) {
                    // remove any completed transfers
                    // and store errors
                    do {
                        int msgq;
                        cmsg = curl_multi_info_read(mhandle, &msgq);

                        if (cmsg != nullptr && (cmsg->msg == CURLMSG_DONE)) {
                            CURL* handle = cmsg->easy_handle;

                            if (save_failures && cmsg->data.result != CURLE_OK)
                                failed_transfers.push_back(curl_easy_strerror(cmsg->data.result));

                            curl_multi_remove_handle(mhandle, handle);
                            curl_easy_cleanup(handle);
                        }
                    } while (cmsg != nullptr);
                }
            }

            void add_metric(metric& m) final override {
                post_data.append(m.get_line(ts_precision));

                if (post_data.size() >= max_buffer)
                    write_metrics();
            }

            void write_metrics() final override {
                CURL* ehandle = curl_easy_init();

                if (ehandle) {
                    curl_easy_setopt(ehandle, CURLOPT_URL, &write_url[0]);
                    curl_easy_setopt(ehandle, CURLOPT_POSTFIELDSIZE, post_data.size());
                    curl_easy_setopt(ehandle, CURLOPT_COPYPOSTFIELDS, &post_data[0]);

                    running_handles++;
                    CURLMcode rcode = curl_multi_add_handle(mhandle, ehandle);

                    if (rcode != CURLM_OK)
                        throw std::runtime_error(curl_multi_strerror(rcode));

                    post_data.clear();
                }
                else
                    throw std::runtime_error("Failed to initialize curl easy handle");
            }

            bool is_active() final override {
                return running_handles > 0;
            }

            const std::vector<std::string>& get_failures() { return failed_transfers; }
            void clear_failures() { failed_transfers.clear(); }

        private:
            std::string format_write_url(const std::string& base_url, const std::string& db) {
                std::string new_url(base_url);
                new_url.append("/write?db=");
                new_url.append(db);

                // TODO handle authentication

                switch (ts_precision) {
                    case precision::nano:
                        new_url.append("&precision=n");
                        break;
                    case precision::micro:
                        new_url.append("&precision=u");
                        break;
                    case precision::milli:
                        new_url.append("&precision=ms");
                        break;
                    case precision::second:
                        new_url.append("&precision=s");
                        break;
                    case precision::minute:
                        new_url.append("&precision=m");
                        break;
                    case precision::hour:
                        new_url.append("&precision=h");
                        break;
                }

                return new_url;
            }

            std::string base_url;
            std::string write_url;
            std::string database;
            precision ts_precision;

            CURLM* mhandle;
            CURLMsg* cmsg;
            size_t max_buffer;
            std::string post_data;
            std::vector<std::string> failed_transfers;
            bool save_failures;

            int running_handles;
            int prev_running_handles;
    };
}

#endif
