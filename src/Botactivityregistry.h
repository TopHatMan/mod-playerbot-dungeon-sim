#ifndef MOD_PLAYERBOT_BOT_ACTIVITY_REGISTRY_H
#define MOD_PLAYERBOT_BOT_ACTIVITY_REGISTRY_H

// ---------------------------------------------------------------------------
// Shared bot-activity reservation.
//
// Used by BOTH mod-playerbot-dungeon-sim and mod-playerbot-world-pvp so the two
// mods never send the same bot to two activities at once (e.g. a dungeon bot
// yanked away to a world-PvP hotspot mid-run).
//
// Header-only and inline on purpose: an inline function's function-local static
// is a single instance across the whole program, so every module that includes
// this file shares ONE registry at runtime — with no link-time dependency of one
// module on the other. Either mod can also be built and run on its own.
//
// IMPORTANT: keep every copy of this file byte-identical across modules (ODR).
// If you change it, change it in all modules that ship it.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <mutex>
#include <unordered_set>

namespace BotActivity
{
    inline std::mutex& _lock()
    {
        static std::mutex m;
        return m;
    }

    inline std::unordered_set<uint32_t>& _set()
    {
        static std::unordered_set<uint32_t> s;
        return s;
    }

    // guidLow is Player::GetGUID().GetCounter().
    inline void Reserve(uint32_t guidLow)
    {
        std::lock_guard<std::mutex> g(_lock());
        _set().insert(guidLow);
    }

    inline void Release(uint32_t guidLow)
    {
        std::lock_guard<std::mutex> g(_lock());
        _set().erase(guidLow);
    }

    inline bool IsReserved(uint32_t guidLow)
    {
        std::lock_guard<std::mutex> g(_lock());
        return _set().count(guidLow) != 0;
    }
}

#endif // MOD_PLAYERBOT_BOT_ACTIVITY_REGISTRY_H
