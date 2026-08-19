// Copyright (c) 2026 The Veil developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <veil/snapshot/snapshot_extract.h>

#include <veil/snapshot/zstd.h>

#include <cstdio>
#include <cstring>
#include <memory>

namespace snapshot {

namespace {

constexpr size_t TAR_BLOCK = 512;

//! Parses the size field of a tar header: octal, or base-256 for large files.
uint64_t ParseTarSize(const unsigned char* p, size_t len)
{
    if (p[0] & 0x80) {
        // base-256: big endian binary in the remaining bytes
        uint64_t v = p[0] & 0x7f;
        for (size_t i = 1; i < len; i++) v = (v << 8) | p[i];
        return v;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < len && p[i]; i++) {
        if (p[i] == ' ') continue;
        if (p[i] < '0' || p[i] > '7') break;
        v = (v << 3) | uint64_t(p[i] - '0');
    }
    return v;
}

//! Entry paths come from the network, so be strict: relative, no "..", and
//! the first component has to be one of the directories we expect.
bool SafeEntryPath(const std::string& name, const std::set<std::string>& allowedDirs, std::string& error)
{
    if (name.empty() || name[0] == '/') {
        error = "archive entry with absolute or empty path: " + name;
        return false;
    }
    std::string first = name.substr(0, name.find('/'));
    if (!allowedDirs.count(first)) {
        error = "archive entry outside the expected folders: " + name;
        return false;
    }
    // reject any ".." component
    size_t pos = 0;
    while (pos <= name.size()) {
        size_t next = name.find('/', pos);
        std::string comp = name.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        if (comp == "..") {
            error = "archive entry tries to escape with ..: " + name;
            return false;
        }
        if (next == std::string::npos) break;
        pos = next + 1;
    }
    return true;
}

//! Consumes the decompressed byte stream and writes tar entries to disk.
class TarConsumer
{
public:
    TarConsumer(const fs::path& destDir, const std::set<std::string>& allowedDirs)
        : m_dest(destDir), m_allowed(allowedDirs) {}

    ~TarConsumer()
    {
        if (m_file) fclose(m_file);
    }

    bool Feed(const unsigned char* data, size_t len, std::string& error)
    {
        while (len > 0) {
            if (m_done) return true; // trailing padding after the end marker
            if (m_dataLeft > 0) {
                size_t take = std::min<uint64_t>(len, m_dataLeft);
                if (m_file) {
                    if (fwrite(data, 1, take, m_file) != take) {
                        error = "failed writing " + m_currentName;
                        return false;
                    }
                }
                m_dataLeft -= take;
                data += take;
                len -= take;
                if (m_dataLeft == 0) {
                    if (m_file) {
                        if (fclose(m_file) != 0) {
                            m_file = nullptr;
                            error = "failed closing " + m_currentName;
                            return false;
                        }
                        m_file = nullptr;
                    }
                    m_dataLeft = m_padLeft;
                    m_padLeft = 0;
                    // the padding bytes fall through the same path with no file open
                }
                continue;
            }
            // assembling a 512 byte header block
            size_t need = TAR_BLOCK - m_headerFill;
            size_t take = std::min(need, len);
            memcpy(m_header + m_headerFill, data, take);
            m_headerFill += take;
            data += take;
            len -= take;
            if (m_headerFill < TAR_BLOCK) continue;
            m_headerFill = 0;
            if (!HandleHeader(error)) return false;
        }
        return true;
    }

