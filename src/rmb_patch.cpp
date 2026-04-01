#include "generated/acre_init.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <rex/logging/macros.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

PPC_EXTERN_IMPORT(__imp__LoadRmbRangeForFolder);
PPC_EXTERN_IMPORT(__imp__TryLoadSingleRmbIndex);

namespace {

struct RmbCacheKey {
  int bank = 0;
  std::string folder;

  bool operator==(const RmbCacheKey& other) const = default;
};

struct RmbCacheKeyHash {
  size_t operator()(const RmbCacheKey& key) const noexcept {
    size_t hash = std::hash<int>{}(key.bank);
    hash ^= std::hash<std::string>{}(key.folder) + 0x9E3779B9u + (hash << 6) + (hash >> 2);
    return hash;
  }
};

struct RmbCacheEntry {
  bool complete = false;
  std::vector<uint16_t> hits;
};

std::mutex g_rmb_cache_mutex;
std::unordered_map<RmbCacheKey, RmbCacheEntry, RmbCacheKeyHash> g_rmb_cache;

std::string GuestString(uint32_t guest_ptr) {
  if (guest_ptr == 0) {
    return {};
  }

  const auto* guest_str = REX_KERNEL_MEMORY()->TranslateVirtual<const char*>(guest_ptr);
  if (!guest_str) {
    return {};
  }

  return std::string(guest_str);
}

RmbCacheKey MakeCacheKey(int bank, uint32_t folder_guest_ptr) {
  return RmbCacheKey{
      .bank = bank,
      .folder = GuestString(folder_guest_ptr),
  };
}

const char* GetBankPrefix(int bank) {
  switch (bank) {
    case 0:
      return "s000";
    case 1:
      return "s100";
    case 2:
      return "s200";
    default:
      return nullptr;
  }
}

bool AsciiIEquals(char lhs, char rhs) {
  return std::tolower(static_cast<unsigned char>(lhs)) ==
         std::tolower(static_cast<unsigned char>(rhs));
}

bool TryParseRmbIndex(std::string_view filename, std::string_view prefix, uint16_t& out_index) {
  if (filename.size() != 12) {
    return false;
  }
  if (!std::equal(prefix.begin(), prefix.end(), filename.begin())) {
    return false;
  }
  if (filename[4] != '_' || filename[8] != '.') {
    return false;
  }
  if (!AsciiIEquals(filename[9], 'r') || !AsciiIEquals(filename[10], 'm') ||
      !AsciiIEquals(filename[11], 'b')) {
    return false;
  }
  if (!std::isdigit(static_cast<unsigned char>(filename[5])) ||
      !std::isdigit(static_cast<unsigned char>(filename[6])) ||
      !std::isdigit(static_cast<unsigned char>(filename[7]))) {
    return false;
  }

  out_index = static_cast<uint16_t>((filename[5] - '0') * 100 + (filename[6] - '0') * 10 +
                                    (filename[7] - '0'));
  return true;
}

std::vector<uint16_t> EnumerateRmbHits(const RmbCacheKey& key) {
  std::vector<uint16_t> hits;

  const char* prefix = GetBankPrefix(key.bank);
  auto* kernel_state = REX_KERNEL_STATE();
  if (!prefix || key.folder.empty() || !kernel_state || !kernel_state->emulator()) {
    return hits;
  }

  const auto dir = kernel_state->emulator()->game_data_root() / "model" / "poly" / key.folder;
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) {
    return hits;
  }

  std::filesystem::directory_iterator iter(dir, ec);
  std::filesystem::directory_iterator end;
  while (!ec && iter != end) {
    const auto& entry = *iter;
    if (entry.is_regular_file(ec)) {
      const auto filename = entry.path().filename().string();
      uint16_t index = 0;
      if (TryParseRmbIndex(filename, prefix, index)) {
        hits.push_back(index);
      }
    }
    iter.increment(ec);
  }

  std::sort(hits.begin(), hits.end());
  hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
  return hits;
}

}  // namespace

PPC_FUNC_IMPL(LoadRmbRangeForFolder) {
  const uint32_t loader = ctx.r3.u32;
  const int bank = ctx.r4.s32;
  const uint32_t folder_guest_ptr = ctx.r5.u32;
  const auto key = MakeCacheKey(bank, folder_guest_ptr);

  const char* prefix = GetBankPrefix(bank);
  if (!prefix || key.folder.empty() || !REX_KERNEL_STATE() || !REX_KERNEL_STATE()->emulator()) {
    ctx.r3.u32 = loader;
    ctx.r4.s32 = bank;
    ctx.r5.u32 = folder_guest_ptr;
    __imp__LoadRmbRangeForFolder(ctx, base);
    return;
  }

  bool cache_hit = false;
  std::vector<uint16_t> hits;

  {
    std::scoped_lock lock(g_rmb_cache_mutex);
    auto it = g_rmb_cache.find(key);
    if (it != g_rmb_cache.end() && it->second.complete) {
      cache_hit = true;
      hits = it->second.hits;
    }
  }

  if (!cache_hit) {
    hits = EnumerateRmbHits(key);

    {
      std::scoped_lock lock(g_rmb_cache_mutex);
      auto& entry = g_rmb_cache[key];
      if (entry.complete) {
        hits = entry.hits;
        cache_hit = true;
      } else {
        entry.complete = true;
        entry.hits = hits;
      }
    }

    if (!cache_hit) {
      REXLOG_INFO("[rmb] enumerated bank={} prefix={} folder='{}' hits={}", bank, prefix,
                  key.folder, hits.size());
    }
  }

  if (cache_hit) {
    REXLOG_INFO("[rmb] cache replay bank={} prefix={} folder='{}' hits={}", bank, prefix,
                key.folder, hits.size());
  }

  for (uint16_t hit : hits) {
    ctx.r3.u32 = loader;
    ctx.r4.s32 = bank;
    ctx.r5.u32 = folder_guest_ptr;
    ctx.r6.s32 = hit;
    __imp__TryLoadSingleRmbIndex(ctx, base);
  }

  ctx.r3.s64 = 1;
}
