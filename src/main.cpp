#include <args/Args.h>
#include <args/Parser.h>
#include <event/Loop.h>
#include <net/Fetch.h>
#include <log/Log.h>
#include <zlib.h>

using namespace reckoning;
using namespace reckoning::log;

struct Concat
{
    std::string base, hardBase;
    std::vector<std::string> parts;
    FILE* file;
} concat;

static inline bool isGzip(const std::shared_ptr<buffer::Buffer>& buf)
{
    if (!buf || buf->size() < 10)
        return false;
    return buf->data()[0] == 0x1f && buf->data()[1] == 0x8b;
}

static inline std::shared_ptr<buffer::Buffer> decompressGzip(const std::shared_ptr<buffer::Buffer>& gzip)
{
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.next_in = Z_NULL;
    strm.avail_in = 0;
    if (inflateInit2(&strm, 15 | 32)) {
        Log(Log::Error) << "Unable to initialize gzip";
        return {};
    }

    enum { BufSiz = 0x4000 };
    uint8_t out[BufSiz];

    strm.next_in = gzip->data();
    strm.avail_in = gzip->size();

    std::vector<uint8_t> decomp;
    size_t off = 0;
    int status;

    do {
        strm.avail_out = BufSiz;
        strm.next_out = out;

        status = inflate(&strm, Z_NO_FLUSH);
        switch (status) {
        case Z_OK:
        case Z_STREAM_END:
        case Z_BUF_ERROR:
            break;
        default:
            inflateEnd(&strm);
            Log(Log::Error) << "inflate error" << status;
            return {};
        }

        off = decomp.size();
        decomp.resize(decomp.size() + (BufSiz - strm.avail_out));
        memcpy(decomp.data() + off, out, BufSiz - strm.avail_out);
    } while (strm.avail_out == 0);

    inflateEnd(&strm);

    auto ret = buffer::Buffer::create(decomp.size());
    ret->assign(decomp.data(), decomp.size());
    return ret;
}

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
    if (!args.has<std::string>("output")) {
        Log(Log::Error) << "no output file name";
        return 1;
    }
    bool hardSet = false;
    bool hardBase = true;
    if (args.has<bool>("hard")) {
        hardBase = args.value<bool>("hard");
        hardSet = true;
    }

    const auto url = args.value<std::string>("url");
    const auto outputfn = args.value<std::string>("output");

    concat.file = fopen(outputfn.c_str(), "w");
    if (!concat.file) {
        Log(Log::Error) << "can't open" << outputfn;
        return 1;
    }

    std::shared_ptr<event::Loop> loop = event::Loop::create();

    if (!hardSet || !hardBase) {
        const auto base = url.rfind('/');
        if (base == std::string::npos) {
            Log(Log::Error) << "no uri base";
        }
        concat.base = url.substr(0, base + 1);
    }
    if (!hardSet || hardBase) {
        // first, find //
        auto base = url.find("://");
        if (base == std::string::npos) {
            Log(Log::Error) << "no uri base";
        } else {
            base = url.find("/", base + 4);
            if (base == std::string::npos) {
                Log(Log::Error) << "no uri base";
            } else {
                concat.hardBase = url.substr(0, base + 1);
                if (hardSet)
                    concat.base = url.substr(0, base + 1);
            }
        }
    }
    Log(Log::Info) << "uri base" << concat.base;

    auto fetch = net::Fetch::create();
    std::function<void(uint32_t)> downloadNext;

    downloadNext = [hardSet, &fetch, &downloadNext, &loop](uint32_t cur) {
        if (cur >= concat.parts.size()) {
            // done
            Log(Log::Info) << "done";
            fclose(concat.file);
            loop->exit();
            return;
        }
        std::string suburi;
        if (concat.parts[cur].find("://") != std::string::npos) {
            suburi = concat.parts[cur];
        } else {
            if (!hardSet && !concat.parts[cur].empty() && concat.parts[cur][0] == '/') {
                suburi = concat.hardBase + concat.parts[cur];
            } else {
                suburi = concat.base + concat.parts[cur];
            }
        }
        //Log(Log::Info) << "fetching" << suburi;
        fetch->fetch(suburi).then([&downloadNext, cur](std::shared_ptr<buffer::Buffer>&& buffer) -> void {
            Log(Log::Info) << "downloaded" << buffer->size() << (cur + 1) << "/" << concat.parts.size();
            fwrite(buffer->data(), buffer->size(), 1, concat.file);
            downloadNext(cur + 1);
        });
    };

    fetch->fetch(url).then([&downloadNext](std::shared_ptr<buffer::Buffer>&& buffer) -> void {
        // parse buffer, all lines that doesn't start with a '#' is go
        if (isGzip(buffer))
            buffer = decompressGzip(buffer);
        const char* data = reinterpret_cast<const char*>(buffer->data());
        size_t off = 0;
        const size_t max = buffer->size();
        while (off < max) {
            if (data[off] == '#') {
                // skip until eol
                while (off < max && data[off] != '\n')
                    ++off;
                if (off < max)
                    ++off;
                continue;
            }
            // read until eol, that's our new uri
            const size_t start = off;
            while (off < max && data[off] != '\n')
                ++off;
            auto suburi = std::string(data + start, off - start);
            Log(Log::Info) << "parsed sub uri" << suburi << (off - start);
            concat.parts.push_back(std::move(suburi));
            if (off < max)
                ++off;
        }

        downloadNext(0);
    });

    loop->execute();

    return 0;
}
