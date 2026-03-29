#include "game/SaveSystem.hpp"
#include "game/ChunkRegistry.hpp"
#include "game/Chunk.hpp"
#include <lz4.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <filesystem>

namespace voxelmmo {

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════════════════
// ═══ CONSTRUCTION & PATHS ══════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════

SaveSystem::SaveSystem(const std::string& gameKey)
    : gameKey_(gameKey)
    , baseDir_("saves/" + gameKey)
    , chunksDir_(baseDir_ + "/chunks")
{
    ensureDirectoriesExist();
}

std::string SaveSystem::getChunkFilename(ChunkId id) const {
    // Use packed ChunkId in hex for a unique, filesystem-safe name
    std::ostringstream oss;
    oss << chunksDir_ << "/" << std::hex << std::setfill('0') << std::setw(16) << id.packed << ".chunk";
    return oss.str();
}

bool SaveSystem::ensureDirectoriesExist() {
    try {
        fs::create_directories(chunksDir_);
        return true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[SaveSystem] Failed to create directories: " << e.what() << "\n";
        return false;
    }
}

std::string SaveSystem::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════════════
// ═══ GLOBAL STATE (YAML-like) ═══════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════

// Helper: trim whitespace from both ends
static std::string trim(std::string_view sv) {
    size_t start = 0;
    while (start < sv.size() && std::isspace(sv[start])) ++start;
    size_t end = sv.size();
    while (end > start && std::isspace(sv[end - 1])) --end;
    return std::string(sv.substr(start, end - start));
}

void SaveSystem::loadOrCreateGlobalState(uint32_t cliSeed, GeneratorType cliType) {
    const std::string globalPath = baseDir_ + "/global.yaml";
    
    // Try to load existing state
    if (fs::exists(globalPath)) {
        try {
            std::ifstream file(globalPath);
            if (!file.is_open()) {
                throw std::runtime_error("Cannot open global.yaml");
            }
            
            std::string line;
            while (std::getline(file, line)) {
                // Skip empty lines and comments
                std::string_view sv = line;
                size_t nonSpace = sv.find_first_not_of(" \t");
                if (nonSpace == std::string_view::npos) continue;
                if (sv[nonSpace] == '#') continue;
                
                // Find key:value separator
                size_t colonPos = sv.find(':');
                if (colonPos == std::string_view::npos) continue;
                
                std::string key = trim(sv.substr(0, colonPos));
                std::string value = trim(sv.substr(colonPos + 1));
                
                if (key == "gameKey") globalState_.gameKey = value;
                else if (key == "version") globalState_.version = static_cast<uint32_t>(std::stoul(value));
                else if (key == "seed") globalState_.seed = static_cast<uint32_t>(std::stoul(value));
                else if (key == "generatorType") globalState_.generatorType = (value == "TEST") ? GeneratorType::TEST : GeneratorType::NORMAL;
                else if (key == "createdAt") globalState_.createdAt = value;
                else if (key == "lastSavedAt") globalState_.lastSavedAt = value;
            }
            file.close();
            
            std::cout << "[SaveSystem] Loaded existing save: " << globalState_.gameKey 
                      << " (seed=" << globalState_.seed << ", type=" 
                      << (globalState_.generatorType == GeneratorType::TEST ? "TEST" : "NORMAL") << ")\n";
            return;
            
        } catch (const std::exception& e) {
            std::cerr << "[SaveSystem] Failed to load global.yaml: " << e.what() 
                      << ", creating new save\n";
        }
    }
    
    // Create new state from CLI parameters
    globalState_.gameKey = gameKey_;
    globalState_.version = FORMAT_VERSION;
    globalState_.seed = cliSeed;
    globalState_.generatorType = cliType;
    globalState_.createdAt = getCurrentTimestamp();
    globalState_.lastSavedAt = globalState_.createdAt;
    
    saveGlobalState();
    
    std::cout << "[SaveSystem] Created new save: " << globalState_.gameKey 
              << " (seed=" << globalState_.seed << ", type=" 
              << (cliType == GeneratorType::TEST ? "TEST" : "NORMAL") << ")\n";
}

void SaveSystem::saveGlobalState() {
    globalState_.lastSavedAt = getCurrentTimestamp();
    
    const std::string globalPath = baseDir_ + "/global.yaml";
    std::ofstream file(globalPath);
    if (!file.is_open()) {
        std::cerr << "[SaveSystem] Failed to open " << globalPath << " for writing\n";
        return;
    }
    
    file << "# voxelmmo save metadata\n";
    file << "gameKey: " << globalState_.gameKey << "\n";
    file << "version: " << globalState_.version << "\n";
    file << "seed: " << globalState_.seed << "\n";
    file << "generatorType: " << (globalState_.generatorType == GeneratorType::TEST ? "TEST" : "NORMAL") << "\n";
    file << "createdAt: " << globalState_.createdAt << "\n";
    file << "lastSavedAt: " << globalState_.lastSavedAt << "\n";
    
    file.close();
}

// ═══════════════════════════════════════════════════════════════════════════
// ═══ CHUNK SAVE/LOAD (BINARY) ══════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════

bool SaveSystem::hasSavedChunk(ChunkId id) const {
    return fs::exists(getChunkFilename(id));
}

bool SaveSystem::loadChunkVoxels(ChunkId id, std::vector<VoxelType>& outVoxels) {
    const std::string filename = getChunkFilename(id);
    
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    // Read header
    uint32_t magic;
    uint32_t version;
    int64_t chunkIdPacked;
    uint32_t flags;
    uint32_t uncompSize;
    uint32_t storedSize;
    
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&chunkIdPacked), sizeof(chunkIdPacked));
    file.read(reinterpret_cast<char*>(&flags), sizeof(flags));
    file.read(reinterpret_cast<char*>(&uncompSize), sizeof(uncompSize));
    file.read(reinterpret_cast<char*>(&storedSize), sizeof(storedSize));
    
    if (file.fail()) {
        std::cerr << "[SaveSystem] Failed to read chunk header: " << filename << "\n";
        return false;
    }
    
    // Validate header
    if (magic != CHUNK_MAGIC) {
        std::cerr << "[SaveSystem] Invalid chunk magic in: " << filename << "\n";
        return false;
    }
    
    if (version != FORMAT_VERSION) {
        std::cerr << "[SaveSystem] Unsupported chunk version " << version << " in: " << filename << "\n";
        return false;
    }
    
    if (chunkIdPacked != id.packed) {
        std::cerr << "[SaveSystem] ChunkId mismatch in: " << filename << "\n";
        return false;
    }
    
    if (uncompSize != CHUNK_VOXEL_COUNT) {
        std::cerr << "[SaveSystem] Unexpected uncompressed size " << uncompSize << " in: " << filename << "\n";
        return false;
    }
    
    // Resize output buffer
    outVoxels.resize(CHUNK_VOXEL_COUNT);
    
    // Read data
    if (flags & CHUNK_FLAG_COMPRESSED) {
        // Read compressed data and decompress
        std::vector<char> compressed(storedSize);
        file.read(compressed.data(), storedSize);
        
        if (file.fail()) {
            std::cerr << "[SaveSystem] Failed to read compressed chunk data: " << filename << "\n";
            return false;
        }
        
        int result = LZ4_decompress_safe(
            compressed.data(),
            reinterpret_cast<char*>(outVoxels.data()),
            static_cast<int>(storedSize),
            static_cast<int>(CHUNK_VOXEL_COUNT)
        );
        
        if (result != static_cast<int>(CHUNK_VOXEL_COUNT)) {
            std::cerr << "[SaveSystem] LZ4 decompression failed for: " << filename << "\n";
            return false;
        }
    } else {
        // Read uncompressed data directly
        file.read(reinterpret_cast<char*>(outVoxels.data()), CHUNK_VOXEL_COUNT);
        
        if (file.fail()) {
            std::cerr << "[SaveSystem] Failed to read chunk data: " << filename << "\n";
            return false;
        }
    }
    
    return true;
}

