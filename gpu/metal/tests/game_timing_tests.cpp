// SPDX-License-Identifier: GPL-3.0-or-later
// CPU-only differential fixtures inspired by Paleblood's limiter cases.
// Never calls game startup, initializes SDL, or creates a Metal device/window.
#include "gpu/metal/bbhost/timing_verify.h"
#include "gpu/metal/vendor/paleblood/limiter.h"
#include "engine/sprj_flipper.h"
#include "engine/frame_rate_step.h"
#include "bbhost/fixed_tick.hpp"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/mman.h>

extern "C" void* runtime_low_map(std::size_t, int);
extern "C" void bb_macos_set_guest_tls(void*);
extern "C" void runtime_restart() { std::abort(); }

namespace {
constexpr std::uint64_t base = 0x400000, t0 = 5'000'000'000;
struct Case { std::string name; SprjFlipper before; std::int64_t offset = 5000, increment = 1000; bool spin = true; };
struct ScriptClock final : PalebloodReference::Clock {
    std::uint64_t at, last = 0;
    std::int64_t increment;
    unsigned reads = 0, sleeps = 0;
    ScriptClock(std::int64_t offset, std::int64_t step) : at(t0 + offset), increment(step) {}
    std::uint64_t Now() override {
        // Exhausted scripts cannot count as a match, even if both sides stop.
        if (++reads > 256) { std::fputs("Incomplete clock script\n", stderr); std::exit(3); }
        last = at; at += increment; return last;
    }
    void Sleep(std::uint32_t) override { ++sleeps; }
};
ScriptClock* guest_clock;
bool guest_spin = true;
extern "C" int fixture_gettimeofday(std::int64_t* out, void* timezone) {
    if (timezone) std::exit(3);
    const auto now = guest_clock->Now();
    out[0] = static_cast<std::int64_t>(now / 1'000'000);
    out[1] = static_cast<std::int64_t>(now % 1'000'000);
    return 0;
}
extern "C" int fixture_usleep(std::uint32_t us) { guest_clock->Sleep(us); return 0; }
extern "C" std::uint8_t fixture_spin() { return guest_spin; }
extern "C" void fixture_fatal() { std::fputs("Unexpected guest fatal-error call\n", stderr); std::exit(3); }

struct Image {
    unsigned char* bytes = nullptr;
    std::size_t size = 0;
    std::array<std::uint64_t, 32> tcb{};
    std::array<std::uint64_t, 32> import_data{};
    template<class T> static T get(const std::vector<unsigned char>& data, std::size_t offset) {
        if (offset > data.size() || sizeof(T) > data.size() - offset) throw std::runtime_error("Truncated ELF");
        T result; std::memcpy(&result, data.data() + offset, sizeof(T)); return result;
    }
    explicit Image(const char* path) {
        std::ifstream input(path, std::ios::binary);
        std::vector<unsigned char> file(std::istreambuf_iterator<char>(input), {});
        if (file.size() < 64 || std::memcmp(file.data(), "\177ELF\2\1", 6)) throw std::runtime_error("Expected ELF64");
        const auto headers = get<std::uint64_t>(file, 32);
        const auto stride = get<std::uint16_t>(file, 54), count = get<std::uint16_t>(file, 56);
        if (stride != 56 || !count || headers > file.size() || count > (file.size() - headers) / stride)
            throw std::runtime_error("Invalid ELF headers");
        struct Load { std::uint64_t off, va, files, memory; };
        std::vector<Load> loads;
        for (unsigned k = 0; k < count; ++k) {
            const auto ph = headers + k * stride;
            const auto kind = get<std::uint32_t>(file, ph);
            if (kind != 1 && kind != 0x61000010) continue;
            Load s{get<std::uint64_t>(file, ph + 8), get<std::uint64_t>(file, ph + 16),
                   get<std::uint64_t>(file, ph + 32), get<std::uint64_t>(file, ph + 40)};
            if (s.files > s.memory || s.off > file.size() || s.files > file.size() - s.off ||
                s.va > 1024 * 1024 * 1024 || s.memory > 1024 * 1024 * 1024 - s.va)
                throw std::runtime_error("Invalid ELF load segment");
            loads.push_back(s); size = std::max<std::size_t>(size, s.va + s.memory);
        }
        if (!size) throw std::runtime_error("No ELF load segments");
        bytes = static_cast<unsigned char*>(runtime_low_map(size, PROT_READ | PROT_WRITE));
        if (!bytes) throw std::runtime_error("Cannot allocate fixture image");
        for (auto& s : loads) std::memcpy(bytes + s.va, file.data() + s.off, s.files);
        unsigned char digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(bytes, CC_LONG(size), digest);
        char hex[65]{}; for (int i = 0; i < 32; ++i) std::snprintf(hex + i * 2, 3, "%02x", digest[i]);
        if (std::string(hex) != "071df19c8880086d97182dbc057bc8cb37badaca57d9112683836b24a0444c0a")
            throw std::runtime_error("Unsupported loaded image; no guest instructions executed");
        // Resolve ELF-local pointers, including GOT indirections to globals.
        // Unscripted function imports fail loudly; data imports use a fixture.
        std::uint64_t dynamic = 0, dynamic_size = 0, dynlib = 0;
        for (unsigned k = 0; k < count; ++k) {
            const auto ph = headers + k * stride;
            const auto kind = get<std::uint32_t>(file, ph);
            if (kind == 2) { dynamic = get<std::uint64_t>(file, ph + 8); dynamic_size = get<std::uint64_t>(file, ph + 32); }
            if (kind == 0x61000000) dynlib = get<std::uint64_t>(file, ph + 8);
        }
        std::map<std::uint64_t, std::uint64_t> tags;
        for (std::uint64_t pos = dynamic; pos < dynamic + dynamic_size; pos += 16) {
            const auto tag = get<std::uint64_t>(file, pos);
            if (!tag) break;
            tags[tag] = get<std::uint64_t>(file, pos + 8);
        }
        for (const auto& pair : {std::pair{0x61000029ull, 0x6100002dull}, std::pair{0x6100002full, 0x61000031ull}}) {
            const auto start = dynlib + tags.at(pair.first), length = tags.at(pair.second);
            if (length % 24) throw std::runtime_error("Invalid relocation table");
            for (std::uint64_t pos = start; pos < start + length; pos += 24) {
                const auto target = get<std::uint64_t>(file, pos), info = get<std::uint64_t>(file, pos + 8);
                const auto addend = get<std::int64_t>(file, pos + 16);
                const auto kind = std::uint32_t(info);
                std::uint64_t value;
                if (kind == 8) value = reinterpret_cast<std::uint64_t>(bytes) + addend;
                else if (kind == 1 || kind == 6 || kind == 7) {
                    const auto symbol = dynlib + tags.at(0x61000039) + (info >> 32) * 24;
                    const auto section = get<std::uint16_t>(file, symbol + 6);
                    if (section) value = (section == 0xfff1 ? 0 : reinterpret_cast<std::uint64_t>(bytes)) +
                                         get<std::uint64_t>(file, symbol + 8) + addend;
                    else value = ((get<std::uint8_t>(file, symbol + 4) & 15) == 1
                                  ? reinterpret_cast<std::uint64_t>(import_data.data())
                                  : reinterpret_cast<std::uint64_t>(&fixture_fatal)) + addend;
                } else throw std::runtime_error("Unsupported fixture relocation");
                put(target + base, value);
            }
        }
        tcb[0] = reinterpret_cast<std::uint64_t>(tcb.data());
        bb_macos_set_guest_tls(tcb.data());
        if (mprotect(bytes, (size + 4095) & ~std::size_t(4095), PROT_READ | PROT_WRITE | PROT_EXEC))
            throw std::runtime_error("Cannot make fixture image executable");
        jump(0x02fbe728, reinterpret_cast<void*>(&fixture_gettimeofday));
        jump(0x02fbfe68, reinterpret_cast<void*>(&fixture_usleep));
        jump(0x013e3980, reinterpret_cast<void*>(&fixture_spin));
        jump(0x024b55b0, reinterpret_cast<void*>(&fixture_fatal));
        put<std::uint64_t>(0x05940500, 1); // checked only for presence by this function
        put<std::uint64_t>(0x05940e00, 0);
    }
    ~Image() { if (bytes) munmap(bytes, (size + 4095) & ~std::size_t(4095)); }
    template<class T> void put(std::uint64_t address, T value) {
        if (address < base || address - base > size || sizeof(T) > size - (address - base))
            throw std::runtime_error("Fixture address outside ELF");
        std::memcpy(bytes + address - base, &value, sizeof(value));
    }
    void jump(std::uint64_t address, void* fn) {
        put<std::uint32_t>(address, 0x000025ff);
        put<std::uint16_t>(address + 4, 0);
        put<std::uint64_t>(address + 6, reinterpret_cast<std::uint64_t>(fn));
    }
    void Run(SprjFlipper& state, ScriptClock& clock, bool spin) {
        guest_clock = &clock; guest_spin = spin;
        reinterpret_cast<void(*)(SprjFlipper*)>(bytes + 0x02434770 - base)(&state);
    }
};

std::vector<Case> cases() {
    SprjFlipper f{};
    f.primary_flip_mode_raw = 1;
    f.secondary_flip_mode_raw = 0;
    f.frame_advance_count = 2; f.allow_frame_skip = 1;
    f.target_frame_seconds = std::bit_cast<float>(0x3d088889u);
    f.previous_frame_microseconds = t0 - 33000; f.current_frame_microseconds = t0;
    f.foreground_history_count = 1; f.background_history_count = 30;
    f.override_foreground_history_count = f.override_background_history_count = -1;
    for (int k = 0; k < 16; ++k) f.fps_frame_time_history[k] = .030f + .0005f * k;
    // Nonzero padding catches implementations that initialize untouched bytes.
    std::memset(f.pad15, 0xa5, sizeof(f.pad15)); std::memset(f.pad1c, 0xa5, sizeof(f.pad1c));
    std::vector<Case> result;
    for (unsigned mode = 0; mode <= 4; ++mode) {
        auto state = f; state.primary_flip_mode_raw = mode;
        result.push_back({"mode_" + std::to_string(mode), state});
    }
    auto add = [&](const char* name, auto edit) { auto state = f; edit(state); result.push_back({name, state}); };
    add("pending_mode", [](auto& s) { s.secondary_mode_requested = 1; s.secondary_flip_mode_raw = 2; });
    add("reset_to_30", [](auto& s) { s.primary_flip_mode_raw = 2; s.reset_to_30_fps_pending = 1; });
    add("override_windows", [](auto& s) { s.override_foreground_history_count = 5; s.override_background_history_count = 12; });
    add("clear_history", [](auto& s) { s.reset_frame_history = 1; });
    add("force_advance", [](auto& s) { s.force_frame_advance = 1; });
    add("forced_late", [](auto& s) { s.force_no_sleep = 1; });
    add("ring_wrap", [](auto& s) { s.frame_history_index = 31; });
    add("catch_up", [](auto& s) { s.frame_behind = 1; s.frame_history[0] = {90000, 1, {}}; });
    add("tiny_history", [](auto& s) { for (float& v : s.fps_frame_time_history) v = 0; s.force_no_sleep = 1; });
    add("missing_notify", [](auto& s) { s.request_window_60_hz = 1; });
    add("invalid_mode", [](auto& s) { s.primary_flip_mode_raw = 9; s.frame_advance_count = 7; });
    result.push_back({"over_budget", f, 50000});
    result.push_back({"exact_budget", f, 33333, 1});
    result.push_back({"sleep_path", f, 1000, 7000, false});
    auto backwards = f; backwards.force_no_sleep = 1;
    result.push_back({"clock_backwards_forced_late", backwards, -2000000, 40000});
    return result;
}

int mismatch(const SprjFlipper& a, const SprjFlipper& b) {
    const auto* left = reinterpret_cast<const unsigned char*>(&a);
    const auto* right = reinterpret_cast<const unsigned char*>(&b);
    for (std::size_t i = 0; i < sizeof(a); ++i) if (left[i] != right[i]) return static_cast<int>(i);
    return -1;
}

unsigned check_steps() {
    unsigned checks = 0;
    for (int fps : {30, 60, 90, 120, 144, 165, 240}) {
        for (float scale : {.5f, 1.f, 2.f}) {
            MacTimingVerify::ResetStep();
            std::int64_t milliseconds = 0;
            const float target = 1.f / fps;
            const float expected = std::min(target * scale, 1.f / 30);
            for (int k = 0; k < fps * 10; ++k) {
                const auto step = MacTimingVerify::Step(target, target, scale);
                if (step.seconds != expected) throw std::runtime_error("Production task step differs from frame-time contract");
                milliseconds += step.play_ms;
            }
            if (std::abs(milliseconds - std::int64_t(double(expected) * fps * 10000)) > 1)
                throw std::runtime_error("Play-time clock lost fractional milliseconds");
            ++checks;
        }
        for (float measured : {0.f, .001f, .020f, .2f}) {
            MacTimingVerify::ResetStep();
            const auto step = MacTimingVerify::Step(measured, 1.f / fps);
            if (step.seconds != std::min(std::max(measured, 1.f / fps), 1.f / 30))
                throw std::runtime_error("Task delta clamp differs from FPS++ contract");
            ++checks;
        }
        // An independent libm oracle for bbhost's camera decay/retention.
        for (float weight : {.01f, .1f, .5f, .95f}) {
            const float frames = 30.f / fps;
            const double expected = 1 - std::pow(1 - double(weight), double(frames));
            if (std::abs(frame_rate_step::weight(weight, frames) - expected) > 1.e-6)
                throw std::runtime_error("Camera smoothing drift");
            ++checks;
        }
    }
    MacTimingVerify::ResetStep();
    // A fluctuating cap and loading frames must use actual measured time.
    for (float measured : {.004f, .025f, .010f, .1f, .006f}) {
        if (MacTimingVerify::Step(measured, 0).seconds != std::min(measured, 1.f / 30))
            throw std::runtime_error("Loading delta clamp differs");
        ++checks;
    }
    return checks;
}
}

