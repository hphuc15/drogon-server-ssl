#include "api_data.h"
#include <json/json.h>

using namespace api;

namespace api
{
    void data::postData(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback)
    {
        auto jsonStr = req->getJsonObject();
        if (!jsonStr)
        {
            Json::Value status;
            status["Error"] = "Invalid JSON";
            HttpResponsePtr resp = HttpResponse::newHttpJsonResponse(status);
            callback(resp);
            return;
        }


        int sensor_id = (*jsonStr)["sensor_id"].asInt();
        float light = (*jsonStr)["light"].asFloat();

        auto client = drogon::app().getDbClient("default");

        // std::cout << "DB CLIENT PTR = " << client.get() << std::endl; // debug

        std::string SQL_insert = "INSERT INTO bh1750 (sensor_id, light) VALUES (?, ?)";
        client->execSqlAsync(
            SQL_insert,
            [=](const drogon::orm::Result &result)
            {
                std::cout << result.affectedRows() << " inserted!" << std::endl;
                Json::Value status;
                status["Status"] = "Received";
                // Debug: check data by client
                status["sensor_id"] = sensor_id;
                status["light"] = light;
                // std::cout << sensor_id << std::endl; // debug
                // spdlog::info("Light: {}", sensor_id);
                auto response = HttpResponse::newHttpJsonResponse(status);
                callback(response);
            },
            [=](const drogon::orm::DrogonDbException &e)
            {
                std::cerr << "error: " << e.base().what() << std::endl;
                Json::Value status;
                status["Error"] = e.base().what();
                auto response = HttpResponse::newHttpJsonResponse(status);
                callback(response);
            },
            sensor_id,
            light);

        // test: curl -X POST http://localhost:5000/api/data/postData -H "Content-Type: application/json" -d "{\"sensor_id\":105,\"light\":192}"
    }

    void data::getData(const HttpRequestPtr &req, std::function<void(const HttpResponsePtr &)> &&callback)
    {
        // Get Database client
        auto client = drogon::app().getDbClient("default");

        // Get data filter parameters
        DataFilter_t s_DataFilter = {
            .sensor_id = req->getParameter("sensor_id"),
            .date = req->getParameter("date"),
            .sort = {req->getParameter("sort")},
            .level = req->getParameter("level")};
        s_DataFilter.to_valid_sort(); // Delete null string in sort

        // Create query string
        std::string SQL_str = "SELECT sensor_id, date, time, light FROM bh1750 WHERE 1=1";

        // Check filter parameter
        if (s_DataFilter.is_datafilter_empty())
        {
            SQL_str.append(" ORDER BY date DESC, time DESC LIMIT 30;");
        }
        else
        {
            /*
            - Filtering by sensor_id
            - Filtering by date
            - Filtering by light intensity level (low, medium, high)
            - Sort by date DESC, time DESC (default)
            - Sort by light DESC, ASC
            */

            // ============ DATA FILTER BLOCK ==============
            // Search
            if (!s_DataFilter.sensor_id.empty() || !s_DataFilter.date.empty())
            {
                std::string SQL_search;

                // Sensor ID filter
                if (!s_DataFilter.sensor_id.empty())
                {
                    SQL_search.append(" AND sensor_id = ");
                    SQL_search.append(s_DataFilter.sensor_id);
                }

                // Date filter
                if (!s_DataFilter.date.empty())
                {
                    SQL_search.append(" AND date = \"");
                    SQL_search.append(s_DataFilter.date);
                    SQL_search.append("\"");
                }

                // Light level filter
                if (s_DataFilter.level == "low") // [0, 1000)
                {
                    SQL_search.append(" AND light >= 0 AND light < 1000");
                }
                else if (s_DataFilter.level == "medium") // [1000, 10000)
                {
                    SQL_search.append(" AND light >= 1000 AND light < 10000");
                }
                else if (s_DataFilter.level == "high") // [10000, max)
                {
                    SQL_search.append(" AND light >= 10000");
                }

                SQL_str.append(SQL_search);
            }
            // Sort
            if (!s_DataFilter.sort.empty())
            {
                int s_comma_count = s_DataFilter.sort.size() - 1;
                std::string SQL_sort = " ORDER BY";
                for (auto param : s_DataFilter.sort)
                {
                    SQL_sort.append(" ");
                    SQL_sort.append(param);
                    SQL_sort.append(" DESC");
                    while (s_comma_count != 0)
                    {
                        SQL_sort.append(",");
                        s_comma_count--;
                    }
                }
                SQL_str.append(SQL_sort);
            }
            SQL_str.append(" LIMIT 30;");
        }



        // ============== Execute ==================
        client->execSqlAsync(
            SQL_str,
            [=](const drogon::orm::Result &result)
            {
                std::cout << result.size() << " rows selected" << std::endl;

                std::vector<std::map<std::string, std::string>> dataList;
                for (auto row : result)
                {
                    std::map<std::string, std::string> rowData;
                    rowData["sensor_id"] = std::to_string(row["sensor_id"].as<int>());
                    rowData["date"] = row["date"].as<std::string>();
                    rowData["time"] = row["time"].as<std::string>();
                    rowData["light"] = std::to_string(row["light"].as<float>());
                    dataList.push_back(rowData);
                }

                HttpViewData viewData;
                viewData.insert("data", dataList);

                auto resp = drogon::HttpResponse::newHttpViewResponse("data.csp", viewData);
                callback(resp);
            },

            [=](const drogon::orm::DrogonDbException &e)
            {
                std::cerr << "error: " << e.base().what() << std::endl;
                Json::Value status;
                status["Error"] = e.base().what();
                auto response = HttpResponse::newHttpJsonResponse(status);
                callback(response);
            });
    }
}
