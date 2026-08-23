//
// Created by RGAA on 2023-08-17.
//

#ifndef SAILFISH_CLIENT_PC_STREAMDBMANAGER_H
#define SAILFISH_CLIENT_PC_STREAMDBMANAGER_H

#include <any>
#include <vector>
#include <memory>
#include <string>
#include <optional>

namespace px_console
{
    class ConsoleStream;
}

namespace px
{
    class PxDatabase;

    class StreamDBOperator {
    public:

        StreamDBOperator(const std::shared_ptr<PxDatabase>& db);
        ~StreamDBOperator();
        static std::string GenUUID();
        void AddStream(const std::shared_ptr<px_console::ConsoleStream>& stream);
        bool UpdateStream(std::shared_ptr<px_console::ConsoleStream> stream);
        bool UpdateStreamRandomPwd(const std::string& stream_id, const std::string& random_pwd);
        bool UpdateStreamSafetyPwd(const std::string& stream_id, const std::string& safety_pwd);
        //std::vector<px_console::ConsoleStream> GetAllStreams();
        std::vector<std::shared_ptr<px_console::ConsoleStream>> GetAllStreamsSortByCreatedTime(bool increase = false);
        std::vector<std::shared_ptr<px_console::ConsoleStream>> GetStreamsSortByCreatedTime(int page, int page_size, bool increase = false);
        std::optional<std::shared_ptr<px_console::ConsoleStream>> GetStreamByStreamId(const std::string& stream_id);
        std::optional<std::shared_ptr<px_console::ConsoleStream>> GetStreamByHostPort(const std::string& host, int port);
        std::optional<std::shared_ptr<px_console::ConsoleStream>> GetStreamByRemoteDeviceId(const std::string& remote_device_id);
        void DeleteStream(int id);
        int RandomColor();
        bool HasStream(const std::string& stream_id);
        void Clear();

    private:
        //void CreateTables();

    private:
        std::shared_ptr<PxDatabase> db_ = nullptr;

    };

}

#endif //SAILFISH_CLIENT_PC_STREAMDBMANAGER_H