int main(int argc, char** argv) {
    try {
        const char* elf = nullptr;
        bool mutate = false;
        for (int i = 1; i < argc; ++i) {
            if (std::string(argv[i]) == "--elf" && i + 1 < argc) elf = argv[++i];
            else if (std::string(argv[i]) == "--mutate") mutate = true;
            else if (std::string(argv[i]) != "--self-test") throw std::runtime_error("Usage: bb-macos-timing-tests [--elf FILE] [--mutate]");
        }
        std::unique_ptr<Image> image;
        if (elf) image = std::make_unique<Image>(elf);
        unsigned passed = 0, failed = 0;
        std::printf("{\"schema\":1,\"original_executed\":%s,\"cases\":[", image ? "true" : "false");
        for (const auto& c : cases()) {
            auto reference = c.before, actual = c.before;
            ScriptClock clock(c.offset, c.increment);
            PalebloodReference::PaceFrame(reference, clock, c.spin, nullptr);
            int original_diff = -1;
            if (image) {
                auto original = c.before;
                ScriptClock original_clock(c.offset, c.increment);
                image->Run(original, original_clock, c.spin);
                original_diff = mismatch(original, reference);
                // Compare observable state and exact scripted import behavior.
                if (original_clock.reads != clock.reads || original_clock.sleeps != clock.sleeps) original_diff = -2;
            }
            MacTimingVerify::Replay(actual, static_cast<std::int64_t>(clock.last));
            if (mutate) actual.measured_frame_seconds = std::bit_cast<float>(std::bit_cast<std::uint32_t>(actual.measured_frame_seconds) ^ 1u);
            const int diff = mismatch(actual, reference);
            const bool ok = diff == -1 && original_diff == -1;
            std::printf("%s{\"id\":\"%s\",\"passed\":%s,\"bbhost_byte_difference\":%d,\"original_byte_difference\":%d,\"clock_reads\":%u}",
                        passed + failed ? "," : "", c.name.c_str(), ok ? "true" : "false", diff, original_diff, clock.reads);
            if (ok) ++passed; else ++failed;
        }
        const unsigned steps = check_steps();
        std::printf("],\"passed\":%u,\"failed\":%u,\"step_checks\":%u,\"mutation\":%s}\n", passed, failed, steps, mutate ? "true" : "false");
        return failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Timing verification incomplete: %s\n", error.what());
        return 3;
    }
}