bool SaveSystem::saveChunkVoxels(ChunkId id, const std::vector<VoxelType>& voxels) {
    if (voxels.size() != CHUNK_VOXEL_COUNT) {
        std::cerr << "[SaveSystem] Invalid voxel buffer size: " << voxels.size() << "\n";
        return false;
    }
    
    const std::string filename = getChunkFilename(id);
    const std::string tempFilename = filename + ".tmp";
    
    // Try to compress
    int maxCompSize = LZ4_compressBound(static_cast<int>(CHUNK_VOXEL_COUNT));
    std::vector<char> compressed(maxCompSize);
    
    int compSize = LZ4_compress_default(
        reinterpret_cast<const char*>(voxels.data()),
        compressed.data(),
        static_cast<int>(CHUNK_VOXEL_COUNT),
        maxCompSize
    );
    
    bool useCompression = (compSize > 0 && static_cast<size_t>(compSize) < CHUNK_VOXEL_COUNT / 2);
    
    // Write to temp file first (atomic save)
    std::ofstream file(tempFilename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[SaveSystem] Failed to open temp file: " << tempFilename << "\n";
        return false;
    }
    
    // Write header
    uint32_t magic = CHUNK_MAGIC;
    uint32_t version = FORMAT_VERSION;
    int64_t chunkIdPacked = id.packed;
    uint32_t flags = useCompression ? CHUNK_FLAG_COMPRESSED : 0;
    uint32_t uncompSize = static_cast<uint32_t>(CHUNK_VOXEL_COUNT);
    uint32_t storedSize = useCompression ? static_cast<uint32_t>(compSize) : static_cast<uint32_t>(CHUNK_VOXEL_COUNT);
    
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&chunkIdPacked), sizeof(chunkIdPacked));
    file.write(reinterpret_cast<const char*>(&flags), sizeof(flags));
    file.write(reinterpret_cast<const char*>(&uncompSize), sizeof(uncompSize));
    file.write(reinterpret_cast<const char*>(&storedSize), sizeof(storedSize));
    
    // Write data
    if (useCompression) {
        file.write(compressed.data(), compSize);
    } else {
        file.write(reinterpret_cast<const char*>(voxels.data()), CHUNK_VOXEL_COUNT);
    }
    
    file.close();
    
    if (file.fail()) {
        std::cerr << "[SaveSystem] Failed to write chunk file: " << tempFilename << "\n";
        fs::remove(tempFilename);
        return false;
    }
    
    // Atomic rename
    try {
        fs::rename(tempFilename, filename);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[SaveSystem] Failed to rename temp file: " << e.what() << "\n";
        fs::remove(tempFilename);
        return false;
    }
    
    return true;
}

size_t SaveSystem::saveAllChunks(const ChunkRegistry& registry) {
    size_t savedCount = 0;
    const auto& chunks = registry.getAllChunks();
    
    for (const auto& [chunkId, chunkPtr] : chunks) {
        if (saveChunkVoxels(chunkId, chunkPtr->world.voxels)) {
            ++savedCount;
        }
    }
    
    std::cout << "[SaveSystem] Saved " << savedCount << " chunks\n";
    return savedCount;
}

size_t SaveSystem::saveActiveChunks(const ChunkRegistry& registry) {
    size_t savedCount = 0;
    const auto& chunks = registry.getAllChunks();
    
    for (const auto& [chunkId, chunkPtr] : chunks) {
        // Only save active chunks (those with entities)
        if (chunkPtr->activated) {
            if (saveChunkVoxels(chunkId, chunkPtr->world.voxels)) {
                ++savedCount;
            }
        }
    }
    
    std::cout << "[SaveSystem] Saved " << savedCount << " active chunks\n";
    return savedCount;
}

} // namespace voxelmmo
