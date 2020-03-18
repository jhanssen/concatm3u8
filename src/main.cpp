#include <args/Args.h>
#include <args/Parser.h>
#include <event/Loop.h>
#include <net/Fetch.h>
#include <log/Log.h>

using namespace reckoning;
using namespace reckoning::log;

struct Concat
{
    std::string base;
    std::vector<std::string> parts;
} concat;

int main(int argc, char** argv)
{
    auto args = args::Parser::parse(argc, argv);

    Log::Level level = Log::Debug;
    if (args.has<std::string>("level")) {
        const auto& slevel = args.value<std::string>("level");
        if (slevel == "debug")
            level = Log::Debug;
        else if (slevel == "info")
            level = Log::Info;
        else if (slevel == "warn")
            level = Log::Warn;
        else if (slevel == "error")
            level = Log::Error;
        else if (slevel == "fatal")
            level = Log::Fatal;
    }

    Log::initialize(level);

    if (!args.has<std::string>("url")) {
        Log(Log::Error) << "no url";
        return 1;
    }

    const auto url = args.get<std::string>("url");

    std::shared_ptr<event::Loop> loop = event::Loop::create();

    const auto base = url.rfind('/');
    if (base == std::string::npos) {
        Log(Log::error) << "no uri base";
    }
    concat.base = url.substr(0, base + 1);
    Log(Log::Info) << "uri base" << concat.base;

    auto fetch = net::Fetch::create();
    fetch->fetch(url).then([](std::shared_ptr<buffer::Buffer>&& buffer) -> auto& {
        // parse buffer, all lines that doesn't start with a '#' is go
        const char* data = reinterpret_cast<const char*>(buffer.data());
        size_t off = 0;
        const size_t max = buffer.size();
        while (off < max) {
            if (data[off] == '#') {
                // skip until eol
                while (off < max && data[off] != '\n')
                    ++off;
                continue;
            }
            // read until eol, that's our new uri
            const size_t start = off;
            while (off < max && data[off] != '\n')
                ++off;
            auto suburi = std::string(data + start, off - start);
            Log(Log::Info) << "parsed sub uri" << suburi;
            concat.parts.push_back(std::move(suburi));
        }
    });

    loop->execute();

    return 0;
}
