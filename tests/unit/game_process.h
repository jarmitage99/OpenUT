#pragma once

#if defined(OPENUT_PATH) && !defined(_WIN32)

#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace testprocess {
    class GameProcess {
    public:
        GameProcess(const std::string& arguments, const std::filesystem::path& logFile, const std::string& environment)
            : log(logFile)
        {
            std::string command = environment + " OPENUT_GAME_DATA=\"" OPENUT_GAME_DATA "\" XDG_DATA_HOME=\"" + log.parent_path().string() + "\" \"" OPENUT_PATH "\" " + arguments + " --control > \"" + log.string() + "\" 2>&1";
            std::signal(SIGPIPE, SIG_IGN);
            pipe = popen(command.c_str(), "w");
        }

        ~GameProcess()
        {
            send("exec quit");
            pclose(pipe);
        }

        GameProcess(const GameProcess&) = delete;
        GameProcess& operator=(const GameProcess&) = delete;

        void send(const std::string& line)
        {
            std::fprintf(pipe, "%s\n", line.c_str());
            std::fflush(pipe);
        }

        std::string output() const
        {
            std::ifstream file(log);
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        }

        std::vector<std::string> ask(const std::string& command, const std::string& prefix)
        {
            size_t mark = output().size();
            send(command);
            std::vector<std::string> lines;
            for (int attempt = 0; attempt < 200; attempt++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                std::string reply = output().substr(mark);
                if (reply.find("done\n") == std::string::npos) {
                    continue;
                }
                std::istringstream text(reply);
                for (std::string line; std::getline(text, line);) {
                    if (line.rfind(prefix, 0) == 0) {
                        lines.push_back(line);
                    }
                }
                break;
            }
            return lines;
        }

    private:
        std::filesystem::path log;
        FILE* pipe = nullptr;
    };

    struct ScratchDirectory {
        ScratchDirectory()
            : path(std::filesystem::temp_directory_path() / ("openut-net-test-" + std::to_string(getpid())))
        {
            std::filesystem::remove_all(path);
            std::filesystem::create_directories(path);
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }

        std::filesystem::path path;
    };

    inline bool eventually(const std::function<bool()>& condition, int seconds = 30)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        while (std::chrono::steady_clock::now() < deadline) {
            if (condition()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return false;
    }
}

#endif
