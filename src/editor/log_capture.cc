#include "editor/log_capture.h"

#include <cstdio>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#define UT_PIPE(fds) _pipe(fds, 65536, _O_BINARY)
#define UT_DUP _dup
#define UT_DUP2 _dup2
#define UT_READ(fd, buffer, size) _read(fd, buffer, static_cast<unsigned int>(size))
#define UT_WRITE(fd, buffer, size) _write(fd, buffer, static_cast<unsigned int>(size))
#define UT_CLOSE _close
#else
#include <poll.h>
#include <unistd.h>
#define UT_PIPE(fds) pipe(fds)
#define UT_DUP dup
#define UT_DUP2 dup2
#define UT_READ(fd, buffer, size) read(fd, buffer, size)
#define UT_WRITE(fd, buffer, size) write(fd, buffer, size)
#define UT_CLOSE close
#endif

namespace ut::editor {
    namespace {
        constexpr size_t MaxLines = 10000;

        bool readable(int fd)
        {
#ifdef _WIN32
            DWORD available = 0;
            if (!PeekNamedPipe(reinterpret_cast<HANDLE>(_get_osfhandle(fd)), nullptr, 0, nullptr, &available, nullptr)) {
                return true;
            }
            if (available == 0) {
                Sleep(50);
            }
            return available > 0;
#else
            pollfd entry { fd, POLLIN, 0 };
            return poll(&entry, 1, 50) > 0;
#endif
        }
    }

    LogCapture::~LogCapture()
    {
        stop();
    }

    int LogCapture::originalStderr() const
    {
        return original;
    }

    bool LogCapture::start()
    {
        if (original >= 0) {
            return true;
        }
        int fds[2] = { -1, -1 };
        if (UT_PIPE(fds) != 0) {
            return false;
        }
        std::fflush(stderr);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        original = UT_DUP(2);
        UT_DUP2(fds[1], 2);
        UT_CLOSE(fds[1]);
        pipeRead = fds[0];
        stopping = false;
        reader = std::thread([this]() {
            pump();
        });
        return true;
    }

    void LogCapture::stop()
    {
        if (original < 0) {
            return;
        }
        std::fflush(stderr);
        UT_DUP2(original, 2);
        stopping = true;
        reader.join();
        UT_CLOSE(pipeRead);
        UT_CLOSE(original);
        pipeRead = -1;
        original = -1;
    }

    void LogCapture::pump()
    {
        char buffer[4096];
        while (true) {
            bool finishing = stopping;
            if (!readable(pipeRead)) {
                if (finishing) {
                    break;
                }
                continue;
            }
            auto count = UT_READ(pipeRead, buffer, sizeof(buffer));
            if (count <= 0) {
                break;
            }
            size_t size = static_cast<size_t>(count);
            auto written = UT_WRITE(original, buffer, size);
            static_cast<void>(written);
            std::lock_guard<std::mutex> lock(mutex);
            for (size_t i = 0; i < size; i++) {
                if (buffer[i] == '\n') {
                    captured.push_back(partial);
                    partial.clear();
                } else if (buffer[i] != '\r') {
                    partial += buffer[i];
                }
            }
            if (captured.size() > MaxLines) {
                captured.erase(captured.begin(), captured.begin() + static_cast<std::ptrdiff_t>(captured.size() - MaxLines));
            }
        }
    }

    void LogCapture::add(const std::string& line)
    {
        std::lock_guard<std::mutex> lock(mutex);
        captured.push_back(line);
    }

    std::vector<std::string> LogCapture::lines()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return captured;
    }

    void LogCapture::clear()
    {
        std::lock_guard<std::mutex> lock(mutex);
        captured.clear();
    }
}
