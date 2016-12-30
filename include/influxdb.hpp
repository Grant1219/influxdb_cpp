#ifndef INFLUXDB_CLIENT_HPP
#define INFLUXDB_CLIENT_HPP

#include <string>
#include <vector>
#include <chrono>
#include <regex>
#include <algorithm>
#include <curl/curl.h>
#include <fmt/format.h>

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

            friend class client;
    };

    class client {
        public:
            client(std::string url, std::string db, precision p)
                : base_url(url), database(db), ts_precision(p), running_handles(0) {
                mhandle = curl_multi_init();
                post_data.reserve(2048);

                // TODO handle curl_multi_init failure

                write_url = format_write_url(base_url, database);
            }

            ~client() {
                curl_multi_cleanup(mhandle);
            }

            void update() {
                CURLMcode rcode;
                prev_running_handles = running_handles;

                rcode = curl_multi_perform(mhandle, &running_handles);

                if (rcode != CURLM_OK) {
                    // TODO check return code,
                }

                if (prev_running_handles < running_handles) {
                    // remove any completed transfers
                    // TODO check for failures
                    int msgq;
                    cmsg = curl_multi_info_read(mhandle, &msgq);

                    do {
                        if (cmsg != nullptr && (cmsg->msg == CURLMSG_DONE)) {
                            CURL* handle = cmsg->easy_handle;

                            curl_multi_remove_handle(mhandle, handle);
                            curl_easy_cleanup(handle);
                        }
                    } while (cmsg != nullptr);
                }
            }

            void add_metric(metric& m) {
                post_data.append(m.get_line(ts_precision));
            }

            void write_metrics() {
                CURL* ehandle = curl_easy_init();

                if (ehandle) {
                    curl_easy_setopt(ehandle, CURLOPT_URL, &write_url[0]);
                    curl_easy_setopt(ehandle, CURLOPT_POSTFIELDSIZE, post_data.size());
                    curl_easy_setopt(ehandle, CURLOPT_COPYPOSTFIELDS, &post_data[0]);

                    running_handles++;
                    CURLMcode rcode = curl_multi_add_handle(mhandle, ehandle);

                    if (rcode != CURLM_OK) {
                        // TODO do something
                    }

                    post_data.clear();
                }
            }

            bool is_active() {
                return running_handles > 0;
            }

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
            std::string post_data;

            int running_handles;
            int prev_running_handles;
    };
}

#endif
