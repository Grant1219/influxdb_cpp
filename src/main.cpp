#include <iostream>
#include <thread>
#include <influxdb.hpp>

int main(int argc, char** argv) {
    std::cout << "Initializing" << std::endl;
    influxdb::initialize();

    {
        auto client = influxdb::client("http://localhost:8086", "test_db", influxdb::precision::milli);

        std::cout << "Creating metrics" << std::endl;
        client.add_metric(influxdb::metric("user_logins").add_field("count", 1));
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        client.add_metric(influxdb::metric("user_logins").add_field("count", 2));
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        client.add_metric(influxdb::metric("user_logins").add_field("count", 1));
        std::string sentence = "The phrase \"Hello there!\" is a greeting.";
        client.add_metric(influxdb::metric("string_test").add_field("value", sentence));
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        client.add_metric(influxdb::metric("string_test").add_field("value", "This is a \"string literal\" for testing"));

        std::cout << "Writing metrics" << std::endl;
        client.write_metrics();

        while (client.is_active()) {
            client.update();
        }

        std::cout << "Finished writing" << std::endl;
    }

    std::cout << "Cleaning up" << std::endl;
    influxdb::cleanup();
    return 0;
}
