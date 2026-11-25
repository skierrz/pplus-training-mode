// Fighter and stage display names for Project+ 3.2.1 (src/menu/names.cpp).
#include "host/test.h"
#include "menu/names.h"

using namespace tm;

TEST(names, project_plus_fighters_and_fallback)
{
    CHECK_STR("Dedede", menu::characterName(0x23));
    CHECK_STR("Giga Bowser", menu::characterName(0x2C));
    CHECK_STR("Wario-Man", menu::characterName(0x2D));
    CHECK_STR("Roy", menu::characterName(0x32));
    CHECK_STR("Mewtwo", menu::characterName(0x33));
    CHECK_STR("Knuckles", menu::characterName(0x35));

    // Non-CSS gap and out-of-range kinds use the numeric fallback.
    CHECK_STR("Fighter 46", menu::characterName(0x2E));
    CHECK_STR("Fighter 52", menu::characterName(0x34));
    CHECK_STR("Fighter 99", menu::characterName(99));
}

TEST(names, project_plus_stages_and_fallback)
{
    // Remapped Brawl slots (base stages in P+ 3.2.1).
    CHECK_STR("Delfino's Secret", menu::stageName(0x03));
    CHECK_STR("Metal Cavern", menu::stageName(0x05));
    CHECK_STR("Bowser's Castle", menu::stageName(0x06));
    CHECK_STR("Saffron City", menu::stageName(0x14));
    CHECK_STR("Wario Land", menu::stageName(0x1C));
    CHECK_STR("Fountain of Dreams", menu::stageName(0x1F));

    // Melee, training, builder and expansion slots.
    CHECK_STR("Yoshi's Story", menu::stageName(0x2A));
    CHECK_STR("Dream Land", menu::stageName(0x2D));
    CHECK_STR("Pokemon Stadium 2", menu::stageName(0x2E));
    CHECK_STR("Stage Builder", menu::stageName(0x35));
    CHECK_STR("Training Room", menu::stageName(0x37));
    CHECK_STR("Dracula's Castle", menu::stageName(0x40));
    CHECK_STR("Clock Town", menu::stageName(0x42));
    CHECK_STR("WarioWare, Inc.", menu::stageName(0x4D));
    CHECK_STR("Poke Floats", menu::stageName(0x50));
    CHECK_STR("Venom", menu::stageName(0x52));

    // Non-playable and unknown kinds use the numeric fallback.
    CHECK_STR("Stage 38", menu::stageName(0x26));
    CHECK_STR("Stage 255", menu::stageName(0xFF));
}
