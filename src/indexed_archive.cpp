#include "opena2/indexed_archive.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <utility>

namespace opena2 {
namespace {

bool readExact(std::ifstream& file, char* destination, std::streamsize size)
{
    file.read(destination, size);
    return file.good() || file.gcount() == size;
}

std::uint16_t readU16(const std::array<unsigned char, 4>& bytes)
{
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(bytes[1] << 8);
}

std::uint32_t readU32(const std::array<unsigned char, 4>& bytes)
{
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::optional<std::uint16_t> readCount(std::ifstream& file)
{
    std::array<unsigned char, 4> bytes{};
    if (!readExact(file, reinterpret_cast<char*>(bytes.data()), 2)) {
        return std::nullopt;
    }
    return readU16(bytes);
}

} // namespace

IndexedArchive::IndexedArchive(
    std::filesystem::path imagePath,
    std::uint64_t imageSize,
    std::vector<std::uint32_t> offsets,
    std::vector<std::string> names)
    : imagePath_(std::move(imagePath)),
      imageSize_(imageSize),
      offsets_(std::move(offsets)),
      names_(std::move(names))
{
}

std::optional<IndexedArchive> IndexedArchive::open(
    const GameDataDirectory& gameData,
    const std::filesystem::path& imagePath,
    const std::filesystem::path& indexPath,
    std::string& error)
{
    error.clear();
    auto resolvedImage = gameData.findFile(imagePath, error);
    if (!resolvedImage) {
        return std::nullopt;
    }

    auto resolvedIndex = gameData.findFile(indexPath, error);
    if (!resolvedIndex) {
        return std::nullopt;
    }

    std::error_code filesystemError;
    const auto imageSize = std::filesystem::file_size(*resolvedImage, filesystemError);
    if (filesystemError) {
        error = "Could not read archive image size: " + filesystemError.message();
        return std::nullopt;
    }

    std::ifstream image(*resolvedImage, std::ios::binary);
    if (!image) {
        error = "Could not open archive image.";
        return std::nullopt;
    }

    std::ifstream index(*resolvedIndex, std::ios::binary);
    if (!index) {
        error = "Could not open archive index.";
        return std::nullopt;
    }

    const auto imageCount = readCount(image);
    const auto indexCount = readCount(index);
    if (!imageCount || !indexCount) {
        error = "Archive image or index is missing its entry count.";
        return std::nullopt;
    }
    if (*imageCount == 0 || *imageCount != *indexCount) {
        error = "Archive image and index entry counts do not match.";
        return std::nullopt;
    }

    const auto indexSize = std::filesystem::file_size(*resolvedIndex, filesystemError);
    if (filesystemError) {
        error = "Could not read archive index size: " + filesystemError.message();
        return std::nullopt;
    }

    const std::uint64_t expectedIndexSize = 2U + static_cast<std::uint64_t>(*indexCount) * 4U;
    if (indexSize != expectedIndexSize) {
        error = "Archive index size does not match its entry count.";
        return std::nullopt;
    }

    std::vector<std::uint32_t> offsets;
    offsets.reserve(*indexCount);
    std::array<unsigned char, 4> bytes{};
    for (std::uint16_t i = 0; i < *indexCount; ++i) {
        if (!readExact(index, reinterpret_cast<char*>(bytes.data()), 4)) {
            error = "Archive index ended before all entry offsets were read.";
            return std::nullopt;
        }

        const std::uint32_t offset = readU32(bytes);
        if (offset < 2 || offset >= imageSize ||
            (!offsets.empty() && offset <= offsets.back())) {
            error = "Archive index contains an invalid entry offset.";
            return std::nullopt;
        }
        offsets.push_back(offset);
    }

    return IndexedArchive(*resolvedImage, imageSize, std::move(offsets));
}

std::optional<IndexedArchive> IndexedArchive::openNamed(
    const GameDataDirectory& gameData,
    const std::filesystem::path& imagePath,
    const std::filesystem::path& indexPath,
    std::string& error)
{
    error.clear();
    auto resolvedImage = gameData.findFile(imagePath, error);
    if (!resolvedImage) {
        return std::nullopt;
    }

    auto resolvedIndex = gameData.findFile(indexPath, error);
    if (!resolvedIndex) {
        return std::nullopt;
    }

    std::error_code filesystemError;
    const auto imageSize = std::filesystem::file_size(*resolvedImage, filesystemError);
    if (filesystemError) {
        error = "Could not read archive image size: " + filesystemError.message();
        return std::nullopt;
    }

    std::ifstream index(*resolvedIndex, std::ios::binary);
    if (!index) {
        error = "Could not open archive index.";
        return std::nullopt;
    }

    const auto count = readCount(index);
    if (!count || *count == 0) {
        error = "Named archive index is missing a valid entry count.";
        return std::nullopt;
    }

    constexpr std::uint64_t recordSize = 24;
    const auto indexSize = std::filesystem::file_size(*resolvedIndex, filesystemError);
    if (filesystemError) {
        error = "Could not read archive index size: " + filesystemError.message();
        return std::nullopt;
    }
    if (indexSize != 2U + static_cast<std::uint64_t>(*count) * recordSize) {
        error = "Named archive index size does not match its fixed-size records.";
        return std::nullopt;
    }

    std::vector<std::uint32_t> offsets;
    std::vector<std::string> names;
    offsets.reserve(*count);
    names.reserve(*count);
    std::array<unsigned char, 12> nameBytes{};
    std::array<unsigned char, 4> offsetBytes{};
    for (std::uint16_t i = 0; i < *count; ++i) {
        if (!readExact(index, reinterpret_cast<char*>(nameBytes.data()), nameBytes.size())) {
            error = "Named archive index ended before all filenames were read.";
            return std::nullopt;
        }

        std::string name;
        for (const auto character : nameBytes) {
            if (character == 0) {
                break;
            }
            if (character < 0x20 || character > 0x7e || character == '/' || character == '\\') {
                error = "Named archive index contains an invalid filename.";
                return std::nullopt;
            }
            name.push_back(static_cast<char>(character));
        }
        if (name.empty()) {
            error = "Named archive index contains an empty filename.";
            return std::nullopt;
        }

        index.seekg(8, std::ios::cur);
        if (!index) {
            error = "Could not seek past named archive metadata.";
            return std::nullopt;
        }
        if (!readExact(index, reinterpret_cast<char*>(offsetBytes.data()), offsetBytes.size())) {
            error = "Named archive index ended before all records were read.";
            return std::nullopt;
        }
        const std::uint32_t offset = readU32(offsetBytes);
        if (offset >= imageSize || (!offsets.empty() && offset <= offsets.back())) {
            error = "Named archive index contains an invalid entry offset.";
            return std::nullopt;
        }
        names.push_back(std::move(name));
        offsets.push_back(offset);
    }

    return IndexedArchive(*resolvedImage, imageSize, std::move(offsets), std::move(names));
}

std::size_t IndexedArchive::entryCount() const noexcept
{
    return offsets_.size();
}

std::optional<std::string_view> IndexedArchive::entryName(std::size_t index) const noexcept
{
    if (index >= names_.size()) {
        return std::nullopt;
    }
    return names_[index];
}

std::optional<std::vector<std::byte>> IndexedArchive::readEntry(
    std::size_t index,
    std::string& error) const
{
    error.clear();
    if (index >= offsets_.size()) {
        error = "Archive entry index is out of range.";
        return std::nullopt;
    }

    const std::uint64_t begin = offsets_[index];
    const std::uint64_t end = index + 1 < offsets_.size()
        ? offsets_[index + 1]
        : imageSize_;
    const std::uint64_t length = end - begin;
    if (length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        length > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        error = "Archive entry is too large to read.";
        return std::nullopt;
    }

    std::ifstream image(imagePath_, std::ios::binary);
    if (!image) {
        error = "Could not reopen archive image.";
        return std::nullopt;
    }

    image.seekg(static_cast<std::streamoff>(begin));
    if (!image) {
        error = "Could not seek to archive entry.";
        return std::nullopt;
    }

    std::vector<std::byte> data(static_cast<std::size_t>(length));
    if (!readExact(image, reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(length))) {
        error = "Could not read complete archive entry.";
        return std::nullopt;
    }

    return data;
}

} // namespace opena2
