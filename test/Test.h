#pragma once

#define SAPPHIRE_API(...) \
    [[clang::annotate("sapphire::bind", __VA_ARGS__)]] SAPPHIRE_EXPORT

#define SAPPHIRE_EXPORT [[clang::annotate("sapphire::export")]]

class MinecraftGame {
public:
    SAPPHIRE_API("\x48\x89\x5C\x00\x00\x55")
    void init();

    SAPPHIRE_API("disp:+1,deref", "\xE8\x00\x00\x00\x00\x48")
    void tick(float a);
};