    bool Finished(std::string& error) const
    {
        if (m_file || m_dataLeft > 0) {
            error = "archive ended in the middle of " + m_currentName;
            return false;
        }
        if (!m_done) {
            error = "archive ended without its end marker";
            return false;
        }
        return true;
    }

private:
    bool HandleHeader(std::string& error)
    {
        bool allZero = true;
        for (size_t i = 0; i < TAR_BLOCK; i++) {
            if (m_header[i]) { allZero = false; break; }
        }
        if (allZero) {
            if (++m_zeroBlocks >= 2) m_done = true;
            return true;
        }
        m_zeroBlocks = 0;

        char type = char(m_header[156]);
        uint64_t size = ParseTarSize(m_header + 124, 12);

        std::string name(reinterpret_cast<char*>(m_header), strnlen(reinterpret_cast<char*>(m_header), 100));
        char* prefix = reinterpret_cast<char*>(m_header) + 345;
        size_t prefixLen = strnlen(prefix, 155);
        if (prefixLen > 0) name = std::string(prefix, prefixLen) + "/" + name;

        if (type == 'L' || type == 'K') {
            error = "archive uses GNU long names, which this reader refuses";
            return false;
        }
        if (type == 'x' || type == 'g') {
            // pax metadata: no use for it, skip the payload
            SetSkip(size);
            return true;
        }
        if (type == '5') {
            if (!SafeEntryPath(name, m_allowed, error)) return false;
            fs::create_directories(m_dest / fs::path(name));
            SetSkip(size); // dirs have no data, but stay uniform
            return true;
        }
        if (type == '0' || type == '\0') {
            if (!SafeEntryPath(name, m_allowed, error)) return false;
            fs::path out = m_dest / fs::path(name);
            fs::create_directories(out.parent_path());
            m_currentName = name;
            m_file = fopen(out.string().c_str(), "wb");
            if (!m_file) {
                error = "cannot create " + name;
                return false;
            }
            SetData(size);
            return true;
        }
        // links, fifos, devices: nothing a snapshot legitimately contains
        error = std::string("archive entry of unexpected kind '") + type + "': " + name;
        return false;
    }

    void SetData(uint64_t size)
    {
        m_dataLeft = size;
        m_padLeft = (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
        if (m_dataLeft == 0) {
            if (m_file) { fclose(m_file); m_file = nullptr; }
            m_dataLeft = m_padLeft;
            m_padLeft = 0;
        }
    }

    void SetSkip(uint64_t size)
    {
        if (m_file) { fclose(m_file); m_file = nullptr; }
        m_dataLeft = size + (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
        m_padLeft = 0;
    }

    fs::path m_dest;
    std::set<std::string> m_allowed;
    unsigned char m_header[TAR_BLOCK];
    size_t m_headerFill = 0;
    uint64_t m_dataLeft = 0;
    uint64_t m_padLeft = 0;
    FILE* m_file = nullptr;
    std::string m_currentName;
    int m_zeroBlocks = 0;
    bool m_done = false;
};

} // namespace

bool ExtractTarZst(const std::vector<fs::path>& parts,
                   const fs::path& destDir,
                   const std::set<std::string>& allowedDirs,
                   std::string& error,
                   const std::function<bool(uint64_t)>& progress)
{
    std::unique_ptr<ZSTD_DStream, size_t(*)(ZSTD_DStream*)> stream(ZSTD_createDStream(), ZSTD_freeDStream);
    if (!stream) {
        error = "could not create the zstd decoder";
        return false;
    }

    std::vector<unsigned char> inBuf(1 << 20);
    std::vector<unsigned char> outBuf(1 << 20);
    TarConsumer consumer(destDir, allowedDirs);
    uint64_t totalOut = 0;

    for (const fs::path& part : parts) {
        FILE* f = fopen(part.string().c_str(), "rb");
        if (!f) {
            error = "cannot open " + part.string();
            return false;
        }
        std::unique_ptr<FILE, int(*)(FILE*)> fileCloser(f, fclose);

        size_t got;
        while ((got = fread(inBuf.data(), 1, inBuf.size(), f)) > 0) {
            ZSTD_inBuffer zin{inBuf.data(), got, 0};
            while (zin.pos < zin.size) {
                ZSTD_outBuffer zout{outBuf.data(), outBuf.size(), 0};
                size_t rc = ZSTD_decompressStream(stream.get(), &zout, &zin);
                if (ZSTD_isError(rc)) {
                    error = std::string("zstd: ") + ZSTD_getErrorName(rc);
                    return false;
                }
                if (zout.pos > 0) {
                    if (!consumer.Feed(outBuf.data(), zout.pos, error)) return false;
                    totalOut += zout.pos;
                    if (progress && !progress(totalOut)) {
                        error = "cancelled";
                        return false;
                    }
                }
            }
        }
        if (ferror(f)) {
            error = "read error on " + part.string();
            return false;
        }
    }

    return consumer.Finished(error);
}

} // namespace snapshot
